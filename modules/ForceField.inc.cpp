// Included after AutoExecute.inc.cpp: uses its cast/control/pipeline helpers.
#include "ForceField.hpp"

static const int kForceFieldSpell_ = 12;
static const uintptr_t kForceFieldInfoSmall_ = 0x63CF18;
static const uintptr_t kForceFieldInfoLarge_ = 0x63CF2C;

static struct {
    bool casting;
    unsigned generation;
} g_forcefield_runtime = { false, 0 };

void ResetForceFieldRuntime_()
{
    // Lifecycle callbacks must not release a latch held by CastSpell.
    ++g_forcefield_runtime.generation;
}

static bool ForceFieldInfo_(const H3ObstacleInfo* info)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(info);
    return address == kForceFieldInfoSmall_ || address == kForceFieldInfoLarge_;
}

// Validate the container before reading it; expired entries retain their metadata.
static bool ForceFieldObstacles_(H3CombatManager* cm,
    const H3Obstacle** out_first, int* out_count)
{
    const H3Obstacle* first = cm->obstacleInfo.begin();
    const H3Obstacle* end = cm->obstacleInfo.end();
    const uintptr_t start_address = reinterpret_cast<uintptr_t>(first);
    const uintptr_t end_address = reinterpret_cast<uintptr_t>(end);
    const unsigned allocated = cm->obstacleInfo.RawSizeAllocated();
    if (!first) {
        if (end || allocated != 0) return false;
        *out_first = nullptr;
        *out_count = 0;
        return true;
    }
    if (!end || end_address < start_address) return false;
    const uintptr_t bytes = end_address - start_address;
    if (bytes % sizeof(H3Obstacle) != 0
        || bytes / sizeof(H3Obstacle) > 65536
        || allocated < bytes || allocated > 64u * 1024u * 1024u)
        return false;
    *out_first = first;
    *out_count = static_cast<int>(bytes / sizeof(H3Obstacle));
    return true;
}

H3AutoPolicy::ForceFieldPresence ReadForceField_(H3CombatManager* cm,
    int side, int anchor)
{
    if (!cm || side < 0 || side > 1 || anchor < 0) return FF_UNKNOWN;
    __try {
        const H3Obstacle* first = nullptr;
        int count = 0;
        if (!ForceFieldObstacles_(cm, &first, &count)) return FF_UNKNOWN;
        for (int i = 0; i < count; ++i) {
            const H3Obstacle& obstacle = first[i];
            if (obstacle.def && ForceFieldInfo_(obstacle.info)
                && obstacle.ownerSide == side && obstacle.anchorHex == anchor)
                return FF_PRESENT;
        }
        return FF_ABSENT;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogDebug("[ForceField] target read exception code=0x%08X side=%d anchor=%d",
            GetExceptionCode(), side, anchor);
        return FF_UNKNOWN;
    }
}

static bool ForceFieldCastWindow_(_BattleMgr_* mgr, int profile,
    const ForceFieldProfileFields& fields, unsigned generation)
{
    return o_BattleMgr == mgr && g_forcefield_runtime.generation == generation
        && g_control == CM_AUTO && g_phase == BP_COMBAT_CLOSED
        && g_active_profile == profile
        && g_forcefield[profile].anchor_hex[0] == fields.anchor_hex[0]
        && g_forcefield[profile].anchor_hex[1] == fields.anchor_hex[1];
}

