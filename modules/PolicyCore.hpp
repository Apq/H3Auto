#pragma once

#include <stdint.h>
#include <stddef.h>

// H3Auto 的纯策略核心：不得依赖 H3API、游戏地址、窗口或日志。
// 生产代码和 tests/PolicyCoreTests.cpp 共用这里的规则，避免测试副本与实际逻辑漂移。
namespace H3AutoPolicy {

enum AutoActionKind : uint8_t {
    AA_MANUAL = 0,
    AA_DEFEND,
    AA_WAIT,
    AA_MOVE,
    AA_MELEE_ATTACK,
    AA_RANGED_ATTACK,
    AA_FIRST_AID,
    AA_COUNT
};

enum AutoTargetKind : uint8_t {
    AT_NONE = 0,
    AT_STACK,
    AT_POSITION,
    AT_COUNT
};

enum AutoTargetSide : uint8_t {
    ATS_OWN = 0,
    ATS_ENEMY,
    ATS_EITHER,
    ATS_COUNT
};

enum AutoTargetSelector : uint8_t {
    SEL_RANDOM = 0,
    SEL_RANGED_SPEED,
    SEL_COUNT_HIGH,
    SEL_WOUND_RATIO,
    SEL_WOUND_VALUE,
    SEL_COUNT
};

// H3 内部 creature id。纯核心不引用 H3API 的 eCreature 枚举。
static constexpr int CREATURE_CATAPULT = 0x91;
static constexpr int CREATURE_BALLISTA = 0x92;
static constexpr int CREATURE_FIRST_AID_TENT = 0x93;
static constexpr int CREATURE_AMMO_CART = 0x94;
static constexpr int CREATURE_ARROW_TOWER = 0x95;

static constexpr int MELEE_PAIR_CAPACITY = 10;
static constexpr int MOVE_WAYPOINT_CAPACITY = 16;
static constexpr int SPELL_SLOT_CAPACITY = 10;

inline bool IsQuickSpellDigit(int digit)
{
    return digit == 0 || (digit >= 1 && digit <= 9);
}

struct AutoTargetRule {
    AutoTargetKind kind;
    AutoTargetSide side;
    AutoTargetSelector selector;
    int16_t meleeStandHex;
    int16_t meleeAttackHex;
    int16_t moveWaypoints[MOVE_WAYPOINT_CAPACITY];
    int8_t moveWaypointCount;
    int16_t meleeStandHexes[MELEE_PAIR_CAPACITY];
    int16_t meleeAttackHexes[MELEE_PAIR_CAPACITY];
    int8_t meleePairCount;
};

struct AutoStackRule {
    AutoActionKind action;
    AutoTargetRule target;
    bool allowDefendFallback;
    bool quickCastFirst;
    int8_t spellSlot;
    int8_t spellSlots[SPELL_SLOT_CAPACITY];
    int8_t spellSlotCount;

    // 首动保活（§3.1.1）：挂单支部队，随方案/槽位一起存储与重排。
    uint8_t         protectEnable;       // 0=关闭；1=加入保活队列
    // 按数量保活阈值（PS_COUNT_BELOW）：该队剩余数量 ≤ 此值才救。
    // 每队各自一份，默认 2，范围 0..2147483647。
    int             protectCountBelow;
};

inline AutoStackRule MakeDefaultRule()
{
    AutoStackRule r = {};
    r.action = AA_MANUAL;
    r.target.kind = AT_NONE;
    r.target.side = ATS_ENEMY;
    r.target.selector = SEL_RANDOM;
    r.target.meleeStandHex = -1;
    r.target.meleeAttackHex = -1;
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i)
        r.target.moveWaypoints[i] = -1;
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i) {
        r.target.meleeStandHexes[i] = -1;
        r.target.meleeAttackHexes[i] = -1;
    }
    r.target.moveWaypointCount = 0;
    r.target.meleePairCount = 0;
    r.allowDefendFallback = false;
    r.quickCastFirst = false;
    r.spellSlot = 1;
    for (int i = 0; i < SPELL_SLOT_CAPACITY; ++i)
        r.spellSlots[i] = -1;
    r.spellSlotCount = 0;
    r.protectEnable = 0;
    r.protectCountBelow = 2;
    return r;
}

inline bool IsWarMachineType(int creature_type)
{
    return creature_type == CREATURE_CATAPULT
        || creature_type == CREATURE_BALLISTA
        || creature_type == CREATURE_FIRST_AID_TENT
        || creature_type == CREATURE_AMMO_CART
        || creature_type == CREATURE_ARROW_TOWER;
}

// 配置动作失败时，是否可把当前战争机器交给玩家操作。
// 普通部队始终可交；战争机器须具备对应技能。无技能时由集成层保持
// 原有分支，不在纯策略层推断后续行为。
inline bool CanYieldFailedActionToPlayer(int creature_type,
    bool has_ballistics, bool has_artillery, bool has_first_aid)
{
    switch (creature_type) {
    case CREATURE_CATAPULT:
        return has_ballistics;
    case CREATURE_BALLISTA:
    case CREATURE_ARROW_TOWER:
        return has_artillery;
    case CREATURE_FIRST_AID_TENT:
        return has_first_aid;
    case CREATURE_AMMO_CART:
        return false;
    default:
        return true;
    }
}

