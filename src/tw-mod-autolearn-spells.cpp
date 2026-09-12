#include "ScriptObjects.h"
#include "Log.h"
#include "Config/Config.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "SharedDefines.h"
#include <unordered_map>
#include <unordered_set>

namespace
{
    class AutoLearnSpellsPlayerScript : public PlayerScript
    {
    public:
        AutoLearnSpellsPlayerScript()
            : PlayerScript("tw_mod_autolearn_spells", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LEVEL_CHANGED })
        {
        }

        void OnLogin(Player* player) override
        {
            if (!sConfig.GetBoolDefault("AutoLearnSpells.Enable", true))
                return;

            if (sConfig.GetBoolDefault("AutoLearnSpells.CatchUpOnLogin", true))
                LearnSpellsForLevelRange(player, 1, player->GetLevel());

            EnsureShamanTotemItems(player);
        }

        void OnLevelChanged(Player* player, uint8 oldLevel) override
        {
            if (!sConfig.GetBoolDefault("AutoLearnSpells.Enable", true))
                return;

            uint8 newLevel = player->GetLevel();
            uint8 maxLevel = static_cast<uint8>(sConfig.GetIntDefault("AutoLearnSpells.MaxLevel", 60));

            if (newLevel > oldLevel && newLevel <= maxLevel)
                LearnSpellsForLevelRange(player, oldLevel + 1, newLevel);
        }

    private:
        // Spells that match the auto-learn criteria but should not be learned automatically.
        // These are typically handled via the additional-spells table below or are special/proc spells.
        std::unordered_set<uint32> const m_ignoredSpells =
        {
            20647, // Execute (invalid/trigger spell; real Execute ranks start at 5308)
            3127,  // Parry - learned at class-specific levels via additional spells
            1804,  // Pick Lock - rogue level 16 via additional spells
            8737,  // Mail - warriors/paladins start with it; hunters/shamans get it at 40
            750,   // Plate Mail - warriors/paladins get it at 40
            12678, // Tactical Mastery (trigger/talent spell)
        };

        // skill_line_ability.skill_id values that belong to each player class.
        // Used to accept SPELLFAMILY_GENERIC spells that are real class spells (e.g. paladin seals,
        // warrior stances) while rejecting pet spells and other non-player spells.
        mutable std::unordered_map<uint32, std::unordered_set<uint32>> m_classSkillIds;

        struct AdditionalSpell
        {
            uint32 spellId;
            TeamId faction = TEAM_NEUTRAL;
        };

        using AdditionalSpellsByFamily = std::unordered_map<uint32, std::vector<AdditionalSpell>>;
        using AdditionalSpellsByLevel = std::unordered_map<uint8, AdditionalSpellsByFamily>;

        // Spells that are not covered by skill_line_ability at the correct level.
        AdditionalSpellsByLevel const m_additionalSpells =
        {
            {6, {
                {SPELLFAMILY_WARRIOR, {{3127}}}, // Parry
            }},
            {8, {
                {SPELLFAMILY_HUNTER,  {{3127}}}, // Parry
                {SPELLFAMILY_PALADIN, {{3127}}}, // Parry
            }},
            {10, {
                {SPELLFAMILY_WARRIOR, {{674}}}, // Dual Wield
                {SPELLFAMILY_HUNTER,  {{674}}}, // Dual Wield
            }},
            {12, {
                {SPELLFAMILY_ROGUE,   {{3127}}}, // Parry
            }},
            {16, {
                {SPELLFAMILY_ROGUE,   {{1804}}}, // Pick Lock
            }},
            {40, {
                {SPELLFAMILY_WARRIOR, {{750}}},  // Plate Mail
                {SPELLFAMILY_PALADIN, {{750}}},  // Plate Mail
                {SPELLFAMILY_HUNTER,  {{8737}}}, // Mail
                {SPELLFAMILY_SHAMAN,  {{8737}}}, // Mail
            }},
        };

        void EnsureShamanTotemItems(Player* player) const
        {
            if (player->GetClass() != CLASS_SHAMAN)
                return;

            // Earth, Fire, Water, Air totem items required to cast shaman totem spells.
            uint32 const totemItems[4] = { 5175, 5176, 5177, 5178 };
            for (uint32 itemId : totemItems)
            {
                if (!player->HasItemCount(itemId, 1))
                    player->AddItem(itemId, 1);
            }
        }

