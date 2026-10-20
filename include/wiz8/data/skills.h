#pragma once

/* Character attributes in the order used by W8Character::attributes and the
   race/profession minimum tables. */
enum W8Attribute {
    W8_ATTRIBUTE_STRENGTH = 0,
    W8_ATTRIBUTE_INTELLIGENCE = 1,
    W8_ATTRIBUTE_PIETY = 2,
    W8_ATTRIBUTE_VITALITY = 3,
    W8_ATTRIBUTE_DEXTERITY = 4,
    W8_ATTRIBUTE_SPEED = 5,
    W8_ATTRIBUTE_SENSES = 6,
    W8_ATTRIBUTE_COUNT = 7
};

enum W8SkillCategory : unsigned char {
    W8_SKILL_CATEGORY_WEAPON,
    W8_SKILL_CATEGORY_MISCELLANEOUS,
    W8_SKILL_CATEGORY_COMBAT,
    W8_SKILL_CATEGORY_MAGIC,
    W8_SKILL_CATEGORY_EXPERT
};

/* The 41 character skills in database/index order. The retail name table at 0x0061E454 indexes labels 738..778 in this
   order. This also agrees with the recovered 0x29-entry skill tables, the spellbook/realm boundaries at
   0x18/0x1c, the expert-skill run at 0x22..0x28, and the retail manual's skill
   appendix. The four spellbook skills use an explicit SPELLBOOK qualifier here
   to avoid colliding with older TU-local aliases in Magic.cpp. */
enum W8Skill {
    W8_SKILL_NONE = -1,
    W8_SKILL_SWORD = 0x00,
    W8_SKILL_AXE = 0x01,
    W8_SKILL_POLEARM = 0x02,
    W8_SKILL_MACE_FLAIL = 0x03,
    W8_SKILL_DAGGER = 0x04,
    W8_SKILL_STAFF_WAND = 0x05,
    W8_SKILL_SHIELD = 0x06,
    W8_SKILL_MODERN_WEAPONS = 0x07,
    W8_SKILL_BOW = 0x08,
    W8_SKILL_THROWING_SLING = 0x09,
    W8_SKILL_LOCKS_TRAPS = 0x0a,
    W8_SKILL_STEALTH = 0x0b,
    W8_SKILL_MUSIC = 0x0c,
    W8_SKILL_PICKPOCKET = 0x0d,
    W8_SKILL_MARTIAL_ARTS = 0x0e,
    W8_SKILL_SCOUTING = 0x0f,
    W8_SKILL_CLOSE_COMBAT = 0x10,
    W8_SKILL_RANGED_COMBAT = 0x11,
    W8_SKILL_DUAL_WEAPONS = 0x12,
    W8_SKILL_CRITICAL_STRIKE = 0x13,
    W8_SKILL_ARTIFACTS = 0x14,
    W8_SKILL_MYTHOLOGY = 0x15,
    W8_SKILL_COMMUNICATION = 0x16,
    W8_SKILL_ENGINEERING = 0x17,
    W8_SKILL_SPELLBOOK_WIZARDRY = 0x18,
    W8_SKILL_SPELLBOOK_DIVINITY = 0x19,
    W8_SKILL_SPELLBOOK_ALCHEMY = 0x1a,
    W8_SKILL_SPELLBOOK_PSIONICS = 0x1b,
    W8_SKILL_FIRE_MAGIC = 0x1c,
    W8_SKILL_WATER_MAGIC = 0x1d,
    W8_SKILL_AIR_MAGIC = 0x1e,
    W8_SKILL_EARTH_MAGIC = 0x1f,
    W8_SKILL_MENTAL_MAGIC = 0x20,
    W8_SKILL_DIVINE_MAGIC = 0x21,
    W8_SKILL_POWER_STRIKE = 0x22,
    W8_SKILL_POWER_CAST = 0x23,
    W8_SKILL_IRON_WILL = 0x24,
    W8_SKILL_IRON_SKIN = 0x25,
    W8_SKILL_REFLEXTION = 0x26,
    W8_SKILL_SNAKESPEED = 0x27,
    W8_SKILL_EAGLE_EYE = 0x28,
    W8_SKILL_COUNT = 0x29
};

/* Ability ids stored in the profession/race tables. The names below are fixed
   either by a recovered consumer or by the profession ability lists. Those
   lists preserve the manual's special-ability order for every independently
   resolved multi-ability profession, which also identifies the Fighter and
   Ninja pairs. The three still-unresolved Faerie ids remain unnamed. */
enum W8Trait {
    W8_TRAIT_NONE = -1,
    W8_TRAIT_CANNOT_LEARN = 0x1f,
    W8_TRAIT_STAMINA_REGENERATION = 0x00, /* Fighter */
    W8_TRAIT_HEALTH_REGENERATION = 0x01, /* Lord */
    W8_TRAIT_CHEAT_DEATH = 0x02, /* Valkyrie */
    W8_TRAIT_FEARLESS = 0x03, /* Samurai, Psionic */
    W8_TRAIT_RANGED_CRITICALS = 0x04, /* Ranger */
    W8_TRAIT_THROWN_CRITICALS = 0x05, /* Ninja */
    W8_TRAIT_MONK_DAMAGE_RESISTANCE = 0x06, /* Monk */
    W8_TRAIT_EFFECTIVE_WHILE_BLIND = 0x07, /* Monk */
    W8_TRAIT_MERGE_GADGETS = 0x08, /* Gadgeteer */
    W8_TRAIT_BACKSTAB = 0x09, /* Rogue */
    W8_TRAIT_CAMP_RECOVERY_BONUS = 0x0a, /* Bard */
    W8_TRAIT_PRAY = 0x0b, /* Priest */
    W8_TRAIT_SEARCH = 0x0c, /* Ranger automatic searching */
    W8_TRAIT_REMOVE_CURSED_ITEMS = 0x0d, /* Bishop */
    W8_TRAIT_MENTAL_CONDITION_IMMUNITY = 0x0e, /* Psionic */
    W8_TRAIT_MAGIC_RESISTANCE_BONUS = 0x0f, /* Mage */
    W8_TRAIT_LIGHTNING_STRIKE = 0x10, /* Samurai */
    W8_TRAIT_TURN_UNDEAD = 0x11, /* Priest, Bishop */
    W8_TRAIT_MAKE_POTIONS = 0x12, /* Alchemist */
    W8_TRAIT_KNOCKOUT = 0x13, /* Fighter */
    W8_TRAIT_BERSERK = 0x14, /* Fighter */
    W8_TRAIT_THROWN_AUTO_PENETRATE = 0x15, /* Ninja */
    W8_TRAIT_FAERIE_BASE_ARMOR_CLASS = 0x16, /* Faerie */
    W8_TRAIT_FAERIE_REDUCED_CARRY_CAPACITY = 0x18, /* Faerie */
    W8_TRAIT_LIZARDMAN_SLOW_MAGIC_RECOVERY = 0x1b, /* Lizardman */
    W8_TRAIT_BREATHE = 0x1c, /* Dracon or breath enchantment */
    W8_TRAIT_DWARF_DAMAGE_RESISTANCE = 0x1d /* Dwarf */
};
