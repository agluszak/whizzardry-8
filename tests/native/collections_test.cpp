#include "wiz8/local_code/Controls.h"
#include <algorithm>
#include "wiz8/asset_paths.h"
#include "wiz8/filesystem.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "temporary_directory.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return false; \
} } while (0)

struct Entry : W8Widget {
    Controls& owner;
    std::vector<int>& destroyed;
    bool& unlinked;
    int id;
    Entry* erase_also;
    Entry(Controls& owner, std::vector<int>& destroyed, bool& unlinked,
          int id, Entry* erase_also = nullptr)
        : owner(owner), destroyed(destroyed), unlinked(unlinked), id(id), erase_also(erase_also) {}
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

static bool journalSerialization()
{
    const auto root = wiz8::path_from_utf8(make_temporary_directory("wiz8-collections"));
    const auto assets = root / "assets", user = root / "user";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(user);
    w8_native::configure_paths({wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user), {"", "", ""}});
    const auto input_path = root / "journal.bin", output_path = root / "journal-copy.bin";
    const int format = 1, count = 3;
    const std::array<W8JournalEntry, count> expected{{
        {1, static_cast<W8FactId>(10), 0}, {5, static_cast<W8FactId>(20), 7},
        {9, static_cast<W8FactId>(30), -1}
    }};
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
    const std::vector<char> original{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>{}};
    const std::vector<char> roundtrip{std::istreambuf_iterator<char>(output), std::istreambuf_iterator<char>{}};
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

int main() { return ownershipAndCallbacks() && journalSerialization() ? 0 : 1; }
