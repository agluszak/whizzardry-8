#include <cmath>
#include <string>
#include <string_view>

#include <SDL3/SDL_keycode.h>
#include <fmt/format.h>

#include "input.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/float_constants.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/mipe.h"
#include "wiz8/sr_api.h"
#include "wiz8/unicode.h"

#define MIPE_EDIT_CPP "C:\\Projects\\Wizardry 8\\Local Screens\\mipeEdit.cpp"

#define NUM_VAR_SETS 1

/* Decimal-place counter while typing a fraction into a type-1 field: -1 until
   '.' starts the fractional digits. */
// GLOBAL: WIZ8 0x0064e000
static signed char g_mipe_edit_decimal = -1;

/* Row labels for the prop field editor, one per W8MipeEditField label_index. */
// GLOBAL: WIZ8 0x0064e004
static constexpr std::string_view g_mipe_prop_labels[] = {
    "Open",   "Lockable", "Locked",   "AutoLocking", "One Way",
    "Secret", "Found",    "Jammable", "Jammed",      "Key",
};

/* Key names for the type-7 "Key" row, indexed by option_base + value. */
// GLOBAL: WIZ8 0x0064ea04
static constexpr std::string_view g_mipe_key_names[] = {
    "Any Key",
    "Master Key",
    "Gold Key",
};

/* The door-prop field table: the nine W8DoorTriggerActionData bit fields
   (labels Open .. Jammed) plus the key id written to item by
   CommitMipeEditFields. */
// GLOBAL: WIZ8 0x0064ed08
static W8MipeEditField g_mipe_prop_fields[10] = {
    {5, 0, 0, 0, {}, 0.0f, 1}, {5, 1, 0, 0, {}, 0.0f, 1}, {5, 2, 0, 0, {}, 0.0f, 1},
    {5, 3, 0, 0, {}, 0.0f, 1}, {5, 4, 0, 0, {}, 0.0f, 1}, {5, 5, 0, 0, {}, 0.0f, 1},
    {5, 6, 0, 0, {}, 0.0f, 1}, {5, 7, 0, 0, {}, 0.0f, 1}, {5, 8, 0, 0, {}, 0.0f, 1},
    {7, 9, 3, 0, {}, 0.0f, 0},
};

/* Field table per variable set; only set 0 (prop trigger flags) exists. */
// GLOBAL: WIZ8 0x0064edf8
static W8MipeEditField* g_mipe_var_set_fields[NUM_VAR_SETS] = {
    g_mipe_prop_fields,
};

/* Running digit accumulator while typing a numeric field value. */
// GLOBAL: WIZ8 0x0069c510
static float g_mipe_edit_accum;

static void DrawMipeEditFieldRow(W8MipeEditField* field, unsigned int palette, char row);
void CommitMipeEditFields(W8TriggerActionData* data, signed char bVarSet);

/* Redraws the up-to-seven visible editor rows starting at
   g_mipe_table_base; the selected row gets palette 6, the rest 15. */
#define MIPE_REDRAW_EDIT_FIELDS()                                                                  \
    do {                                                                                           \
        W8MipeState* state_ = g_mipe_state.get();                                                  \
        char shown_ =                                                                              \
            static_cast<char>(state_->edit_field_count - static_cast<char>(g_mipe_table_base));    \
        char row_;                                                                                 \
        if (shown_ >= 8) {                                                                         \
            shown_ = 7;                                                                            \
        }                                                                                          \
        ResetEditorStatusLine(-1);                                                                 \
        for (row_ = static_cast<char>(g_mipe_table_base); row_ < g_mipe_table_base + shown_;       \
             ++row_) {                                                                             \
            DrawMipeEditFieldRow(&state_->edit_fields[static_cast<int>(row_)],                     \
                                 row_ != state_->edit_selection ? 15 : 6, row_);                   \
        }                                                                                          \
    } while (0)

/* Key handler for the prop field editor: digits pick or edit a row, arrows
   scroll/adjust, Backspace and printable keys edit type-6 text, Enter commits
   to the prop's trigger action data and Escape cancels the selection. */
