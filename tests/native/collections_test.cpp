#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/npc_items.h"
#include <algorithm>
#include "wiz8/asset_paths.h"
#include "wiz8/filesystem.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "temporary_directory.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                          \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

struct Entry : W8Widget {
    Controls& owner;
    std::vector<int>& destroyed;
    bool& unlinked;
    int id;
    Entry* erase_also;
    Entry(Controls& owner, std::vector<int>& destroyed, bool& unlinked, int id,
          Entry* erase_also = nullptr)
        : owner(owner), destroyed(destroyed), unlinked(unlinked), id(id), erase_also(erase_also)
    {
    }
    ~Entry() override
    {
        unlinked = unlinked && std::ranges::find(owner.m_controls, this) == owner.m_controls.end();
        destroyed.push_back(id);
        if (erase_also) {
            owner.RemoveControl(erase_also);
            delete erase_also;
        }
    }
};

static bool ownershipAndCallbacks()
{
    Controls owner;
    std::vector<int> destroyed;
    bool unlinked = true;
    auto* first = new Entry(owner, destroyed, unlinked, 1);
    auto* second = new Entry(owner, destroyed, unlinked, 2);
    auto* third = new Entry(owner, destroyed, unlinked, 3, second);
    owner.m_controls = {first, second, third};
    // Exercise the real UI owner's teardown, including a destructor that
    // removes and destroys its neighbor from the same collection.
    owner.DestroyAllControls();
    CHECK(owner.m_controls.empty() && unlinked);
    CHECK((destroyed == std::vector<int>{3, 2, 1}));

    auto* borrowed = new Entry(owner, destroyed, unlinked, 4);
    owner.m_controls = {borrowed, nullptr, borrowed};
    owner.RemoveControl(borrowed);
    CHECK((owner.m_controls == std::vector<W8Widget*>{nullptr, borrowed}));
    CHECK(destroyed.size() == 3); // unlinking removes only the first borrowed pointer
    owner.RemoveControl(borrowed);
    owner.m_controls.clear();
    delete borrowed;
    CHECK(unlinked && destroyed.back() == 4);
    return true;
}

static std::vector<int> masterCalls;

static void laterMaster(int command)
{
    masterCalls.push_back(30 + command);
    g_remove_current_master_function = false;
}

static void firstMaster(int command)
{
    masterCalls.push_back(10 + command);
    g_remove_current_master_function = false;
    if (command == 0) {
        g_master_functions.push_back(laterMaster);
    }
}

static void removingMaster(int command)
{
    masterCalls.push_back(20 + command);
    g_remove_current_master_function = true;
}

static bool masterFunctionMutation()
{
    g_master_functions = {firstMaster, removingMaster};
    RunMasterFunctions();
    CHECK((masterCalls == std::vector<int>{10, 20}));
    CHECK((g_master_functions == std::vector<W8MasterFunction>{firstMaster, laterMaster}));
    masterCalls.clear();
    SaveMasterFunctions();
    CHECK((masterCalls == std::vector<int>{9, 29}));
    g_master_functions.clear();
    return true;
}

static bool groupMemberLookup()
{
    W8MonsterGroup group{};
    CHECK(group.MemberIds().empty() && group.MemberLocationId(0) == -1);
    std::vector<int> members{101, 202};
    group.monsters = &members;
    CHECK(group.MemberIds().size() == 2);
    CHECK(group.MemberLocationId(0) == 101 && group.MemberLocationId(1) == 202);
    CHECK(group.MemberLocationId(2) == -1 && group.MemberLocationId(-1) == -1);
    members.erase(members.begin());
    CHECK(group.MemberLocationId(0) == 202);
    return true;
}

static bool npcStockOrdering()
{
    std::array<W8ItemDatabaseRecord, 2> records{};
    records[0].unidentified_name_index = 20;
    records[1].unidentified_name_index = 10;
    auto* previous_records = g_item_records;
    g_item_records = records.data();

    auto* first = new W8NpcItemEntry{};
    auto* second = new W8NpcItemEntry{};
    auto* third = new W8NpcItemEntry{};
    first->item.iItemNo = third->item.iItemNo = 0;
    second->item.iItemNo = 1;
    first->available_at = 100;
    third->available_at = 200;
    std::vector<W8NpcItemEntry*> items{first, second, third};
    W8NpcState npc{};
    npc.items = &items;
    SortNpcItems(&npc);
    CHECK((items == std::vector<W8NpcItemEntry*>{second, first, third}));
    CHECK(first->available_at == 100 && third->available_at == 200);
    CHECK(GetNpcItemAt(&npc, -1) == nullptr && GetNpcItemAt(&npc, 3) == nullptr);
    CHECK(GetNpcItemAt(&npc, 0) == second);
    delete first;
    delete second;
    delete third;
    g_item_records = previous_records;
    return true;
}

