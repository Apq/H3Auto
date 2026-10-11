// Shared preflight for every automated hero spell. No UI, RNG, or state writes.
#include "SpellRulesCore.hpp"

static bool SpellRulesWears_(H3Hero* hero, int artifact)
{
    // The original guarantees AL; its successful return may preserve upper EAX.
    return hero && THISCALL_2(bool, 0x4D9460, hero, artifact);
}

static int SpellRulesLevel_(int spell)
{
    if (spell < 0 || spell >= 70) return -1;
    H3Spell* table = *reinterpret_cast<H3Spell**>(0x687FA8);
    return table ? table[spell].level : -1;
}

static bool HeroMagicAllowed_(H3CombatManager* cm, int side, int spell)
{
    if (!cm || side < 0 || side > 1 || spell < 0 || spell >= 70) return false;
    __try {
        // Original global gate: book, anti-magic garrison, either hero's orb.
        if (!THISCALL_3(bool, 0x41FA10, cm, side, true)) return false;
        H3AutoPolicy::BattleMagicRestrictions rules = {};
        rules.anti_magic_garrison = cm->antiMagicGarrison != 0;
        rules.cursed_ground = cm->specialTerrain == 2;
        for (int s = 0; s < 2; ++s) {
            rules.orb[s] = SpellRulesWears_(cm->hero[s], 126);
            rules.cloak[s] = SpellRulesWears_(cm->hero[s], 83);
        }
        if (!H3AutoPolicy::BattleAllowsHeroMagic(SpellRulesLevel_(spell), rules))
            return false;
        H3Hero* hero = cm->hero[side];
        if (!hero || !hero->HasSpell(spell)) return false;
        H3Army* enemy = cm->army[1 - side];
        if (!enemy) return false;
        const int cost = hero->CalculateSpellCost(spell, enemy, cm->specialTerrain);
        return cost >= 0 && hero->spellPoints >= cost && !cm->heroCasted[side];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false; // Unknown state must never authorize a restricted spell.
    }
}

static bool TargetMagicAllowed_(H3CombatManager* cm, int caster_side,
    const H3CombatCreature* target, int spell)
{
    if (!cm || !target || caster_side < 0 || caster_side > 1) return false;
    __try {
        // GetOwner follows the original hypnotize ownership rule. The native
        // resolver still owns all other creature immunities/resistance rolls.
        H3Hero* owner = target->GetOwner();
        return H3AutoPolicy::DragonFatherAllowsMagic(SpellRulesLevel_(spell),
            SpellRulesWears_(owner, 134));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
