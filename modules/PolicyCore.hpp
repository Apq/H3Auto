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
    if (event == RESULT_CLOSED_WITH_BATTLE_UI && state->cancel_armed) {
        *state = {};
        return RESULT_KEEP_AND_REBIND;
    }
    if (event == RESULT_CLOSED_WITHOUT_BATTLE_UI
        || (event == RESULT_CLOSED_WITH_BATTLE_UI && state->accept_armed)) {
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
};

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
            const int current_ranged = candidates[i].shots > 0 ? 1 : 0;
            const int best_ranged = candidates[best].shots > 0 ? 1 : 0;
            better = current_ranged > best_ranged
                || (current_ranged == best_ranged
                    && candidates[i].speed > candidates[best].speed);
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

// 纯策略部分的规范化；近战槽位/移动坐标的几何校验仍由 CellControl 负责。
inline void NormalizeRule(AutoStackRule* rule, int creature_type, bool is_ranged,
    bool has_artillery, bool has_first_aid)
{
    if (!rule) return;
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
