#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/platform/input.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/ui_layer.h"

struct SpriteSheet;
struct W8ItemInstance;

/* The use-item picker. The main-game screen owns it, with its input and
   controls, for exactly as long as it is open; whoever opened it supplies
   `on_return`, which runs once an item use is chosen or the picker is
   cancelled. Abandoning the screen discards the picker without running it. */
class W8UseItemView {
public:
    explicit W8UseItemView(std::move_only_function<void()> on_return);

    W8Action draft{W8_ACTION_USE_ITEM, -1};
    std::move_only_function<void()> on_return;

    void SetInputEnabled(bool enabled) { input.setEnabled(enabled); }
    void InvalidatePanel() { dirty[1] = true; }
    /* Retarget the list at party_slot (drag, recorded item, or plain refresh
       paths), closing the picker when the slot cannot act. */
    void RefreshForSlot(int party_slot);
    /* After dropping a cursor item, rebuild the list under the current pool. */
    void Refresh();
    /* Redraw the dirty panels, or every panel when `all` is set. */
    void Paint(bool all);
    /* Per frame: hand a ready use to its character. */
    void Commit();
    /* Text-box body input and wheel rotation while the picker is open. */
    UiResult HandleTextBox(const UiPointerEvent& event);
    void TextBoxWheelAt(unsigned short y);
    void SelectCurrentLine() { SelectLine(selected_line); }

private:
    enum class Pool { Character, Party, Hand };
    struct Row {
        W8ItemInstance* item;
        W8ItemLocation location;
        bool usable;
    };

    void CreateControls();
    void SelectPool(Pool pool);
    void UpdatePoolButtons();
    void Rebuild(Pool pool, W8ItemInstance* select);
    void AppendEntry(W8ItemLocation location);
    void ShowList(W8ItemInstance* select);
    bool IsFilteredOut(W8ItemInstance* item) const;
    void SelectLine(int line);
    void OpenAssayDialog(W8ItemInstance* item);
    void UpdateDetailPanel(W8ItemInstance* item);
    void TakeIntoHand();

    Pool pool = Pool::Character;
    int selected_line = -1;
    int owner = -1;
    int hover_row = -1;
    W8ItemInstance* detail_item = nullptr;
    std::vector<Row> rows;
    UiLayer input{UiLayerOrder::ItemSelection};
    std::array<std::unique_ptr<W8ButtonWidget>, 2> pools;
    std::unique_ptr<W8ButtonWidget> filter;
    std::unique_ptr<W8ButtonWidget> close;
    W8TextBuffer caption;
    W8TextBuffer quantity;
    std::shared_ptr<const SpriteSheet> item_image;
    std::array<bool, 3> dirty{true, true, true};
};

/* The main game's open use-item picker, or null. */
W8UseItemView* UseItemPicker();
/* Open the use-item picker for a party slot. `on_return` runs when it closes
   with a use chosen or cancelled. */
bool OpenUseItemSelectView(int slot, std::move_only_function<void()> on_return = {});
/* Close the picker and return to whoever opened it. */
void CloseUseItemSelectView(void);
/* Discard the picker without returning to its opener. */
void DismissUseItemSelectView(void);