enum StableStackIdentityKind : uint8_t {
    STACK_ID_NONE = 0,
    STACK_ID_ARMY_SLOT,
    STACK_ID_WAR_MACHINE,
    // 召唤物/克隆：仅本场内有效，重打/跨场不迁移（remap 视为不可匹配）。
    STACK_ID_SUMMON,
};

struct StableStackIdentity {
    StableStackIdentityKind kind;
    int8_t side;
    int16_t value;
    int8_t occurrence;
};

// Original army stacks keep source_army_slot (0..6) across a quick-battle
// retry. War machines do not have one, so identify them by kind and ordinal.
// Summons and clones only carry a within-battle identity (type + ordinal);
// BuildStableStackSlotRemap refuses to carry their rules across a retry.
inline StableStackIdentity MakeStableStackIdentity(int side,
    int source_army_slot, int creature_type, int occurrence)
{
    StableStackIdentity id = {};
    id.side = static_cast<int8_t>(side);
    if (side < 0 || side > 1) return id;
    if (source_army_slot >= 0 && source_army_slot < 7) {
        id.kind = STACK_ID_ARMY_SLOT;
        id.value = static_cast<int16_t>(source_army_slot);
        return id;
    }
    if (IsWarMachineType(creature_type)) {
        id.kind = STACK_ID_WAR_MACHINE;
        id.value = static_cast<int16_t>(creature_type);
        id.occurrence = static_cast<int8_t>(occurrence < 0 ? 0 : occurrence);
        return id;
    }
    if (creature_type >= 0) {
        id.kind = STACK_ID_SUMMON;
        id.value = static_cast<int16_t>(creature_type);
        id.occurrence = static_cast<int8_t>(occurrence < 0 ? 0 : occurrence);
    }
    return id;
}

inline bool StableStackIdentityEquals(const StableStackIdentity& a,
    const StableStackIdentity& b)
{
    if (a.kind == STACK_ID_NONE || b.kind == STACK_ID_NONE) return false;
    return a.kind == b.kind && a.side == b.side && a.value == b.value
        && a.occurrence == b.occurrence;
}

// For each current battle slot, return the previous slot holding its rule.
// Unmatched slots remain -1 and receive defaults in the integration layer.
// 召唤物/克隆没有跨重打身份：不参与重排，重打后回到默认规则。
inline void BuildStableStackSlotRemap(const StableStackIdentity* previous,
    int previous_count, const StableStackIdentity* current, int current_count,
    int* previous_slot_for_current)
{
    if (!previous_slot_for_current || current_count <= 0) return;
    for (int i = 0; i < current_count; ++i) {
        previous_slot_for_current[i] = -1;
        if (!current || current[i].kind == STACK_ID_NONE) continue;
        if (current[i].kind == STACK_ID_SUMMON) continue;
        for (int j = 0; previous && j < previous_count; ++j) {
            if (previous[j].kind == STACK_ID_SUMMON) continue;
            if (StableStackIdentityEquals(previous[j], current[i])) {
                previous_slot_for_current[i] = j;
                break;
            }
        }
    }
}

// 面板准入纯规则：数量大于 0；弹药车永不进入；投石车必须有弹道术。
inline bool IsConfigurablePanelStack(int creature_type, int number_alive,
    bool has_ballistics)
{
    if (number_alive <= 0) return false;
    if (creature_type == CREATURE_AMMO_CART) return false;
    if (creature_type == CREATURE_CATAPULT) return has_ballistics;
    return true;
}

enum ResultLifecycleEvent : uint8_t {
    RESULT_NONE = 0,
    RESULT_SHOWN,
    RESULT_ACCEPT_CLICKED,
    RESULT_CANCEL_CLICKED,
    RESULT_CLOSED_WITH_BATTLE_UI,
    RESULT_CLOSED_WITHOUT_BATTLE_UI,
};

enum ResultLifecycleAction : uint8_t {
    RESULT_WAIT = 0,
    RESULT_KEEP_AND_REBIND,
    RESULT_CLEAR_SETTINGS,
};

struct ResultLifecycleState {
    bool saw_result;
    bool accept_armed;
    bool cancel_armed;
};

// 结果窗状态机纯函数：取消/重打只保留并重绑，接受才清除。
inline ResultLifecycleAction ApplyResultLifecycle(
    ResultLifecycleState* state, ResultLifecycleEvent event)
{
    if (!state) return RESULT_WAIT;
    if (event == RESULT_SHOWN) {
        state->saw_result = true;
        state->accept_armed = false;
        state->cancel_armed = false;
        return RESULT_WAIT;
    }
    if (!state->saw_result) return RESULT_WAIT;
    if (event == RESULT_ACCEPT_CLICKED) {
        state->accept_armed = true;
        state->cancel_armed = false;
        return RESULT_WAIT;
    }
    if (event == RESULT_CANCEL_CLICKED) {
        state->cancel_armed = true;
        state->accept_armed = false;
        return RESULT_WAIT;
    }
    if (event == RESULT_CLOSED_WITH_BATTLE_UI && state->accept_armed) {
        *state = {};
        return RESULT_CLEAR_SETTINGS;
    }
    // Returning BattleUI is the authoritative retry signal when accept was
    // not explicitly captured. Do not depend on the exact cancel mouse frame.
    if (event == RESULT_CLOSED_WITH_BATTLE_UI) {
        *state = {};
        return RESULT_KEEP_AND_REBIND;
    }
    // Cancel can close CPResult one or more frames before BattleUI reappears.
    // Keep waiting instead of clearing profiles during that transition.
    if (event == RESULT_CLOSED_WITHOUT_BATTLE_UI && state->cancel_armed)
        return RESULT_WAIT;
    if (event == RESULT_CLOSED_WITHOUT_BATTLE_UI) {
        *state = {};
        return RESULT_CLEAR_SETTINGS;
    }
    return RESULT_WAIT;
}

