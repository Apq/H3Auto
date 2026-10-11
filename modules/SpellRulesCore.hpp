#pragma once

// Pure rules shared by production and unit tests. Spell level is the database
// level (1..5), never the hero's school expertise (0..3).
namespace H3AutoPolicy {

struct BattleMagicRestrictions {
    bool anti_magic_garrison;
    bool cursed_ground;
    bool orb[2];
    bool cloak[2];
};

inline bool BattleAllowsHeroMagic(int level, const BattleMagicRestrictions& rules)
{
    if (level < 1 || level > 5) return false;
    if (rules.anti_magic_garrison || rules.orb[0] || rules.orb[1]) return false;
    if (rules.cursed_ground && level > 1) return false;
    if ((rules.cloak[0] || rules.cloak[1]) && level > 2) return false;
    return true;
}

// Dragon Father is checked before Vulnerability in the original. Neither
// caster's nor target owner's orb can remove this artifact's level immunity.
inline bool DragonFatherAllowsMagic(int level, bool target_owner_wears)
{
    return level >= 1 && level <= 5 && (!target_owner_wears || level > 4);
}

inline bool MagicHasMassVersion(unsigned flags, int expertise)
{
    if (expertise < 0 || expertise > 3) return false;
    return (flags & 0x10u) == 0 && ((flags & 0x40u) == 0 || expertise >= 3)
        && ((flags & 0x20u) == 0 || expertise >= 2);
}

} // namespace H3AutoPolicy
