// AttackMagic.inc.cpp
// Included after AutoExecute.inc.cpp and ForceField.inc.cpp, before BattleState.
// Scheme-level offensive spells: single-target enemy pick / fixed area anchor / field-wide.
// TryAttackMagicCast_: true = this frame may continue; false = stop the old frame.
// A submitted spell is NOT proof that an enemy was hit (resistance is original logic).

static struct AttackMagicRuntime_ {
    bool casting;
    unsigned generation;
    int attempt_turn;
    int attempt_profile;
} g_attack_magic_runtime = { false, 0, -1, -1 };

static void ResetAttackMagicRuntime_()
{
    ++g_attack_magic_runtime.generation;
    g_attack_magic_runtime.attempt_turn = -1;
    g_attack_magic_runtime.attempt_profile = -1;
    // Never release a latch held by CastSpell while animation pumps messages.
}

struct AttackMagicWindow_ {
    _BattleMgr_* mgr;
    _BattleStack_* current;
    unsigned generation;
    unsigned forcefield_generation;
    int profile;
    int side;
    int index;
    int turn;
    int count;
};

// Check only plugin-owned state before dereferencing an old combat manager.
static bool AttackMagicLifetimeValid_(const AttackMagicWindow_& window)
{
    return window.mgr && o_BattleMgr == window.mgr
        && g_attack_magic_runtime.generation == window.generation
        && g_forcefield_runtime.generation == window.forcefield_generation;
}

static bool AttackMagicWindowValid_(const AttackMagicWindow_& window)
{
    if (!AttackMagicLifetimeValid_(window) || g_phase != BP_COMBAT_CLOSED || g_control != CM_AUTO
        || g_active_profile != window.profile
        || window.profile < 0 || window.profile >= 5
        || window.side < 0 || window.side > 1
        || window.index < 0 || window.index >= 21 || !window.current)
        return false;
    if (g_status[window.profile].attack_count != window.count) return false;
    // Same address is not lifetime proof: the generation checks above are required.
    if (window.mgr->current_mon_side != window.side
        || window.mgr->current_mon_index != window.index
        || window.mgr->action != 0
        || CurrentTurnStack_(window.mgr) != window.current)
        return false;
    H3CombatManager* cm = reinterpret_cast<H3CombatManager*>(window.mgr);
    return cm->turn == window.turn && !window.mgr->auto_combat
        && !IsHiddenBattle(window.mgr) && !IsTacticsPhase_(window.mgr)
        && cm->stacks[window.side][window.index].numberAlive > 0
        && cm->stacks[window.side][window.index].activeSpellDuration[60] <= 0;
}

static bool AttackMagicRuleEquals_(const H3AutoPolicy::AttackMagicRule& a,
    const H3AutoPolicy::AttackMagicRule& b)
{
    if (a.spell_id != b.spell_id || a.target_mode != b.target_mode
        || a.position_count != b.position_count) return false;
    for (int i = 0; i < H3AutoPolicy::kAttackMagicPositionCapacity; ++i)
        if (a.positions[i] != b.positions[i]) return false;
    return true;
}

static bool AttackMagicPlayableHex_(int hex)
{
    return hex >= 0 && hex < 187 && hex % 17 != 0 && hex % 17 != 16;
}

// 白名单以 PolicyCore 为准。火墙 13 需要完整空段规则，暂不纳入。
static bool AttackMagicAllowedSpell_(int spell)
{
    return H3AutoPolicy::IsAttackMagicSpellId(spell);
}

// 24/25/26 是全场伤害，原版不需要目标格；目标选项对这些法术不生效。
static bool AttackMagicFieldWide_(int spell)
{
    return H3AutoPolicy::AttackMagicKind(spell) == 2;
}

static int AttackMagicRadius_(int spell)
{
    if (spell == 22) return 2;
    if (spell == 20 || spell == 21 || spell == 23) return 1;
    return 0; // Chain lightning chooses its first target; the original owns all jumps.
}

static int AttackMagicDistance_(int a, int b)
{
    return FASTCALL_2(INT32, 0x469250, a, b);
}