inline int GetAllowedActions(int creature_type, bool is_ranged,
    bool has_artillery, bool has_first_aid, AutoActionKind out_actions[AA_COUNT])
{
    int n = 0;
    auto push = [&](AutoActionKind action) {
        if (n < AA_COUNT) out_actions[n++] = action;
    };

    switch (creature_type) {
    case CREATURE_FIRST_AID_TENT:
        if (has_first_aid) push(AA_MANUAL);
        push(AA_FIRST_AID);
        break;
    case CREATURE_CATAPULT:
        push(AA_MANUAL);
        push(AA_DEFEND);
        break;
    case CREATURE_BALLISTA:
    case CREATURE_ARROW_TOWER:
        if (has_artillery) push(AA_MANUAL);
        push(AA_RANGED_ATTACK);
        break;
    case CREATURE_AMMO_CART:
        push(AA_MANUAL);
        break;
    default:
        push(AA_MANUAL);
        push(AA_DEFEND);
        push(AA_MOVE);
        push(AA_MELEE_ATTACK);
        if (is_ranged) push(AA_RANGED_ATTACK);
        break;
    }
    return n;
}

inline bool ActionNeedsTarget(AutoActionKind action)
{
    return action == AA_MOVE || action == AA_MELEE_ATTACK
        || action == AA_RANGED_ATTACK || action == AA_FIRST_AID;
}

inline bool ActionUsesTwoHex(AutoActionKind action)
{
    return action == AA_MELEE_ATTACK;
}

inline bool ActionShowsFallback(int creature_type, AutoActionKind action)
{
    if (IsWarMachineType(creature_type)) return false;
    return action != AA_MANUAL && action != AA_DEFEND && action != AA_WAIT;
}

inline AutoTargetRule DefaultTargetForAction(AutoActionKind action)
{
    AutoTargetRule t = {};
    t.meleeStandHex = -1;
    t.meleeAttackHex = -1;
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i)
        t.moveWaypoints[i] = -1;
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i) {
        t.meleeStandHexes[i] = -1;
        t.meleeAttackHexes[i] = -1;
    }

    switch (action) {
    case AA_MOVE:
    case AA_MELEE_ATTACK:
        t.kind = AT_POSITION;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        break;
    case AA_RANGED_ATTACK:
        t.kind = AT_STACK;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        break;
    case AA_FIRST_AID:
        t.kind = AT_STACK;
        t.side = ATS_OWN;
        t.selector = SEL_WOUND_VALUE;
        break;
    default:
        t.kind = AT_NONE;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        break;
    }
    return t;
}

struct TargetCandidate {
    int count_current;
    int count_at_start;
    int hit_points;
    int lost_hp;
    int shots;
    int speed;
    int flyer;
    int ranged; // 生物类型固有远程标志（H3CreatureInformation.shooter），与当前弹药无关
};

inline int StackRemainingHp(const TargetCandidate& candidate)
{
    const int hp = candidate.hit_points > 0 ? candidate.hit_points : 1;
    const int alive = candidate.count_current > 0 ? candidate.count_current : 0;
    int64_t total = static_cast<int64_t>(alive) * hp;
    const int lost = candidate.lost_hp > 0 ? candidate.lost_hp : 0;
    if (lost > 0 && total > lost) total -= lost;
    else if (lost > 0) total = 0;
    if (total > 0x7FFFFFFF) total = 0x7FFFFFFF;
    return static_cast<int>(total);
}

// 首动保活（§3.1.1）纯判定逻辑。
// 可恢复量：复活/聚灵按法术等级 基础 50×力量 / 高级 75×力量 / 专家 100×力量。
inline int ResurrectionRestoreHp(int expertise, int spell_power)
{
    if (spell_power <= 0) return 0;
    int base = 0;
    if (expertise == 3) base = 100;
    else if (expertise == 2) base = 75;
    else if (expertise == 1) base = 50;
    else return 0;
    return base * spell_power;
}

// 保活策略（方案级）：整个方案的保活触发方式；默认 0=无。
enum ProtectStrategy : uint8_t {
    PS_NONE = 0,          // 无：不保活
    PS_COUNT_BELOW,       // 按数量：剩余数量 ≤ 该队阈值才救（阈值随规则存储，默认 2；0=只救全灭）
    PS_FIRST_ACTION,      // 回合内首动：队列中有损失的部队即救
    PS_LOSS_GT_RESTORE,   // 损失量大于恢复量：已损 HP 超过一次可恢复量才救
    PS_COUNT
};

