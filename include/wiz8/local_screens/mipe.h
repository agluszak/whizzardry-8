#pragma once

#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>

#include "glm/ext/vector_float3.hpp"
#include "input.h"

class Trigger;
class W8Monster;
class W8Prop;
struct MonGen;

enum { W8_MIPE_NO_GROUP = 1000000 };

struct W8MipeMonsterEntry {
    std::string name;
    unsigned char kind;
    bool selectable;
};

/* One row of mipeEdit.cpp's prop field editor: a label index into the
   g_mipe_prop_labels table plus the live value slots.  type 1 edits
   float_value, types 2/4/5 edit value as int/bool, 6 edits owned UTF-8 text
   and 7 picks one of option_count names starting at option_base in the
   g_mipe_key_names table. */
struct W8MipeEditField {
    char type;
    int label_index;
    signed char option_count;
    signed char option_base;
    std::string text;
    float float_value;
    int value;
};

/* The editor owns selected monster IDs and borrows its waypoint monsters. */
struct W8MipeState {
    int SelectedMonsterLocationId() const
    {
        if (monster_ids.empty()) return -1;
        return monster_ids.front();
    }

    std::vector<int> monster_ids;

    int selected_group_id; /* group id of the last group pick; W8_MIPE_NO_GROUP = none */
    unsigned char selecting;
    unsigned char unknown_11[0x13];
    glm::vec3 drag_anchor;
    unsigned char creation_method; /* 0 exact, 1 placeholder, 2 selection */
    bool dragging;
    unsigned char padding_32[2];
    float value_34; /* initialised to 1.0 */
    int waypoint_count;
    std::vector<W8Monster*> waypoints;
    W8Monster* monster;
    float speed_step;
    Trigger* trigger;
    MonGen* generator;
    W8Prop* prop;
    W8MipeEditField* edit_fields; /* mipeEdit.cpp prop field table */
    signed char edit_field_count;
    signed char edit_selection; /* -1 = no row selected */
};

extern bool g_debug_monster_cycle;
extern std::unique_ptr<W8MipeState> g_mipe_state;
extern int g_mipe_mode;
extern int g_mipe_count;
/* First visible table row; shared by mipe.cpp's table view and mipeEdit.cpp's
   prop field editor. */
extern int g_mipe_table_base;

/* mipeEdit.cpp: prop field-editor key handler, fed MSG wParam key values by
   mipe.cpp's mode-0xd dispatcher. */
void HandleMipeEditPropKey(SDL_Keycode key);
void HandleMipeEditPropText(char32_t character);

void ShowMonsterSpeedStatus(void);
void ShowCubeParameters(void);
void ShowMonsterGeneratorStatus(void);

/* The MIPE input dispatcher fed from the main game input loop: routes key
   events through the mode state machine and returns nonzero when the event
   was consumed. */
unsigned char HandleMipeKey(const InputEvent* event);
void ShowMonsterGeneratorEditor(void);

void ToggleMipePanel(void);
/* Per-tick world-view pick while selecting: generator markers in mode 0x15,
   otherwise monster hover with single/group select semantics. */
void UpdateMipeSelection(void);
void DragSelectionWithCursor(void);
/* MIPE's world-view input dispatch: cube drag, cube pick, action menu. */
bool MipeWorldViewEvent(InputEventType event, const SDL_Point* point);

/* Any armed monster-generator marker within reach of the camera; sticky
   index resumes the scan at the last hit. Used with AnyWorldItemVisible to
   gate world-model picking. */
bool AnyMonsterGeneratorMarkerWithinReach(void);

bool IsMipeActive(void);
bool IsMipeMenuActive(void);
/* MIPE's key-event handler; consumes the atom while the editor is open. */