static bool keyboardBindingReplacement()
{
    const auto root = wiz8::path_from_utf8(make_temporary_directory("wiz8-keyboard"));
    const auto path = root / "bindings.bin";
    const std::array<MGSKeyBinding, 3> bindings{{
        {'A', 0, 1, W8_MGS_COMMAND_CANCEL},
        {'B', 0, 1, W8_MGS_COMMAND_MOVE_FORWARD},
        {'C', 0, 1, W8_MGS_COMMAND_CANCEL},
    }};
    {
        auto file = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        const int count = bindings.size();
        file->write(&count, sizeof(count));
        file->write(bindings.data(), sizeof(bindings));
    }
    MGSKeyboard keyboard;
    CHECK(keyboard.GetBinding(0) == nullptr);
    {
        auto file = wiz8::open_host_file(path);
        CHECK(keyboard.Load(file.get(), true));
    }
    // A duplicate command replaces its record and moves to the end of the
    // ordered bindings. The command index must refer to that replacement.
    CHECK(keyboard.FindBinding(W8_MGS_COMMAND_MOVE_FORWARD) == 0);
    CHECK(keyboard.FindBinding(W8_MGS_COMMAND_CANCEL) == 1);
    CHECK(keyboard.GetBinding(0)->key == 'B' && keyboard.GetBinding(1)->key == 'C');
    CHECK(keyboard.GetBinding(-1) == nullptr);
    CHECK(keyboard.GetBinding(2) == keyboard.GetBinding(0));
    gfKeyState['A'] = true;
    CHECK(!keyboard.IsCommandPressed(W8_MGS_COMMAND_CANCEL));
    gfKeyState['A'] = false;
    gfKeyState['C'] = true;
    CHECK(keyboard.IsCommandPressed(W8_MGS_COMMAND_CANCEL));
    gfKeyState['C'] = false;
    keyboard.Clear();
    CHECK(keyboard.GetBinding(0) == nullptr);
    CHECK(!keyboard.IsCommandPressed(W8_MGS_COMMAND_CANCEL));
    std::filesystem::remove_all(root);
    return true;
}

static bool journalSerialization()
{
    const auto root = wiz8::path_from_utf8(make_temporary_directory("wiz8-collections"));
    const auto assets = root / "assets", user = root / "user";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(user);
    w8_native::configure_paths(
        {wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user), {"", "", ""}});
    const auto input_path = root / "journal.bin", output_path = root / "journal-copy.bin";
    const int format = 1, count = 3;
    const std::array<W8JournalEntry, count> expected{{{1, static_cast<W8FactId>(10), 0},
                                                      {5, static_cast<W8FactId>(20), 7},
                                                      {9, static_cast<W8FactId>(30), -1}}};
    static_assert(sizeof(W8JournalEntry) == 12);
    {
        auto file = wiz8::open_host_file(input_path, wiz8::OpenMode::replace);
        file->write(&format, sizeof(format));
        file->write(&count, sizeof(count));
        file->write(expected.data(), sizeof(expected));
    }
    {
        auto file = wiz8::open_host_file(input_path);
        LoadJournalEntries(file.get());
    }
    CHECK(g_fact_journal_entries && g_fact_journal_entries->size() == count);
    for (int i = 0; i < count; ++i) {
        const auto& actual = (*g_fact_journal_entries)[i];
        CHECK(actual.level == expected[i].level && actual.fact == expected[i].fact &&
              actual.alternate_text == expected[i].alternate_text);
    }
    {
        auto file = wiz8::open_host_file(output_path, wiz8::OpenMode::replace);
        SaveFactJournal(file.get());
    }
    std::ifstream input(input_path, std::ios::binary), output(output_path, std::ios::binary);
    const std::vector<char> original{std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>{}};
    const std::vector<char> roundtrip{std::istreambuf_iterator<char>(output),
                                      std::istreambuf_iterator<char>{}};
    CHECK(original == roundtrip && original.size() == 8 + 3 * 12);
    // Reusing the journal with an empty chunk must shrink its live extent.
    {
        auto file = wiz8::open_host_file(input_path, wiz8::OpenMode::replace);
        const int empty = 0;
        file->write(&format, sizeof(format));
        file->write(&empty, sizeof(empty));
    }
    {
        auto file = wiz8::open_host_file(input_path);
        LoadJournalEntries(file.get());
    }
    CHECK(g_fact_journal_entries->empty());
    delete g_fact_journal_entries;
    g_fact_journal_entries = nullptr;
    std::filesystem::remove_all(root);
    return true;
}

int main()
{
    return ownershipAndCallbacks() && masterFunctionMutation() && groupMemberLookup() &&
                   npcStockOrdering() && keyboardBindingReplacement() && journalSerialization()
               ? 0
               : 1;
}