// 是否够格：未入队不触发；无损失不触发；按方案策略判定。
// PS_COUNT_BELOW 要求剩余数量 ≤ 该队阈值（全灭数量 0 天然满足，0=只救全灭）；
// PS_FIRST_ACTION 有损失即救；
// PS_LOSS_GT_RESTORE 要求已损 HP 严格大于一次可恢复量。
inline bool ProtectShouldCast(bool enabled, ProtectStrategy strategy,
    int restorable_hp, int wound_value,
    int count_current, int count_below)
{
    if (!enabled) return false;
    if (wound_value <= 0) return false;
    switch (strategy) {
    case PS_FIRST_ACTION:    return true;
    case PS_LOSS_GT_RESTORE: return restorable_hp > 0
        && wound_value > restorable_hp;
    case PS_COUNT_BELOW:     return count_current <= count_below;
    default:                 return false; // PS_NONE
    }
}

inline int WoundValue(const TargetCandidate& candidate)
{
    const int hp = candidate.hit_points > 0 ? candidate.hit_points : 1;
    const int dead = candidate.count_at_start > candidate.count_current
        ? candidate.count_at_start - candidate.count_current : 0;
    const int lost = candidate.lost_hp > 0 ? candidate.lost_hp : 0;
    return dead * hp + lost;
}

inline int WoundRatioKey(const TargetCandidate& candidate)
{
    const int hp = candidate.hit_points > 0 ? candidate.hit_points : 1;
    const int start = candidate.count_at_start > 0
        ? candidate.count_at_start : candidate.count_current;
    const int total = start * hp;
    if (total <= 0) return 0;
    return static_cast<int>((static_cast<int64_t>(WoundValue(candidate)) * 10000) / total);
}

// 够格者中选目标下标：血量最低（全灭者剩余 0 天然最前）；
// 平分保留先出现者。返回 -1 = 无合适目标。
inline int SelectProtectTargetIndex(const TargetCandidate* candidates, int count)
{
    if (!candidates || count <= 0) return -1;
    int best = 0;
    for (int i = 1; i < count; ++i)
        if (StackRemainingHp(candidates[i]) < StackRemainingHp(candidates[best]))
            best = i;
    return best;
}

// 自动停止（§3.1.2）。按本场掉血外推敌方还要几回合全灭。
// 返回值 < 0 表示现在不能停：阈值为 0、还没有基线、没掉过血。
// 否则返回预计剩余回合（向上取整）。
inline int ProjectEnemyTurnsLeft(int threshold, int baseline_hp,
    int current_hp, int elapsed_turns)
{
    if (threshold <= 0) return -1;
    if (baseline_hp <= 0 || current_hp <= 0 || elapsed_turns <= 0) return -1;
    const int damage = baseline_hp - current_hp;
    if (damage <= 0) return -1;
    const int64_t product = static_cast<int64_t>(current_hp) * elapsed_turns;
    return static_cast<int>((product + damage - 1) / damage);
}

inline bool AutoStopShouldYield(int threshold, int baseline_hp,
    int current_hp, int elapsed_turns)
{
    const int left = ProjectEnemyTurnsLeft(threshold, baseline_hp,
        current_hp, elapsed_turns);
    return left >= 0 && left <= threshold;
}

// 返回候选下标；平分时保留先出现者，与原执行器行为一致。
// random_value 由生产侧传入 rand()，测试侧可传固定值。
inline int SelectTargetIndex(const TargetCandidate* candidates, int count,
    AutoTargetSelector selector, uint32_t random_value = 0)
{
    if (!candidates || count <= 0) return -1;
    if (selector == SEL_RANDOM)
        return static_cast<int>(random_value % static_cast<uint32_t>(count));

    int best = 0;
    for (int i = 1; i < count; ++i) {
        bool better = false;
        switch (selector) {
        case SEL_COUNT_HIGH:
            better = candidates[i].count_current > candidates[best].count_current;
            break;
        case SEL_RANGED_SPEED: {
            // 远程按生物类型固有标志，不用当前弹药（shots 弹药打光即归零）。
            const int current_ranged = candidates[i].ranged > 0 ? 1 : 0;
            const int best_ranged = candidates[best].ranged > 0 ? 1 : 0;
            const int current_flyer = candidates[i].flyer > 0 ? 1 : 0;
            const int best_flyer = candidates[best].flyer > 0 ? 1 : 0;
            // 远程 > 飞行 > 速度；同类同速时优先打剩余总血量更高的那队。
            better = current_ranged > best_ranged
                || (current_ranged == best_ranged && current_flyer > best_flyer)
                || (current_ranged == best_ranged && current_flyer == best_flyer
                    && candidates[i].speed > candidates[best].speed)
                || (current_ranged == best_ranged && current_flyer == best_flyer
                    && candidates[i].speed == candidates[best].speed
                    && StackRemainingHp(candidates[i]) > StackRemainingHp(candidates[best]));
            break;
        }
        case SEL_WOUND_VALUE:
            better = WoundValue(candidates[i]) > WoundValue(candidates[best]);
            break;
        case SEL_WOUND_RATIO:
            better = WoundRatioKey(candidates[i]) > WoundRatioKey(candidates[best]);
            break;
        default:
            return static_cast<int>(random_value % static_cast<uint32_t>(count));
        }
        if (better) best = i;
    }
    return best;
}

