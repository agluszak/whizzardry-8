#pragma once
#include "wiz8/character_skills.h"
#include "wiz8/data/records.h"
#include "wiz8/rules/character.h"

class Game;

// Allocation history belonging to one character draft.
struct CharacterAllocation {
    int attribute_points_remaining;
    int attribute_points_total;
    int attribute_values[7];
    int attribute_step_limit;
    int attribute_limits[7];
    int attribute_baselines[7];
    int skill_points_remaining;
    int skill_points_total;
    int skill_points_spent[0x29];
    int skill_step_limit;
    int skill_limits[0x29];
    /* The per-skill counterpart of attribute_baselines. The
       level-up reset zeroes it, and a profession change refunds whatever each
       entry holds before the new profession's skill points are assigned. */
    int skill_baselines[0x29];
    int spell_points_remaining;
    int spell_points_total;
    int magic_skill_bonus;
};

// One editable character and the decisions made while creating or levelling it.
// The draft keeps its game binding for the whole edit, independent of the active screen.
class CharacterDraft {
public:
    explicit CharacterDraft(Game& game) : game(game) {}
    Game& game;
    W8Character character{};
    CharacterAllocation allocation{};

    bool attributesComplete() const;
    bool skillsComplete() const;
    void beginCreation();
    void beginLevelUp();
    void applyRaceProfessionTables();
    void setProfession(W8Profession);
    void setRace(W8Race);
    void setGender(W8Gender);
    void payDownAttributeDebt();
    bool canChooseProfession(W8Profession profession) const;
    void finish(bool);
    void recomputeAttributeLimits();
    void clampAttributesToBudget();
    void applyProfessionMinimumAttributes();
    void adjustAttribute(W8Attribute, int);
    void refundAllocatedAttributes();
    void recomputeSkillLimits();
    void adjustSkill(unsigned int, int);
    void resetSkillContribution(W8Skill);
    void refundSkillAllocation(W8Skill);
    void rebuildSkillAllocations();
    void clampSkillsToBudget();
    void refundAllSkillPoints();
    void finalizeSpellPointPool();
    int recountSpellPoints();
    int computeLevelUpSpellPointAward();
    void selectCreationSpell(unsigned int spell);
    void deselectCreationSpell(unsigned int spell);
    void resetSpellSelections();

private:
    void initializeCharacterPointPools();
    void refreshSkillAvailability();
    bool gender_was_forced = false;
};

/* Hand out the starting equipment table the race or profession
   selects, then the profession's own extra item. */
void AddCharacterStartingEquipment(Game&, W8Character*);
/* The six starting item ids each profession hands out, with the faerie
   race's own row last; -1 is an empty slot. */
extern int g_starting_equipment[0x10][6];
int ComputeRealmSkillDebt(W8Character* original, W8Character* edited);
int ComputeStartingEquipmentCost(Game&, W8Character*);
bool CanAffordStartingEquipment(Game&, W8Character*);