static bool AttackMagicSquareHit_(int spell, int center, int square)
{
    if (!AttackMagicPlayableHex_(square)) return false;
    const int distance = AttackMagicDistance_(center, square);
    return distance >= 0 && distance <= AttackMagicRadius_(spell)
        && (spell != 20 || distance != 0); // Ice ring excludes its center.
}

static bool AttackMagicStackHit_(int spell, int center, const H3CombatCreature& stack)
{
    if (stack.numberAlive <= 0) return false;
    if (AttackMagicSquareHit_(spell, center, stack.position)) return true;
    return stack.info.doubleWide
        && AttackMagicSquareHit_(spell, center, stack.GetSecondSquare());
}

// Geometric coverage counts each double-wide stack once. No resistance probes or AI
// evaluation functions: those may consume random numbers or reject valid player targets.
static void AttackMagicCoverage_(H3CombatManager* cm, int spell, int center,
    int side, int* enemies, int* friends)
{
    *enemies = 0;
    *friends = 0;
    for (int s = 0; s < 2; ++s) {
        for (int i = 0; i < 21; ++i) {
            if (!AttackMagicStackHit_(spell, center, cm->stacks[s][i])
                || !TargetMagicAllowed_(cm, side, &cm->stacks[s][i], spell)) continue;
            if (s == side) ++*friends;
            else ++*enemies;
        }
    }
}

static bool AttackMagicEnemyAt_(H3CombatManager* cm, int side, int hex)
{
    for (int i = 0; i < 21; ++i) {
        const H3CombatCreature& enemy = cm->stacks[1 - side][i];
        if (enemy.numberAlive <= 0) continue;
        if (enemy.position == hex
            || (enemy.info.doubleWide && enemy.GetSecondSquare() == hex))
            return true;
    }
    return false;
}

static bool AttackMagicLegalCenter_(H3CombatManager* cm, int spell,
    int expertise, int side, int hex)
{
    if (!AttackMagicPlayableHex_(hex)) return false;
    if (AttackMagicRadius_(spell) == 0) {
        if (!AttackMagicEnemyAt_(cm, side, hex)) return false;
    } else {
        int enemies = 0, friends = 0;
        AttackMagicCoverage_(cm, spell, hex, side, &enemies, &friends);
        if (enemies <= 0) return false; // Empty area centers are allowed, empty hits aren't.
    }
    return cm->CanCastSpellAtCoord(spell, expertise, hex) != 0;
}

// For ice ring, putting the center on the selected enemy would miss that square.
// Enumerate valid neighboring centers and keep one that actually covers the enemy.
// Prefer fewer friendly hits, then more enemy hits; stable hex order resolves ties.
static int AttackMagicEnemyCenter_(H3CombatManager* cm, int spell, int expertise,
    int side, const H3CombatCreature& enemy)
{
    if (!AttackMagicPlayableHex_(enemy.position)) return -1;
    if (spell != 20) {
        return AttackMagicLegalCenter_(cm, spell, expertise, side, enemy.position)
            ? enemy.position : -1;
    }
    int best = -1;
    int best_friends = 43;
    int best_enemies = -1;
    for (int hex = 0; hex < 187; ++hex) {
        if (!AttackMagicPlayableHex_(hex)
            || AttackMagicDistance_(hex, enemy.position) != 1
            || !AttackMagicStackHit_(spell, hex, enemy)
            || !AttackMagicLegalCenter_(cm, spell, expertise, side, hex))
            continue;
        int enemies = 0, friends = 0;
        AttackMagicCoverage_(cm, spell, hex, side, &enemies, &friends);
        if (friends < best_friends
            || (friends == best_friends && enemies > best_enemies)) {
            best = hex;
            best_friends = friends;
            best_enemies = enemies;
        }
    }
    return best;
}