// 方案存档（加载/保存按钮）：每个编号一个独立文件（H3Auto.profilesN.ini）。
// 纯编解码：一行文本 "H3AP5 <21×部队表> <策略> <停止回合> <21×规则>"，
// 规则按槽位排列，每条 60 个十进制整数（H3AP4 及更早一律拒绝）。
// 部队表在头部：每槽 2 个整数（生物类型、数量），空槽写 -1 0。
// 部队表供读档时做四轮关联（存档部队 ↔ 当前部队），规则本体仍不含身份。
// 文本在部队表之后有 1 个策略、1 个自动停止回合（0..999）。
// 存档格式：21*2 部队 + 1 策略 + 1 停止回合 + 21 条规则（每条 60 个整数）。
static constexpr int PROFILE_STORE_SLOTS = 21;
// 每条规则的整数字段数必须与 EncodeRuleInts/DecodeRuleInts 的写入数一致
// （曾因手写 34 与实写 59 脱节导致越界写堆 = 保存后崩溃的根因）。
// 用表达式自校验：6 头 + 16 航点 + 1 计数 + 2*10 近战对 + 1 计数 + 3 杂项
// + 10 施法槽 + 3 尾（+保活数量阈值）= 60。
static constexpr int PROFILE_STORE_RULE_FIELDS =
    6 + MOVE_WAYPOINT_CAPACITY + 1 + 2 * MELEE_PAIR_CAPACITY + 1 + 3
    + SPELL_SLOT_CAPACITY + 3;
// 旧档（3575/3580 整数）由越界写堆的坏版本写出：交错覆盖、不可靠且
// 按新步进读会越界读，一律拒绝（读档失败，需重新配置）。
static constexpr int PROFILE_STORE_LEGACY_INTS = 3575;
// H3AP1（6205 整数，无部队表）：格式已废弃，同样拒绝。
static constexpr int PROFILE_STORE_ARMY_INTS =
    PROFILE_STORE_SLOTS * 2;
static constexpr int PROFILE_STORE_INTS =
    PROFILE_STORE_ARMY_INTS
    + 1 + 1
    + PROFILE_STORE_SLOTS * PROFILE_STORE_RULE_FIELDS;
static constexpr int DEFAULT_STOP_TURNS = 10;

// 读档四轮关联（存档部队 → 当前部队槽）：
//   轮 1：槽位 + 生物类型 + 初始数量（三项全等，零误配）
//   轮 2：生物类型 + 初始数量（换了槽、规模没变；数量相等天然消歧）
//   轮 3：槽位 + 生物类型（没换槽、规模变了；同类型多组时降级跳过）
//   轮 4：只按生物类型（兜底，先到先得）
// 每轮跳过两边已被匹配的槽位；arch_for_cur[cur] = 存档槽号，-1 = 未匹配。
// 空槽（类型 < 0）不参与。未匹配的存档规则由调用方丢弃。
inline void BuildArchiveSlotMapByRounds(const int* arch_type,
    const int* arch_count, const int* cur_type, const int* cur_count,
    int* arch_for_cur)
{
    if (!arch_type || !arch_count || !cur_type || !cur_count || !arch_for_cur)
        return;
    bool cur_used[PROFILE_STORE_SLOTS] = {};
    bool arch_used[PROFILE_STORE_SLOTS] = {};
    for (int i = 0; i < PROFILE_STORE_SLOTS; ++i)
        arch_for_cur[i] = -1;

    // 某类型在数组中的出现次数（仅有效槽）。O(21^2)，规模固定无所谓。
    auto count_type = [](const int* types, int t) -> int {
        int n = 0;
        for (int i = 0; i < PROFILE_STORE_SLOTS; ++i)
            if (types[i] == t) ++n;
        return n;
    };

    // 轮 1：槽位 + 生物类型 + 初始数量。
    for (int i = 0; i < PROFILE_STORE_SLOTS; ++i) {
        if (cur_used[i] || arch_used[i]) continue;
        if (cur_type[i] < 0 || arch_type[i] < 0) continue;
        if (cur_type[i] == arch_type[i]
            && cur_count[i] == arch_count[i]) {
            arch_for_cur[i] = i;
            cur_used[i] = true;
            arch_used[i] = true;
        }
    }
    // 轮 2：生物类型 + 初始数量（当前槽升序，存档槽取第一个命中）。
    for (int c = 0; c < PROFILE_STORE_SLOTS; ++c) {
        if (cur_used[c] || cur_type[c] < 0) continue;
        for (int a = 0; a < PROFILE_STORE_SLOTS; ++a) {
            if (arch_used[a] || arch_type[a] < 0) continue;
            if (arch_type[a] == cur_type[c]
                && arch_count[a] == cur_count[c]) {
                arch_for_cur[c] = a;
                cur_used[c] = true;
                arch_used[a] = true;
                break;
            }
        }
    }
    // 轮 3：槽位 + 生物类型。同类型在任一边出现多组时降级跳过
    // （此时槽位是仅剩信号但不可靠，交给轮 4 按类型先到先得）。
    for (int i = 0; i < PROFILE_STORE_SLOTS; ++i) {
        if (cur_used[i] || arch_used[i]) continue;
        if (cur_type[i] < 0 || arch_type[i] < 0) continue;
        if (cur_type[i] != arch_type[i]) continue;
        if (count_type(cur_type, cur_type[i]) > 1
            || count_type(arch_type, arch_type[i]) > 1)
            continue;
        arch_for_cur[i] = i;
        cur_used[i] = true;
        arch_used[i] = true;
    }
    // 轮 4：只按生物类型（先到先得）。
    for (int c = 0; c < PROFILE_STORE_SLOTS; ++c) {
        if (cur_used[c] || cur_type[c] < 0) continue;
        for (int a = 0; a < PROFILE_STORE_SLOTS; ++a) {
            if (arch_used[a] || arch_type[a] < 0) continue;
            if (arch_type[a] == cur_type[c]) {
                arch_for_cur[c] = a;
                cur_used[c] = true;
                arch_used[a] = true;
                break;
            }
        }
    }
}