bool EnsureForceFieldBeforeAction_(_BattleMgr_* mgr)
{
    // Animation pumps messages; no automatic keys or unit actions may reenter.
    if (g_cast_in_flight || g_forcefield_runtime.casting) return false;
    if (g_control != CM_AUTO || g_phase != BP_COMBAT_CLOSED) return true;
    const int profile = g_active_profile;
    if (profile < 0 || profile >= 5) return true;
    const ForceFieldProfileFields fields = g_forcefield[profile];
    if (fields.anchor_hex[0] < 0 && fields.anchor_hex[1] < 0) return true;
    const unsigned generation = g_forcefield_runtime.generation;
    int side = -1;
    int anchor = -1;
    int saved_side = -1;
    H3CombatManager* cm = nullptr;
    __try {
        if (!mgr || o_BattleMgr != mgr) return false;
        if (mgr->auto_combat || IsHiddenBattle(mgr) || IsTacticsPhase_(mgr)) return false;
        side = ResolveHumanSide_(mgr);
        if (side < 0 || side > 1) return false;
        if (mgr->current_mon_side != side || mgr->action != 0) return false;
        const int index = mgr->current_mon_index;
        if (index < 0 || index >= 21) return false;
        cm = reinterpret_cast<H3CombatManager*>(mgr);
        const H3CombatCreature& current = cm->stacks[side][index];
        if (current.numberAlive <= 0 || current.activeSpellDuration[60] > 0) return false;
        // Respect the game's single hero spell per round, even if both shields expired.
        if (mgr->hero_casted[side] != 0) return true;
        H3Hero* hero = cm->hero[side];
        if (!hero) return true;
        const H3AutoPolicy::ForceFieldPresence presence[2] = {
            ReadForceField_(cm, side, fields.anchor_hex[0]),
            ReadForceField_(cm, side, fields.anchor_hex[1])
        };
        anchor = H3AutoPolicy::SelectForceFieldAnchor(fields, presence);
        if (anchor < 0) return true;
        const int expertise = hero->GetSpellExpertise(kForceFieldSpell_, cm->specialTerrain);
        const int power = cm->heroSpellPower[side];
        if (!ForceFieldCastWindow_(mgr, profile, fields, generation)) return false;
        if (cm->currentActiveSide < 0 || cm->currentActiveSide > 1) return false;
        g_forcefield_runtime.casting = true;
        g_cast_in_flight = true;
        saved_side = CastSideGuardEnter_(cm, side);
        if (saved_side < 0) {
            g_cast_in_flight = false;
            g_forcefield_runtime.casting = false;
            return false;
        }
        LogInfo("[ForceField] cast profile=%d side=%d anchor=%d exp=%d",
            profile + 1, side, anchor, expertise);
        cm->CastSpell(kForceFieldSpell_, anchor, 0, -1, expertise, power);
        // A retry/reset may replace the battle while the spell animation pumps messages.
        if (o_BattleMgr != mgr || g_forcefield_runtime.generation != generation
            || g_phase != BP_COMBAT_CLOSED) {
            g_cast_in_flight = false;
            g_forcefield_runtime.casting = false;
            return false;
        }
        CastSideGuardLeave_(cm, saved_side);
        saved_side = -1;
        if (!ForceFieldCastWindow_(mgr, profile, fields, generation)) {
            g_cast_in_flight = false;
            g_forcefield_runtime.casting = false;
            return false;
        }
        // CastSpell returns void: record the attempt without testing its resulting obstacle.
        MarkHeroCastThisTurn_(mgr, side);
        g_cast_in_flight = false;
        g_forcefield_runtime.casting = false;
        return mgr->action == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (saved_side >= 0 && o_BattleMgr == mgr
            && g_forcefield_runtime.generation == generation
            && g_phase == BP_COMBAT_CLOSED)
            CastSideGuardLeave_(cm, saved_side);
        g_cast_in_flight = false;
        g_forcefield_runtime.casting = false;
        LogError("[ForceField] cast exception code=0x%08X side=%d anchor=%d",
            GetExceptionCode(), side, anchor);
        if (o_BattleMgr == mgr && g_forcefield_runtime.generation == generation
            && g_phase == BP_COMBAT_CLOSED) {
            PauseAutoExecution();
            g_auto_state.action_wake_stack = nullptr;
            if (g_pipeline_stage == PS_SPELL_POSTED) ClearSpellWait_();
        }
        return false;
    }
}