static int AttackMagicSelectEnemyHex_(H3CombatManager* cm, int spell,
    int expertise, int side, int mode)
{
    H3AutoPolicy::TargetCandidate candidates[21] = {};
    int centers[21] = {};
    int count = 0;
    for (int i = 0; i < 21; ++i) {
        H3CombatCreature& enemy = cm->stacks[1 - side][i];
        if (enemy.numberAlive <= 0 || !CreatureInfoIndexValid_(enemy.type)
            || H3AutoPolicy::IsWarMachineType(enemy.type)
            || !TargetMagicAllowed_(cm, side, &enemy, spell)) continue;
        const int center = AttackMagicEnemyCenter_(cm, spell, expertise, side, enemy);
        if (center < 0) continue;
        H3AutoPolicy::TargetCandidate candidate =
            TargetCandidateOf_(reinterpret_cast<_BattleStack_*>(&enemy));
        candidate.speed = enemy.GetStackSpeed(); // Current buff/debuff-adjusted speed.
        centers[count] = center;
        candidates[count++] = candidate;
        LogDebug("[AttackMagic] candidate spell=%d side=%d slot=%d cid=%d center=%d ranged=%d flyer=%d speed=%d hp=%d",
            spell, 1 - side, i, enemy.type, center, candidate.ranged,
            candidate.flyer, candidate.speed, H3AutoPolicy::StackRemainingHp(candidate));
    }
    if (!count) return -1;
    // Same controlled C-runtime random source as the existing target selector.
    // Draw exactly once after filtering, and draw none for the deterministic policy.
    const uint32_t random_value = mode == 1 ? static_cast<uint32_t>(rand()) : 0;
    const int selected = H3AutoPolicy::SelectTargetIndex(candidates, count,
        mode == 1 ? H3AutoPolicy::SEL_RANDOM : H3AutoPolicy::SEL_RANGED_SPEED,
        random_value);
    return selected >= 0 && selected < count ? centers[selected] : -1;
}

// 全场伤害不需要目标格：原版分支只按 spell id 结算，落点不参与判定，
// 因此这里不调用坐标合法性检查（它对全场法术的语义未验证，误判会让
// 这三个法术永远不施放），只保证传一个可玩战场格。
static int AttackMagicFieldWideHex_(H3CombatManager* cm, int side)
{
    const int index = cm->currentMonIndex;
    if (index >= 0 && index < 21) {
        const H3CombatCreature& stack = cm->stacks[side][index];
        if (AttackMagicPlayableHex_(stack.position)) return stack.position;
        if (stack.info.doubleWide && AttackMagicPlayableHex_(stack.GetSecondSquare()))
            return stack.GetSecondSquare();
    }
    for (int hex = 1; hex < 186; ++hex)
        if (AttackMagicPlayableHex_(hex)) return hex;
    return -1;
}

// 分发按法术类别而非 target_mode：类别已由归一保证与控件一致。
// 范围法术只检查唯一配置落点，不循环、不跳备选、也不退化成随机敌人。
static int AttackMagicSelectHex_(H3CombatManager* cm,
    const H3AutoPolicy::AttackMagicRule& rule, int expertise, int side)
{
    const int kind = H3AutoPolicy::AttackMagicKind(rule.spell_id);
    if (kind == 2) {
        // No coordinate predicate for field-wide magic; skip if every enemy is
        // protected by Dragon Father. The original resolves individual immunity.
        bool any_target = false;
        for (int i = 0; i < 21; ++i) {
            const H3CombatCreature& enemy = cm->stacks[1 - side][i];
            if (enemy.numberAlive > 0
                && !H3AutoPolicy::IsWarMachineType(enemy.type)
                && TargetMagicAllowed_(cm, side, &enemy, rule.spell_id)) {
                any_target = true;
                break;
            }
        }
        return any_target ? AttackMagicFieldWideHex_(cm, side) : -1;
    }
    if (kind == 1) {
        // 范围法术：唯一固定落点。
        if (rule.position_count != 1) return -1;
        const int hex = rule.positions[0];
        return AttackMagicLegalCenter_(cm, rule.spell_id, expertise, side, hex) ? hex : -1;
    }
    if (kind != 0) return -1;
    return AttackMagicSelectEnemyHex_(cm, rule.spell_id, expertise, side,
        rule.target_mode);
}