inline void EncodeRuleInts(const AutoStackRule& rule, int* out)
{
    int n = 0;
    out[n++] = static_cast<int>(rule.action);
    out[n++] = static_cast<int>(rule.target.kind);
    out[n++] = static_cast<int>(rule.target.side);
    out[n++] = static_cast<int>(rule.target.selector);
    out[n++] = rule.target.meleeStandHex;
    out[n++] = rule.target.meleeAttackHex;
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i)
        out[n++] = rule.target.moveWaypoints[i];
    out[n++] = rule.target.moveWaypointCount;
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i)
        out[n++] = rule.target.meleeStandHexes[i];
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i)
        out[n++] = rule.target.meleeAttackHexes[i];
    out[n++] = rule.target.meleePairCount;
    out[n++] = rule.allowDefendFallback ? 1 : 0;
    out[n++] = rule.quickCastFirst ? 1 : 0;
    out[n++] = rule.spellSlot;
    for (int i = 0; i < SPELL_SLOT_CAPACITY; ++i)
        out[n++] = rule.spellSlots[i];
    out[n++] = rule.spellSlotCount;
    out[n++] = rule.protectEnable ? 1 : 0;
    out[n++] = rule.protectCountBelow;
}

inline bool DecodeRuleInts(const int* in, AutoStackRule* rule)
{
    if (!in || !rule) return false;
    AutoStackRule r = MakeDefaultRule();
    int n = 0;
    r.action = static_cast<AutoActionKind>(in[n++]);
    r.target.kind = static_cast<AutoTargetKind>(in[n++]);
    r.target.side = static_cast<AutoTargetSide>(in[n++]);
    r.target.selector = static_cast<AutoTargetSelector>(in[n++]);
    r.target.meleeStandHex = static_cast<int16_t>(in[n++]);
    r.target.meleeAttackHex = static_cast<int16_t>(in[n++]);
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i)
        r.target.moveWaypoints[i] = static_cast<int16_t>(in[n++]);
    r.target.moveWaypointCount = static_cast<int8_t>(in[n++]);
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i)
        r.target.meleeStandHexes[i] = static_cast<int16_t>(in[n++]);
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i)
        r.target.meleeAttackHexes[i] = static_cast<int16_t>(in[n++]);
    r.target.meleePairCount = static_cast<int8_t>(in[n++]);
    r.allowDefendFallback = in[n++] != 0;
    r.quickCastFirst = in[n++] != 0;
    r.spellSlot = static_cast<int8_t>(in[n++]);
    for (int i = 0; i < SPELL_SLOT_CAPACITY; ++i)
        r.spellSlots[i] = static_cast<int8_t>(in[n++]);
    r.spellSlotCount = static_cast<int8_t>(in[n++]);
    r.protectEnable = in[n++] != 0 ? 1 : 0;
    r.protectCountBelow = in[n++];
    if (r.protectCountBelow < 0) r.protectCountBelow = 0;
    if (n != PROFILE_STORE_RULE_FIELDS) return false;
    if (r.action < AA_MANUAL || r.action >= AA_COUNT) return false;
    if (r.target.kind < AT_NONE || r.target.kind >= AT_COUNT) return false;
    if (r.target.side < ATS_OWN || r.target.side >= ATS_COUNT) return false;
    if (r.target.selector < SEL_RANDOM || r.target.selector >= SEL_COUNT) return false;
    *rule = r;
    return true;
}

// 文本 ↔ 整数数组。Encode 返回写入字符数（不含结尾 0），缓冲不足返回 -1。
// Decode 只接受以 "H3AP5 " 开头且整数个数恰好为 PROFILE_STORE_INTS 的文本。
inline int EncodeProfileStoreText(const int army_types[PROFILE_STORE_SLOTS],
    const int army_counts[PROFILE_STORE_SLOTS],
    uint8_t strategy,
    const AutoStackRule rules[PROFILE_STORE_SLOTS],
    uint16_t stop_turns,
    char* buffer, int buffer_size)
{
    if (!army_types || !army_counts || !rules
        || !buffer || buffer_size <= 0)
        return -1;
    int written = 0;
    auto append = [&](const char* s) -> bool {
        for (int i = 0; s[i]; ++i) {
            if (written + 1 >= buffer_size) return false;
            buffer[written++] = s[i];
        }
        return true;
    };
    if (!append("H3AP5")) return -1;
    int* ints = new int[PROFILE_STORE_INTS];
    int n = 0;
    for (int s = 0; s < PROFILE_STORE_SLOTS; ++s) {
        ints[n++] = army_types[s];
        ints[n++] = army_counts[s];
    }
    ints[n++] = strategy;
    int turns = stop_turns;
    if (turns < 0) turns = 0;
    if (turns > 999) turns = 999;
    ints[n++] = turns;
    bool ok = true;
    for (int s = 0; ok && s < PROFILE_STORE_SLOTS; ++s) {
        __try {
            EncodeRuleInts(rules[s], ints + n);
        } __except (1) {
            delete[] ints;
            return -100000 - s;
        }
        n += PROFILE_STORE_RULE_FIELDS;
    }
    for (int i = 0; ok && i < n; ++i) {
        char num[16];
        int v = ints[i];
        int len = 0;
        if (v < 0) { num[len++] = '-'; v = -v; }
        char digits[12];
        int dlen = 0;
        do { digits[dlen++] = static_cast<char>('0' + v % 10); v /= 10; }
        while (v > 0);
        while (dlen > 0) num[len++] = digits[--dlen];
        num[len] = 0;
        if (!append(" ") || !append(num)) ok = false;
    }
    delete[] ints;
    if (!ok) return -1;
    buffer[written] = 0;
    return written;
}