        uint32 GetSpellFamilyForClass(uint8 playerClass) const
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:     return SPELLFAMILY_WARRIOR;
                case CLASS_PALADIN:     return SPELLFAMILY_PALADIN;
                case CLASS_HUNTER:      return SPELLFAMILY_HUNTER;
                case CLASS_ROGUE:       return SPELLFAMILY_ROGUE;
                case CLASS_PRIEST:      return SPELLFAMILY_PRIEST;
                case CLASS_SHAMAN:      return SPELLFAMILY_SHAMAN;
                case CLASS_MAGE:        return SPELLFAMILY_MAGE;
                case CLASS_WARLOCK:     return SPELLFAMILY_WARLOCK;
                case CLASS_DRUID:       return SPELLFAMILY_DRUID;
                default:                return SPELLFAMILY_GENERIC;
            }
        }

        bool IsIgnoredSpell(uint32 spellId) const
        {
            return m_ignoredSpells.find(spellId) != m_ignoredSpells.end();
        }

        std::unordered_set<uint32> const& GetClassSkillIds(uint32 classMask, uint32 classFamily) const
        {
            auto& skillIds = m_classSkillIds[classMask];
            if (!skillIds.empty())
                return skillIds;

            // Known class-specific skill_line_ability.skill_id values.
            // These identify which SPELLFAMILY_GENERIC spells still belong to a player
            // class (e.g. warrior stances, paladin seals/mounts, hunter tracking).
            // They were taken from the tw_world.skill_line_ability table by selecting
            // spells with class_mask = <class> AND spellFamilyName = 0 AND baseLevel > 0.
            // Pet-only skill IDs are intentionally omitted.
            switch (classMask)
            {
                // Warrior: Arms(26), Fury(256), Protection(257), plus custom TW Cyclone line(574)
                case 1 << (CLASS_WARRIOR - 1): skillIds.insert({26, 256, 257, 574}); break;
                // Paladin: Protection(267), Retribution(594), plus Argent mount line(549)
                case 1 << (CLASS_PALADIN - 1): skillIds.insert({267, 549, 594}); break;
                // Hunter: Beast Mastery(50), Survival(51), Marksmanship(163). 261 is pet skill.
                case 1 << (CLASS_HUNTER - 1):  skillIds.insert({50, 51, 163}); break;
                // Rogue: Assassination(38), Combat(39), Poisons(40). 633 is Pick Lock (handled separately).
                case 1 << (CLASS_ROGUE - 1):   skillIds.insert({38, 39, 40}); break;
                // Priest: Discipline(56), Holy(78), Shadow(613)
                case 1 << (CLASS_PRIEST - 1):  skillIds.insert({56, 78, 613}); break;
                // Shaman: Elemental Combat(95), Enhancement(373), Restoration(374), plus TW line(375)
                case 1 << (CLASS_SHAMAN - 1):  skillIds.insert({95, 373, 374, 375}); break;
                // Mage: Fire(6), Frost(8), Arcane(237)
                case 1 << (CLASS_MAGE - 1):    skillIds.insert({6, 8, 237}); break;
                // Warlock: Affliction(355), Demonology(354), Destruction(593)
                case 1 << (CLASS_WARLOCK - 1): skillIds.insert({354, 355, 593}); break;
                // Druid: Balance(574), Feral Combat(134), Restoration(573)
                case 1 << (CLASS_DRUID - 1):   skillIds.insert({134, 573, 574}); break;
                default: break;
            }

            for (uint32 i = 0; i < sSpellMgr.GetMaxSpellId(); ++i)
            {
                SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(i);
                if (!spellInfo)
                    continue;

                if (spellInfo->SpellFamilyName != classFamily)
                    continue;

                if (spellInfo->baseLevel == 0)
                    continue;

                SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellInfo->Id);
                for (auto itr = bounds.first; itr != bounds.second; ++itr)
                {
                    SkillLineAbilityEntry const* ability = itr->second;
                    if (!ability)
                        continue;

                    if (ability->spellId != spellInfo->Id)
                        continue;

                    if ((ability->classmask & classMask) == 0)
                        continue;

                    skillIds.insert(ability->skillId);
                }
            }

            return skillIds;
        }

        void LearnAdditionalSpells(uint8 level, Player* player)
        {
            uint32 playerFamily = GetSpellFamilyForClass(player->GetClass());
            auto levelIt = m_additionalSpells.find(level);
            if (levelIt == m_additionalSpells.end())
                return;

            auto familyIt = levelIt->second.find(playerFamily);
            if (familyIt == levelIt->second.end())
                return;

            for (auto const& spell : familyIt->second)
            {
                if (player->HasSpell(spell.spellId))
                    continue;

                if (spell.faction != TEAM_NEUTRAL && spell.faction != player->GetTeamId())
                    continue;

                player->LearnSpell(spell.spellId, false);
            }
        }

        bool IsValidSpellForPlayer(SpellEntry const* spellInfo, Player* player, uint8 level) const
        {
            if (!spellInfo)
                return false;

            uint32 playerClass = player->GetClass();
            uint32 playerClassMask = player->GetClassMask();
            uint32 playerFamily = GetSpellFamilyForClass(playerClass);

            // Ignore focus spells (hunter pet spells).
            if (spellInfo->powerType == POWER_FOCUS)
                return false;

            if (IsIgnoredSpell(spellInfo->Id))
                return false;

            // Level must match the spell's intended base level.
            if (spellInfo->baseLevel != level)
                return false;

            if (!sSpellMgr.IsSpellValid(spellInfo, player, false))
                return false;

            // Must be listed in skill_line_ability for this class/race.
            SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellInfo->Id);
            bool foundValidAbility = false;

            std::unordered_set<uint32> const& classSkillIds = GetClassSkillIds(playerClassMask, playerFamily);

            for (auto itr = bounds.first; itr != bounds.second; ++itr)
            {
                SkillLineAbilityEntry const* ability = itr->second;
                if (!ability)
                    continue;

                if (ability->spellId != spellInfo->Id)
                    continue;

                // Only trainer-taught or level-up skill spells.
                if (ability->learnOnGetSkill != 0 && ability->learnOnGetSkill != 2)
                    continue;

                if ((ability->classmask & playerClassMask) == 0)
                    continue;

                if (ability->racemask != 0 && (ability->racemask & player->GetRaceMask()) == 0)
                    continue;

                // Accept spells whose family matches the player's class, or generic-family spells
                // that use one of the class's own skill lines (e.g. warrior stances, paladin seals).
                if (spellInfo->SpellFamilyName != playerFamily &&
                    (spellInfo->SpellFamilyName != SPELLFAMILY_GENERIC || classSkillIds.find(ability->skillId) == classSkillIds.end()))
                    continue;

                foundValidAbility = true;
                break;
            }

            if (!foundValidAbility)
                return false;

            // Previous rank must be known.
            uint32 prevSpellId = sSpellMgr.GetPrevSpellInChain(spellInfo->Id);
            if (prevSpellId != 0 && !player->HasSpell(prevSpellId))
                return false;

            // Do not auto-learn talents.
            if (GetTalentSpellPos(spellInfo->Id))
                return false;

            return true;
        }

        void LearnSpellsForLevelRange(Player* player, uint8 fromLevel, uint8 toLevel)
        {
            uint32 playerFamily = GetSpellFamilyForClass(player->GetClass());
            if (playerFamily == SPELLFAMILY_GENERIC)
                return;

            for (uint8 level = fromLevel; level <= toLevel; ++level)
            {
                LearnAdditionalSpells(level, player);

                for (uint32 i = 0; i < sSpellMgr.GetMaxSpellId(); ++i)
                {
                    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(i);
                    if (!spellInfo)
                        continue;

                    if (!IsValidSpellForPlayer(spellInfo, player, level))
                        continue;

                    if (player->HasSpell(spellInfo->Id))
                        continue;

                    player->LearnSpell(spellInfo->Id, false);
                }
            }
        }
    };

    class AutoLearnSpellsWorldScript : public WorldScript
    {
    public:
        AutoLearnSpellsWorldScript()
            : WorldScript("tw_mod_autolearn_spells_world", { WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED })
        {
        }

        void OnBeforeWorldInitialized() override
        {
            sLog.outString("[tw-mod-autolearn-spells] module loaded.");
        }
    };
}

void Addtw_mod_autolearn_spellsScripts()
{
    new AutoLearnSpellsWorldScript();
    new AutoLearnSpellsPlayerScript();
}