static bool AttackMagicRelease_(const AttackMagicWindow_& window,
    H3CombatManager* cm, int saved_side)
{
    const bool lifetime_valid = AttackMagicLifetimeValid_(window);
    // Restore the temporary side while this battle is still alive, even if F9,
    // a panel or an action-window change now prevents continuing the old frame.
    if (lifetime_valid && saved_side >= 0) CastSideGuardLeave_(cm, saved_side);
    const bool valid = lifetime_valid && AttackMagicWindowValid_(window);
    g_cast_in_flight = false;
    g_attack_magic_runtime.casting = false;
    return valid;
}

static bool TryAttackMagicCast_(_BattleMgr_* mgr)
{
    if (g_cast_in_flight || g_attack_magic_runtime.casting) return false;
    if (g_phase != BP_COMBAT_CLOSED || g_control != CM_AUTO) return true;
    const int profile = g_active_profile;
    if (profile < 0 || profile >= 5) return true;
    const int count = g_status[profile].attack_count;
    if (count <= 0) return true;
    if (count > H3AutoPolicy::kAttackMagicCapacity) return false;

    AttackMagicWindow_ window = {};
    window.mgr = mgr;
    window.generation = g_attack_magic_runtime.generation;
    window.forcefield_generation = g_forcefield_runtime.generation;
    window.profile = profile;
    window.count = count;
    int saved_side = -1;
    int selected_row = -1;
    int spell = -1;
    int hex = -1;
    H3CombatManager* cm = nullptr;
    __try {
        if (!mgr || o_BattleMgr != mgr) return false;
        if (mgr->auto_combat || IsHiddenBattle(mgr) || IsTacticsPhase_(mgr)) return true;
        window.side = ResolveHumanSide_(mgr);
        if (window.side < 0 || window.side > 1
            || mgr->current_mon_side != window.side || mgr->action != 0) return false;
        window.index = mgr->current_mon_index;
        if (window.index < 0 || window.index >= 21) return false;
        window.current = CurrentTurnStack_(mgr);
        cm = reinterpret_cast<H3CombatManager*>(mgr);
        window.turn = cm->turn;
        if (window.turn < 0 || !AttackMagicWindowValid_(window)) return false;
        if (GetHeroCasted_(mgr, window.side)) return true;
        if (g_attack_magic_runtime.attempt_turn == window.turn
            && g_attack_magic_runtime.attempt_profile == profile) return true;
        H3Hero* hero = cm->hero[window.side];
        if (!hero) return true;
        if (cm->currentActiveSide < 0 || cm->currentActiveSide > 1) return false;

        // CanCastSpellAtCoord forwards currentActiveSide; all queries and casting
        // must use the same temporary side correction as other direct cast channels.
        g_attack_magic_runtime.casting = true;
        g_cast_in_flight = true;
        saved_side = CastSideGuardEnter_(cm, window.side);
        if (saved_side < 0) return AttackMagicRelease_(window, cm, saved_side);
        // bool (8-bit return), hero spell path. No NH3API header is included here.
        if (!THISCALL_3(bool, 0x41FA10, cm, window.side, true))
            return AttackMagicRelease_(window, cm, saved_side);

        for (int row = 0; row < count; ++row) {
            const H3AutoPolicy::AttackMagicRule rule = g_status[profile].attacks[row];
            spell = rule.spell_id;
            if (!AttackMagicAllowedSpell_(spell) || rule.target_mode < -1
                || rule.target_mode > 2 || !HeroMagicAllowed_(cm, window.side, spell)) continue;
            // HasSpell = learnedSpells | availableSpell（+0x3EA/+0x430 位或），
            // 与原版施法面板同口径：装备/卷轴赋予的攻击魔法也参与（2026-10-11
            // 用户裁定）。学校技能等级不构成资格；expertise 仍实算，神器无
            // 技能时为 0，施法走英雄路径由原版内部重算等级与力量。
            const int expertise = hero->GetSpellExpertise(spell, cm->specialTerrain);
            H3Army* enemy_army = cm->army[1 - window.side];
            if (!enemy_army) {
                LogDebug("[AttackMagic] skip missing enemy army spell=%d side=%d", spell, window.side);
                continue; // Do not guess hero->army or an adventure-map nullptr cost.
            }
            const int cost = hero->CalculateSpellCost(spell, enemy_army, cm->specialTerrain);
            const int mana_before = GetHeroMana_(mgr, window.side);
            if (cost < 0 || mana_before < cost) {
                LogDebug("[AttackMagic] skip mana row=%d spell=%d cost=%d mana=%d",
                    row, spell, cost, mana_before);
                continue;
            }
            hex = AttackMagicSelectHex_(cm, rule, expertise, window.side);
            if (hex < 0) {
                LogDebug("[AttackMagic] no target row=%d spell=%d mode=%d positions=%d",
                    row, spell, rule.target_mode, rule.position_count);
                continue;
            }
            if (!AttackMagicWindowValid_(window)
                || !AttackMagicRuleEquals_(g_status[profile].attacks[row], rule)
                || GetHeroCasted_(mgr, window.side)
                || !HeroMagicAllowed_(cm, window.side, spell))
                return AttackMagicRelease_(window, cm, saved_side);
            const int casted_before = GetHeroCasted_(mgr, window.side);
            selected_row = row;
            g_attack_magic_runtime.attempt_turn = window.turn;
            g_attack_magic_runtime.attempt_profile = profile;
            LogInfo("[AttackMagic] submit profile=%d row=%d spell=%d mode=%d hex=%d cost=%d mana=%d",
                profile + 1, row, spell, rule.target_mode, hex, cost, mana_before);
            // Hero path: the original derives expertise/power itself for cast_type 0.
            // One direct call, no shortcut-slot mutation or input injection.
            cm->CastSpell(spell, hex, 0, -1, 0, 3);

            // Animation may pump retry/result/panel callbacks. Only plugin-owned
            // state is read until lifetime has been proven for this same generation.
            if (!AttackMagicWindowValid_(window))
                return AttackMagicRelease_(window, cm, saved_side);
            if (!AttackMagicRuleEquals_(g_status[profile].attacks[row], rule))
                return AttackMagicRelease_(window, cm, saved_side);
            const int mana_after = GetHeroMana_(mgr, window.side);
            const int casted_after = GetHeroCasted_(mgr, window.side);
            const bool submitted = (casted_before == 0 && casted_after != 0)
                || (mana_before >= 0 && mana_after >= 0 && mana_after < mana_before);
            if (submitted) {
                // Only corroborated submission may repair the side-specific mark.
                // VOID returning by itself must not consume the hero's spell slot.
                MarkHeroCastThisTurn_(mgr, window.side);
            }
            LogInfo("[AttackMagic] result row=%d spell=%d hex=%d submitted=%d mana=%d->%d casted=%d->%d (submission is not hit proof)",
                row, spell, hex, submitted ? 1 : 0, mana_before, mana_after,
                casted_before, casted_after);
            return AttackMagicRelease_(window, cm, saved_side);
        }
        return AttackMagicRelease_(window, cm, saved_side);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        const DWORD code = GetExceptionCode();
        // Do not let a fault in stale game memory make the exception handler fault
        // again. Restoring and manual pause are separate narrowly guarded operations.
        bool valid = false;
        __try {
            const bool lifetime_valid = AttackMagicLifetimeValid_(window);
            if (lifetime_valid && saved_side >= 0) CastSideGuardLeave_(cm, saved_side);
            valid = lifetime_valid && AttackMagicWindowValid_(window);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_cast_in_flight = false;
        g_attack_magic_runtime.casting = false;
        LogError("[AttackMagic] exception code=0x%08X row=%d spell=%d hex=%d side=%d",
            code, selected_row, spell, hex, window.side);
        if (AttackMagicLifetimeValid_(window)) {
            __try {
                PauseAutoExecution();
                g_auto_state.action_wake_stack = nullptr;
                if (g_pipeline_stage == PS_SPELL_POSTED) ClearSpellWait_();
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return false;
    }
}