inline bool DecodeProfileStoreText(const char* text,
    int army_types[PROFILE_STORE_SLOTS],
    int army_counts[PROFILE_STORE_SLOTS],
    uint8_t* strategy,
    AutoStackRule rules[PROFILE_STORE_SLOTS],
    uint16_t* stop_turns)
{
    if (!text || !army_types || !army_counts || !strategy || !rules
        || !stop_turns) return false;
    const char* magic = "H3AP5";
    for (int i = 0; magic[i]; ++i)
        if (text[i] != magic[i]) return false;
    const char* p = text + 5;

    int* ints = new int[PROFILE_STORE_INTS];
    int count = 0;
    bool bad = false;
    while (*p && !bad) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (!*p) break;
        if (count >= PROFILE_STORE_INTS) { bad = true; break; }
        int sign = 1;
        if (*p == '-') { sign = -1; ++p; }
        if (*p < '0' || *p > '9') { bad = true; break; }
        int v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            ++p;
        }
        ints[count++] = sign * v;
    }
    if (bad || count != PROFILE_STORE_INTS) {
        // 含旧格式（H3AP2 6247 整数 / 3575/3580 交错坏档），一律拒绝。
        delete[] ints;
        return false;
    }

    uint8_t decoded_strategy = 0;
    uint16_t decoded_stop = 0;
    int decoded_army_types[PROFILE_STORE_SLOTS] = {};
    int decoded_army_counts[PROFILE_STORE_SLOTS] = {};
    AutoStackRule decoded[PROFILE_STORE_SLOTS] = {};
    int n = 0;
    for (int s = 0; s < PROFILE_STORE_SLOTS; ++s) {
        decoded_army_types[s] = ints[n];
        decoded_army_counts[s] = ints[n + 1];
        if (ints[n] < -1 || ints[n + 1] < 0) { delete[] ints; return false; }
        n += 2;
    }
    if (ints[n] < PS_NONE || ints[n] >= PS_COUNT) { delete[] ints; return false; }
    decoded_strategy = static_cast<uint8_t>(ints[n++]);
    if (ints[n] < 0 || ints[n] > 999) { delete[] ints; return false; }
    decoded_stop = static_cast<uint16_t>(ints[n++]);
    for (int s = 0; s < PROFILE_STORE_SLOTS; ++s) {
        if (!DecodeRuleInts(ints + n, &decoded[s])) { delete[] ints; return false; }
        n += PROFILE_STORE_RULE_FIELDS;
    }
    delete[] ints;
    for (int s = 0; s < PROFILE_STORE_SLOTS; ++s) {
        army_types[s] = decoded_army_types[s];
        army_counts[s] = decoded_army_counts[s];
        rules[s] = decoded[s];
    }
    *strategy = decoded_strategy;
    *stop_turns = decoded_stop;
    return true;
}

inline int GetAllowedSelectors(AutoActionKind action, AutoTargetKind /*kind*/,
    AutoTargetSelector out_selectors[SEL_COUNT])
{
    int n = 0;
    auto push = [&](AutoTargetSelector selector) {
        if (n < SEL_COUNT) out_selectors[n++] = selector;
    };
    if (ActionUsesTwoHex(action)) return 0;
    if (action == AA_FIRST_AID) {
        push(SEL_WOUND_VALUE);
        push(SEL_WOUND_RATIO);
        push(SEL_RANDOM);
    } else {
        push(SEL_RANDOM);
        push(SEL_RANGED_SPEED);
        push(SEL_COUNT_HIGH);
    }
    return n;
}

inline void NormalizeSpellSlots(AutoStackRule* rule)
{
    if (!rule) return;
    auto valid = [](int slot) {
        return slot == 0 || (slot >= 1 && slot <= 9);
    };

    int8_t compact[SPELL_SLOT_CAPACITY] = {};
    int count = 0;
    int requested = rule->spellSlotCount;
    if (requested < 0) requested = 0;
    if (requested > SPELL_SLOT_CAPACITY) requested = SPELL_SLOT_CAPACITY;
    for (int i = 0; i < requested; ++i) {
        const int slot = rule->spellSlots[i];
        if (valid(slot)) compact[count++] = static_cast<int8_t>(slot);
    }
    if (count == 0 && rule->quickCastFirst && valid(rule->spellSlot)) {
        compact[0] = rule->spellSlot;
        count = 1;
    }
    for (int i = 0; i < SPELL_SLOT_CAPACITY; ++i)
        rule->spellSlots[i] = i < count ? compact[i] : static_cast<int8_t>(-1);
    rule->spellSlotCount = static_cast<int8_t>(count);
    rule->quickCastFirst = count > 0;
    rule->spellSlot = count > 0 ? rule->spellSlots[0] : 1;
}