// FUNCTION: WIZ8 0x005c3880
void HandleMipeEditPropKey(SDL_Keycode key)
{
    W8MipeState* state = g_mipe_state.get();
    W8MipeEditField* fields = state->edit_fields;
    W8MipeEditField* field;
    Trigger* trigger;

    if (key == SDLK_BACKSPACE) {
        if (state->edit_selection != -1) {
            field = &fields[static_cast<int>(state->edit_selection)];
            if (field->type == 6 && !field->text.empty()) {
                field->text.resize(wiz8::text::previous_grapheme(field->text, field->text.size()));
            }
            MIPE_REDRAW_EDIT_FIELDS();
        }
        return;
    }
    if (((key >= SDLK_A && key <= SDLK_Z) || (key > 0x2f && key < 0x3a) || key == SDLK_SPACE) &&
        state->edit_selection != -1) {
        field = &fields[static_cast<int>(state->edit_selection)];
        if (field->type == 6) {
            key = TranslateKeyToCharacter(key, GetInputState().modifiers);
            field->text.push_back(static_cast<char>(key));
            MIPE_REDRAW_EDIT_FIELDS();
        }
        return;
    }
    switch (key) {
    case SDLK_UP:
        if (state->edit_selection == -1) {
            if (g_mipe_table_base != 0) {
                --g_mipe_table_base;
            }
        } else {
            field = &fields[static_cast<int>(state->edit_selection)];
            if (field->type == 7) {
                if (field->value > 0) {
                    --field->value;
                }
            } else if (field->type == 5) {
                field->value = field->value == 0;
            }
        }
        MIPE_REDRAW_EDIT_FIELDS();
        break;
    case SDLK_DOWN:
        if (state->edit_selection == -1) {
            if (g_mipe_table_base < state->edit_field_count - 1) {
                ++g_mipe_table_base;
            }
        } else {
            field = &fields[static_cast<int>(state->edit_selection)];
            if (field->type == 7) {
                if (field->value < field->option_count - 1) {
                    ++field->value;
                }
            } else if (field->type == 5) {
                field->value = field->value == 0;
            }
        }
        MIPE_REDRAW_EDIT_FIELDS();
        break;
    case SDLK_0:
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
    case SDLK_5:
    case SDLK_6:
    case SDLK_7:
    case SDLK_8:
    case SDLK_9:
        if (state->edit_selection == -1) {
            state->edit_selection = static_cast<signed char>(key - 0x30);
            g_mipe_edit_accum = 0.0f;
            field = &fields[static_cast<int>(state->edit_selection)];
            g_mipe_edit_decimal = -1;
            if (field->type == 6) {
                field->text.clear();
            }
        } else {
            field = &fields[static_cast<int>(state->edit_selection)];
            if (field->type == 4) {
                g_mipe_edit_accum = g_mipe_edit_accum * g_float_ten + (key - 0x30);
                field->value = static_cast<int>(g_mipe_edit_accum);
            } else if (field->type == 1) {
                g_mipe_edit_accum = g_mipe_edit_accum * g_float_ten + (key - 0x30);
                if (g_mipe_edit_decimal < 0) {
                    field->float_value = g_mipe_edit_accum;
                } else {
                    ++g_mipe_edit_decimal;
                    field->float_value =
                        static_cast<float>(g_mipe_edit_accum / pow(10.0, g_mipe_edit_decimal));
                }
            }
        }
        MIPE_REDRAW_EDIT_FIELDS();
        break;
    case SDLK_DELETE:
        if (g_mipe_edit_decimal == -1) {
            g_mipe_edit_decimal = 0;
            MIPE_REDRAW_EDIT_FIELDS();
        }
        break;
    case SDLK_RETURN:
        trigger = state->prop->GetTrigger();
        if (trigger != 0) {
            CommitMipeEditFields(trigger->m_pActionData, 0);
        }
        state->edit_selection = -1;
        MIPE_REDRAW_EDIT_FIELDS();
        break;
    case SDLK_ESCAPE:
        state->edit_selection = -1;
        MIPE_REDRAW_EDIT_FIELDS();
        break;
    }
}

/* Renders one editor row: "index) label: value" with the value format chosen
   by the field type. */
// FUNCTION: WIZ8 0x005c41d0
static void DrawMipeEditFieldRow(W8MipeEditField* field, unsigned int palette, char row)
{
    if (field->type == 1) {
        if (g_mipe_edit_decimal == 0) {
            ShowNotice(palette,
                       fmt::format("{:d}) {}: {:g}.", static_cast<int>(row),
                                   g_mipe_prop_labels[field->label_index], field->float_value));
        } else {
            ShowNotice(palette,
                       fmt::format("{:d}) {}: {:g}", static_cast<int>(row),
                                   g_mipe_prop_labels[field->label_index], field->float_value));
        }
    } else if (field->type == 2 || field->type == 4) {
        ShowNotice(palette, fmt::format("{:d}) {}: {:d}", static_cast<int>(row),
                                        g_mipe_prop_labels[field->label_index], field->value));
    } else if (field->type == 5) {
        if (field->value != 0) {
            ShowNotice(palette, fmt::format("{:d}) {}: true", static_cast<int>(row),
                                            g_mipe_prop_labels[field->label_index]));
        } else {
            ShowNotice(palette, fmt::format("{:d}) {}: false", static_cast<int>(row),
                                            g_mipe_prop_labels[field->label_index]));
        }
    } else if (field->type == 6) {
        ShowNotice(palette, fmt::format("{:d}) {}: {}", static_cast<int>(row),
                                        g_mipe_prop_labels[field->label_index], field->text));
    } else if (field->type == 7) {
        ShowNotice(palette, fmt::format("{:d}) {}: {}", static_cast<int>(row),
                                        g_mipe_prop_labels[field->label_index],
                                        g_mipe_key_names[field->option_base + field->value]));
    }
}

/* Packs the edited fields of variable set bVarSet back into a trigger's action
   data: the nine door bit fields and the key id item. */
// FUNCTION: WIZ8 0x005c4340
void CommitMipeEditFields(W8TriggerActionData* data, signed char bVarSet)
{
    W8MipeEditField* fields;

    if (bVarSet >= NUM_VAR_SETS) {
        srAssertFail("bVarSet < NUM_VAR_SETS", MIPE_EDIT_CPP, 0x20a, 0);
    }
    fields = g_mipe_var_set_fields[static_cast<int>(bVarSet)];
    if (bVarSet == 0) {
        W8DoorTriggerActionData* door = static_cast<W8DoorTriggerActionData*>(data);
        door->open = (fields[0].value & 1) != 0;
        door->lockable = (fields[1].value & 1) != 0;
        door->locked = (fields[2].value & 1) != 0;
        door->auto_locking = (fields[3].value & 1) != 0;
        door->one_way = (fields[4].value & 1) != 0;
        door->secret = (fields[5].value & 1) != 0;
        door->found = (fields[6].value & 1) != 0;
        door->jammable = (fields[7].value & 1) != 0;
        door->jammed = (fields[8].value & 1) != 0;
        door->item = static_cast<signed char>(fields[9].value);
    }
}