// 删除一个已有施法槽并压紧。删除最后一项时清除旧版兼容镜像，
// 防止 NormalizeSpellSlots 把旧 quickCastFirst + spellSlot 重新迁移回来。
inline bool RemoveSpellSlot(AutoStackRule* rule, int index)
{
    if (!rule || index < 0 || index >= rule->spellSlotCount) return false;
    for (int i = index; i + 1 < rule->spellSlotCount; ++i)
        rule->spellSlots[i] = rule->spellSlots[i + 1];
    rule->spellSlots[rule->spellSlotCount - 1] = -1;
    --rule->spellSlotCount;
    if (rule->spellSlotCount == 0)
        rule->quickCastFirst = false;
    NormalizeSpellSlots(rule);
    return true;
}

// 纯策略部分的规范化；近战槽位/移动坐标的几何校验仍由 CellControl 负责。
inline void NormalizeRule(AutoStackRule* rule, int creature_type, bool is_ranged,
    bool has_artillery, bool has_first_aid)
{
    if (!rule) return;
    if (rule->protectCountBelow < 0) rule->protectCountBelow = 0;

    AutoActionKind allowed[AA_COUNT] = {};
    const int n = GetAllowedActions(creature_type, is_ranged,
        has_artillery, has_first_aid, allowed);
    bool action_ok = false;
    for (int i = 0; i < n; ++i)
        if (allowed[i] == rule->action) action_ok = true;
    if (!action_ok) rule->action = n > 0 ? allowed[0] : AA_MANUAL;

    NormalizeSpellSlots(rule);
    if (!ActionNeedsTarget(rule->action)) {
        rule->target = DefaultTargetForAction(AA_MANUAL);
        rule->allowDefendFallback = false;
        return;
    }

    if (rule->target.kind == AT_NONE)
        rule->target = DefaultTargetForAction(rule->action);
    switch (rule->action) {
    case AA_MELEE_ATTACK:
        rule->target.kind = AT_POSITION;
        rule->target.side = ATS_ENEMY;
        rule->target.selector = SEL_RANDOM;
        break;
    case AA_RANGED_ATTACK:
        rule->target.kind = AT_STACK;
        rule->target.side = ATS_ENEMY;
        break;
    case AA_FIRST_AID:
        rule->target.kind = AT_STACK;
        rule->target.side = ATS_OWN;
        break;
    case AA_MOVE:
        rule->target.kind = AT_POSITION;
        rule->target.selector = SEL_RANDOM;
        break;
    default:
        break;
    }

    if (!ActionUsesTwoHex(rule->action)) {
        AutoTargetSelector selectors[SEL_COUNT] = {};
        const int selector_count = GetAllowedSelectors(rule->action,
            rule->target.kind, selectors);
        bool selector_ok = false;
        for (int i = 0; i < selector_count; ++i)
            if (selectors[i] == rule->target.selector) selector_ok = true;
        if (!selector_ok && selector_count > 0)
            rule->target.selector = selectors[0];
    }
    if (!ActionShowsFallback(creature_type, rule->action))
        rule->allowDefendFallback = false;
}

} // namespace H3AutoPolicy

// 保持现有生产代码的未限定名称，避免一次性改动全部模块。
using H3AutoPolicy::AutoActionKind;
using H3AutoPolicy::AutoTargetKind;
using H3AutoPolicy::AutoTargetSide;
using H3AutoPolicy::AutoTargetSelector;
using H3AutoPolicy::AutoTargetRule;
using H3AutoPolicy::AutoStackRule;
using H3AutoPolicy::AA_MANUAL;
using H3AutoPolicy::AA_DEFEND;
using H3AutoPolicy::AA_WAIT;
using H3AutoPolicy::AA_MOVE;
using H3AutoPolicy::AA_MELEE_ATTACK;
using H3AutoPolicy::AA_RANGED_ATTACK;
using H3AutoPolicy::AA_FIRST_AID;
using H3AutoPolicy::AA_COUNT;
using H3AutoPolicy::AT_NONE;
using H3AutoPolicy::AT_STACK;
using H3AutoPolicy::AT_POSITION;
using H3AutoPolicy::ATS_OWN;
using H3AutoPolicy::ATS_ENEMY;
using H3AutoPolicy::ATS_EITHER;
using H3AutoPolicy::SEL_RANDOM;
using H3AutoPolicy::SEL_RANGED_SPEED;
using H3AutoPolicy::SEL_COUNT_HIGH;
using H3AutoPolicy::SEL_WOUND_RATIO;
using H3AutoPolicy::SEL_WOUND_VALUE;
using H3AutoPolicy::SEL_COUNT;
using H3AutoPolicy::MELEE_PAIR_CAPACITY;
using H3AutoPolicy::MOVE_WAYPOINT_CAPACITY;
using H3AutoPolicy::SPELL_SLOT_CAPACITY;
