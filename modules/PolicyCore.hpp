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
    // 召唤物共享规则专用（普通部队行动下拉不出现）：
    AA_SCATTER,       // 散开：与己方其它部队分级拉开距离（≥2 格 → ≥1 格 → 原地防御）
    AA_RANDOM_MOVE,   // 随机移动：可达格均匀随机
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

// ---- 召唤通道常量（保活通道 = 兵力不足时召唤）----
static constexpr int SUMMON_ELEMENT_COUNT = 4;   // 固定顺序：气/水/火/土（与法术下拉一致）
static constexpr int kSummonSpellIds[SUMMON_ELEMENT_COUNT] = {69, 68, 66, 67};   // 气/水/火/土
static constexpr int kSummonCreatureIds[SUMMON_ELEMENT_COUNT] = {112, 115, 114, 113};
// 自动停止第二条件（敌方魔力停手）默认阈值：约「最多再放一次」低阶法术的量。
// 随方案可调（0..32767，随 H3AP9 方案存档）。
static constexpr int kSummonStopManaDefault = 6; // 敌方魔力阈值默认（0..32767 可调）
// 战场六格坐标（15×11，索引 = y*15+x；实参语义与原版距离对拍见实施文档 §5）。
static constexpr int BATTLEFIELD_COLS = 15;
static constexpr int BATTLEFIELD_HEXES = 165;

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

// 保活方式（部队级，三选一）：挂在每条规则上。
// PM_NONE=不保活（默认）：该队永不参与保活施法。
enum ProtectMode : uint8_t {
    PM_COUNT_BELOW = 0,   // 按剩余数量：剩余数量 ≤ 该队阈值才救（默认 2；0=只救全灭）
    PM_LOSS_GT_RESTORE,   // 损失量大于恢复量：已损 HP 严格大于一次可恢复量才救
    PM_NONE,              // 不保活：该队不参与（默认）
    PM_COUNT
};

struct AutoStackRule {
    AutoActionKind action;
    AutoTargetRule target;
    bool allowDefendFallback;
    bool quickCastFirst;
    int8_t spellSlot;
    int8_t spellSlots[SPELL_SLOT_CAPACITY];
    int8_t spellSlotCount;

    // 保活方式三选一（默认不保活）；法术自动选：亡灵用聚灵、活体用复活。
    uint8_t         protectMode;         // ProtectMode：0=按剩余数量；1=损失量大于恢复量；2=不保活
    // 按数量保活阈值（PM_COUNT_BELOW）：该队剩余数量 ≤ 此值才救。
    // 每队各自一份，默认 2，范围 0..2147483647；损失量方式下不参与判定。
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
    r.protectMode = PM_NONE;
    r.protectCountBelow = 2;
    return r;
}

// 原版友方范围增益。下拉还要同时满足：英雄已学会，且法术表带
// friendlyMass(0x800) 或 expertMassVersion(0x40)。没有范围版的不列。
static constexpr int kBuffSpellCount = 16;
static constexpr int kBuffSpellIds[kBuffSpellCount] = {
    27, 28, 30, 31, 32, 33, 41, 43, 44, 46, 48, 49, 51, 53, 55, 58,
};
static constexpr int kSpellFlagFriendlyMass = 0x800;
static constexpr int kSpellFlagExpertMass = 0x40;

inline bool IsMassBuffSpell(int spell_id, unsigned spell_flags)
{
    if (spell_id <= 0) return false;
    const bool listed = [&]() {
        for (int id : kBuffSpellIds) if (id == spell_id) return true;
        return false;
    }();
    if (!listed) return false;
    return (spell_flags & kSpellFlagFriendlyMass) != 0
        || (spell_flags & kSpellFlagExpertMass) != 0;
}
static constexpr int kSlowSpellId = 54;
// 补状态阈值默认值：剩余回合 ≤ 该值才补（阈值本体在
// StatusProfileFields.refresh_turns，玩家可改）。
static constexpr int kStatusRefreshTurns = 1;
static constexpr int kStatusRefreshTurnsMax = 9;
static constexpr int kStatusSlotCapacity = 8;

// 保持状态是方案级设置，不挂在单支部队上。
// slots 存已选法术 id；0 表示空槽。slow 单独开关，只在专家群体时生效。
// refresh_turns：补状态阈值，剩余回合 ≤ 它才补（1..kStatusRefreshTurnsMax）。
struct StatusProfileFields {
    int slots[kStatusSlotCapacity];
    int slot_count;
    int slow;
    int refresh_turns;
};

inline StatusProfileFields MakeDefaultStatusFields()
{
    StatusProfileFields fields = {};
    fields.refresh_turns = kStatusRefreshTurns;
    return fields;
}

struct StatusMaintainChoice {
    int spell_id;   // -1 = 本回合不补
    int target_hex; // 群体减速时是剩余回合最短的那队
    int mass;       // 1 = 专家群体，目标格只作施法锚点
};

// 全体法术的代表剩余回合。durations 是目标侧各存活队的原始值：
// >0 已带，0 未带，-1 不存在。全体法术各可接受部队的剩余回合一致，
// 所以找到任一已带部队就采用它；全是 0 才表示缺失。无法接受的 0
// 不覆盖已经找到的正值。
inline int RepresentativeMassDuration(const int* durations, int count)
{
    if (!durations || count <= 0) return -1;
    bool saw_stack = false;
    for (int i = 0; i < count; ++i) {
        if (durations[i] < 0) continue;
        saw_stack = true;
        if (durations[i] > 0) return durations[i];
    }
    return saw_stack ? 0 : -1;
}

// durations[i] 是 spell_ids[i] 的代表剩余回合：>0 = 已带，0 = 整侧缺失，
// -1 = 未选/英雄不可施。refresh_turns：只补 ≤ 该值的；多个达标取剩余最少者。
inline int ChooseBuffToRefresh(const int* durations, int count,
    int refresh_turns)
{
    if (!durations || count <= 0) return -1;
    if (refresh_turns < 0) refresh_turns = kStatusRefreshTurns;
    int best = -1;
    for (int i = 0; i < count; ++i) {
        if (durations[i] < 0 || durations[i] > refresh_turns) continue;
        if (best < 0 || durations[i] < durations[best]) best = i;
    }
    return best;
}

// 敌方减速：己方必须是专家群体。enemy_durations 覆盖全部存活敌方。
// 语义 A（2026-10-05 用户定）：群体迟缓按「全体覆盖」算达标——敌方已
// 有任意一队带迟缓 buff（剩余 > refresh_turns）就不再施，只有
// 敌方没有任何一队被覆盖时才施全群体。理由：敌方可能有抵抗术，部分
// 命中是常态，若按「任一队缺失即补」会在被抵抗后连续多回合重复施放，
// 挤占一回合一次的施法位与魔力；被抵抗的队留到全体 buff 将断时随群
// 体一起重新覆盖。-1 表示该队不存在/免疫，不参与覆盖判定。
inline StatusMaintainChoice ChooseSlowTarget(bool expert_mass,
    const int* enemy_durations, const int* enemy_hexes, int enemy_count,
    int refresh_turns)
{
    StatusMaintainChoice choice = {-1, -1, 0};
    if (!expert_mass || !enemy_durations || !enemy_hexes || enemy_count <= 0)
        return choice;
    if (refresh_turns < 0) refresh_turns = kStatusRefreshTurns;
    int best = -1;
    for (int i = 0; i < enemy_count; ++i) {
        if (enemy_durations[i] < 0) continue;      // 不存在/免疫：跳过
        if (enemy_durations[i] > refresh_turns)
            return choice;                         // 已有覆盖：达标，不再施
        if (best < 0 || enemy_durations[i] < enemy_durations[best]) best = i;
    }
    if (best < 0) return choice;
    choice.spell_id = kSlowSpellId;
    choice.target_hex = enemy_hexes[best];
    choice.mass = 1;
    return choice;
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

// ======================================================================
// 战斗内容指纹（§12：同一场战斗的稳定标识）
//
// 目标：重打 / 战斗中读档 S&L / 关游戏再开，同一场战斗得到同一个值；
// 不同战斗（不同敌情或不同触发点）得到不同值。喂入的必须全部是
// 「开局态」数据（重打与 S&L 后均回到同一快照）：
//   - 双方各 ≤21 槽 (type, numberAtStart)，按 (type,count) 升序排序后
//     喂入——与内部槽位顺序、镜像无关；侧序号参与，攻守互换算不同战斗。
//   - 双方英雄 id（无英雄 = -1；不含技能/法术/军队——军队已在槽表内）。
//   - 战场地形、攻城类型。
//   - 冒险地图触发点坐标 (x,y,z)：z 区分地上/地下；取**被攻击方**位置
//     （守方英雄格 / 与攻方英雄相邻的野怪或城镇格 / 兜底攻方英雄格），
//     不用攻击发起格——同一目标从不同方向攻击应视为同一场。
// 哈希 = FNV-1a 64 逐字段滚动。碰撞（同指纹不同战斗）的后果只是把上次
// 方案带进本场（同敌情方案通常仍适用），可由下一次「确定」覆盖自愈。
struct BattleFingerprintInput {
    int side_types[2][21];    // 21 槽生物类型（空槽 0）
    int side_counts[2][21];   // 21 槽开局数量
    int hero_id[2];           // -1 = 无英雄
    int terrain;              // 战场地形
    int siege_kind;           // 0 = 野战
    int map_x, map_y, map_z;  // 冒险地图触发点（z: 0 地上 / 1 地下）
};

inline unsigned long long Fnv1a64_Mix_(unsigned long long h, int v)
{
    const unsigned long long prime = 1099511628211ULL;
    const unsigned int u = static_cast<unsigned int>(v);
    for (int i = 0; i < 4; ++i) {
        h ^= (u >> (i * 8)) & 0xFF;
        h *= prime;
    }
    return h;
}

inline unsigned long long ComputeBattleFingerprint(
    const BattleFingerprintInput& in)
{
    unsigned long long h = 1469598103934665603ULL;
    for (int side = 0; side < 2; ++side) {
        h = Fnv1a64_Mix_(h, 0x53494445 + side); // 侧标签：攻守互换 ≠ 同场
        h = Fnv1a64_Mix_(h, in.hero_id[side]);
        // 槽 (type,count) 排序后喂入：槽位顺序无关。
        int idx[21];
        int n = 0;
        for (int i = 0; i < 21; ++i) {
            if (in.side_types[side][i] > 0 && in.side_counts[side][i] > 0)
                idx[n++] = i;
        }
        for (int i = 1; i < n; ++i) { // 插入排序（n ≤ 21）
            const int cur = idx[i];
            int j = i - 1;
            while (j >= 0
                && (in.side_types[side][idx[j]] > in.side_types[side][cur]
                    || (in.side_types[side][idx[j]] == in.side_types[side][cur]
                        && in.side_counts[side][idx[j]]
                            > in.side_counts[side][cur]))) {
                idx[j + 1] = idx[j];
                --j;
            }
            idx[j + 1] = cur;
        }
        h = Fnv1a64_Mix_(h, n);
        for (int i = 0; i < n; ++i) {
            h = Fnv1a64_Mix_(h, in.side_types[side][idx[i]]);
            h = Fnv1a64_Mix_(h, in.side_counts[side][idx[i]]);
        }
    }
    h = Fnv1a64_Mix_(h, in.terrain);
    h = Fnv1a64_Mix_(h, in.siege_kind);
    h = Fnv1a64_Mix_(h, in.map_x);
    h = Fnv1a64_Mix_(h, in.map_y);
    h = Fnv1a64_Mix_(h, in.map_z);
    return h;
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

// 面板准入纯规则（含阵亡）：count_initial>0 即该槽本场存在过部队——阵亡
// （当前数量 0）的己方非召唤部队仍列入面板，规则可见可改、复活后继续生效
// （方案生效范围=面板所见部队）；未存在过的空槽不列。弹药车永不进入；
// 投石车必须有弹道术；召唤物/克隆物（稳定身份 STACK_ID_SUMMON，由集成层
// 按与 MakeStableStackIdentity 同口径判定）不进部队页——其行动只由召唤页
// 的共享规则控制，配置出口唯一（§2.6）。
inline bool IsConfigurablePanelStack(int creature_type, int count_initial,
    bool has_ballistics, bool is_summon_clone)
{
    if (count_initial <= 0) return false;
    if (is_summon_clone) return false;
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
    // 召唤动作：随机移动可配「允许降级为防御」；散开的降级链固定终于防御，不显示。
    if (action == AA_RANDOM_MOVE) return true;
    if (action == AA_SCATTER) return false;
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

// 保活复活（§3.1.1）纯判定逻辑。
// 可恢复量 = 法术表 baseValue[等级] + 力量 × spEffect（H3Spell::GetBaseEffect）。
// 读不到法术表时退回旧估算：基础 50×力量 / 高级 75×力量 / 专家 100×力量。
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

inline int ResurrectionRestoreHp(int base_value, int sp_effect,
    int expertise, int spell_power)
{
    if (expertise <= 0 || expertise > 3 || spell_power <= 0) return 0;
    if (base_value < 0 || sp_effect <= 0)
        return ResurrectionRestoreHp(expertise, spell_power);
    const int64_t total = static_cast<int64_t>(base_value)
        + static_cast<int64_t>(spell_power) * sp_effect;
    if (total <= 0) return ResurrectionRestoreHp(expertise, spell_power);
    if (total > 0x7FFFFFFF) return 0x7FFFFFFF;
    return static_cast<int>(total);
}

// 保活与召唤是同一条施法通道，没有方案级通道选择：每次行动（还有施法
// 次数时）先按各队的 ProtectMode 判保活（§3.1.1，默认不保活），
// 没人要救且召唤已启用（SummonProfileFields.enabled）再判召唤（§3.7）。

// 是否够格：无损失不触发；按该队自己的保活方式判定。
// PM_COUNT_BELOW 要求剩余数量 ≤ 该队阈值（全灭数量 0 天然满足，0=只救全灭）；
// PM_LOSS_GT_RESTORE 要求已损 HP 严格大于一次可恢复量；PM_NONE 永不触发。
inline bool ProtectShouldCast(ProtectMode mode,
    int restorable_hp, int wound_value,
    int count_current, int count_below)
{
    if (mode == PM_NONE) return false;
    if (wound_value <= 0) return false;
    switch (mode) {
    case PM_LOSS_GT_RESTORE: return restorable_hp > 0
        && wound_value > restorable_hp;
    case PM_COUNT_BELOW:     return count_current <= count_below;
    default:                 return false;
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

// ---------------------------------------------------------------------------
// 召唤通道（保活复活的兜底：无人可救且已启用时）与召唤物行动的纯判定。
// ---------------------------------------------------------------------------

// 自动停止第二条件：敌方英雄法力耗尽前停手。
// 无英雄/无魔法书不触发（无书英雄法力常为 0，不能开战即误停）。
inline bool ShouldStopOnEnemyMana(int stop_flag, bool has_enemy_hero,
    bool has_spellbook, int enemy_mana, int threshold)
{
    if (!stop_flag) return false;
    if (!has_enemy_hero || !has_spellbook) return false;
    if (enemy_mana < 0) return false;
    return enemy_mana <= threshold;
}

// 队数/血量两条件的组合方式：0=和（两者都满足才召，默认）；1=或（任一满足即召）。
static constexpr int SUMMON_COMBINE_AND = 0;
static constexpr int SUMMON_COMBINE_OR  = 1;

// 召唤时机：己方存活队数（含召唤物、不含战争机器）< 阈值（严格小于）、
// 剩余血量合计（同口径）≤ 阈值；组合方式 cond_combine：和=都满足、或=任一。
// 本回合未施法、已学、法力够。
inline bool SummonShouldCast(int stack_count, int count_th, int hp_total,
    int hp_th, int cond_combine, int mana, int mana_cost, bool hero_casted,
    bool learned)
{
    if (hero_casted) return false;
    if (!learned) return false;
    if (mana_cost > 0 && mana < mana_cost) return false;
    const bool count_ok = stack_count < count_th;
    const bool hp_ok = hp_total <= hp_th;
    return cond_combine == SUMMON_COMBINE_OR ? (count_ok || hp_ok)
                                             : (count_ok && hp_ok);
}

inline bool IsSummonedElemental(int creature_id)
{
    for (int i = 0; i < SUMMON_ELEMENT_COUNT; ++i)
        if (creature_id == kSummonCreatureIds[i]) return true;
    return false;
}

// 选召唤法术。amounts[i]/learned[i]：各元素本次召唤总量（调用方按等级×力量×生物算好）
// 与是否已学；spell_pick：0=自动，1..4=固定元素；locked_index：自动模式本场锁定（-1 无）。
// 返回元素下标 0..3（对应 kSummonSpellIds/kSummonCreatureIds），-1 = 不召。
// 固定未学 → 不召；自动有锁定 → 用锁定；否则取总量最大（平手取靠前，稳定可测）。
inline int PickSummonSpell(const int amounts[SUMMON_ELEMENT_COUNT],
    const bool learned[SUMMON_ELEMENT_COUNT], int spell_pick, int locked_index)
{
    if (spell_pick >= 1 && spell_pick <= SUMMON_ELEMENT_COUNT) {
        const int fixed = spell_pick - 1;
        return learned[fixed] ? fixed : -1;
    }
    if (locked_index >= 0 && locked_index < SUMMON_ELEMENT_COUNT)
        return locked_index;
    int best = -1;
    for (int i = 0; i < SUMMON_ELEMENT_COUNT; ++i) {
        if (!learned[i]) continue;
        if (best < 0 || amounts[i] > amounts[best]) best = i;
    }
    return best;
}

// 己方存活侧统计（调用方先过滤：只传存活槽、剔除战争机器）。
inline int CountAliveSideStacks(const TargetCandidate* stacks, int count)
{
    int alive = 0;
    for (int i = 0; i < count; ++i)
        if (stacks[i].count_current > 0) ++alive;
    return alive;
}

inline int SumSideRemainingHp(const TargetCandidate* stacks, int count)
{
    int64_t total = 0;
    for (int i = 0; i < count; ++i) {
        if (stacks[i].count_current <= 0) continue;
        total += StackRemainingHp(stacks[i]);
        if (total > 0x7FFFFFFF) return 0x7FFFFFFF;
    }
    return static_cast<int>(total);
}

// 战场六格距离（15×11，odd-r：奇数行右移半格；转立方坐标取三轴最大差）。
// 与原版距离的对拍列入实施文档 §5 上机验证。
inline int HexCoordDistance(int hex_a, int hex_b)
{
    if (hex_a < 0 || hex_b < 0
        || hex_a >= BATTLEFIELD_HEXES || hex_b >= BATTLEFIELD_HEXES)
        return 0x7FFFFFFF;
    const int x1 = hex_a % BATTLEFIELD_COLS, y1 = hex_a / BATTLEFIELD_COLS;
    const int x2 = hex_b % BATTLEFIELD_COLS, y2 = hex_b / BATTLEFIELD_COLS;
    const int cx1 = x1 - ((y1 - (y1 & 1)) >> 1);
    const int cx2 = x2 - ((y2 - (y2 & 1)) >> 1);
    const int dx = cx2 - cx1;
    const int dz = y2 - y1;
    const int dy = -dx - dz;
    int d = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    const int az = dz < 0 ? -dz : dz;
    if (ay > d) d = ay;
    if (az > d) d = az;
    return d;
}

// 召唤物移动目标格。candidates：可达格；own_positions：己方其它存活部队格
// （不含自身，含战争机器）。返回目标格；-1 = 原地（不动/防御）。
// - AA_SCATTER 分级满足：先「与所有己方部队距离 ≥2」，降「≥1」，两档均无则原地；
//   当前格已满足该档则不移动；该档多格取离部队最远者（平手取候选序靠前者，稳定可测）。
// - AA_RANDOM_MOVE：排除当前格后均匀随机（rng 由调用方传入）。
inline int ChooseSummonMoveHex(AutoActionKind kind, const int* candidates,
    int cand_count, int cur_hex, const int* own_positions, int own_count,
    uint32_t rng)
{
    if (kind == AA_RANDOM_MOVE) {
        int pool[BATTLEFIELD_HEXES] = {};
        int n = 0;
        for (int i = 0; i < cand_count && n < BATTLEFIELD_HEXES; ++i) {
            const int hex = candidates[i];
            if (hex >= 0 && hex < BATTLEFIELD_HEXES && hex != cur_hex)
                pool[n++] = hex;
        }
        if (n <= 0) return -1;
        return pool[rng % static_cast<uint32_t>(n)];
    }
    if (kind != AA_SCATTER) return -1;
    auto min_dist = [&](int hex) -> int {
        int m = 0x7FFFFFFF;
        for (int i = 0; i < own_count; ++i) {
            const int d = HexCoordDistance(hex, own_positions[i]);
            if (d < m) m = d;
        }
        return m;
    };
    for (int tier = 2; tier >= 1; --tier) {
        // 当前格已满足该档：不动（原地防御由调用方提交）。
        if (min_dist(cur_hex) >= tier) return -1;
        int best = -1, best_d = -1;
        for (int i = 0; i < cand_count; ++i) {
            const int hex = candidates[i];
            if (hex < 0 || hex >= BATTLEFIELD_HEXES || hex == cur_hex) continue;
            const int d = min_dist(hex);
            if (d >= tier && d > best_d) {
                best = hex;
                best_d = d;
            }
        }
        if (best >= 0) return best;
    }
    return -1; // 两档都做不到：原地防御
}

// 召唤物共享规则的行动集（专用，不套用普通部队）。
inline int GetAllowedSummonActions(AutoActionKind out_actions[4])
{
    if (!out_actions) return 0;
    out_actions[0] = AA_MANUAL;
    out_actions[1] = AA_DEFEND;
    out_actions[2] = AA_SCATTER;
    out_actions[3] = AA_RANDOM_MOVE;
    return 4;
}

// 召唤共享规则规范化：行动裁剪到 4 动作集；无目标/施法/保活；
// 「允许降级为防御」仅随机移动保留。
inline void NormalizeSummonRule(AutoStackRule* rule)
{
    if (!rule) return;
    bool action_ok = false;
    AutoActionKind allowed[4] = {};
    const int n = GetAllowedSummonActions(allowed);
    for (int i = 0; i < n; ++i)
        if (allowed[i] == rule->action) action_ok = true;
    if (!action_ok) rule->action = AA_MANUAL;
    rule->target = DefaultTargetForAction(AA_MANUAL);
    rule->allowDefendFallback =
        (rule->action == AA_RANDOM_MOVE) && rule->allowDefendFallback;
    rule->quickCastFirst = false;
    rule->spellSlot = 1;
    for (int i = 0; i < SPELL_SLOT_CAPACITY; ++i)
        rule->spellSlots[i] = -1;
    rule->spellSlotCount = 0;
    rule->protectMode = PM_NONE;   // 召唤物共享规则不走保活
    rule->protectCountBelow = 2;
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

// 方案存档：一套方案一个 JSON 对象，字段分开存，数组按实际数量存。
// 不再使用 H3AP<N> 数字串。旧数字串一律拒绝，视为没有存档。
static constexpr int PROFILE_STORE_SLOTS = 21;
// 保留纯函数整数编解码供旧的独立方案文件代码编译；战斗 JSON 不再调用它。
static constexpr int PROFILE_STORE_RULE_FIELDS =
    6 + MOVE_WAYPOINT_CAPACITY + 1 + 2 * MELEE_PAIR_CAPACITY + 1 + 3
    + SPELL_SLOT_CAPACITY + 3;
static constexpr int PROFILE_STORE_INTS = PROFILE_STORE_SLOTS * 2 + 1 + 7
    + (PROFILE_STORE_SLOTS + 1) * PROFILE_STORE_RULE_FIELDS;
static constexpr int DEFAULT_STOP_TURNS = 10;

// 方案级召唤配置（随 H3AP9 方案存档）。保活与召唤同一条施法通道：
// 保活优先，无人可救且 enabled 时才走召唤。
struct SummonProfileFields {
    int enabled;          // 是否启用自动召唤（0/1，默认 0）：复活无人可救时的兜底
    int count_th;         // 队数阈值，默认 2，2..21（存活队数严格小于此值才触发）
    int hp_th;            // 血量阈值，默认 750，≥0（口径同自动停止）
    int spell_pick;       // 法术选择：0=自动；1..SUMMON_ELEMENT_COUNT=固定元素
    int cond_combine;     // 队数/血量两条件组合：0=和（默认）；1=或
    int stop_enemy_mana;  // 自动停止第二条件勾选（0/1）
    int stop_mana_th;     // 敌方魔力阈值，默认 6，0..32767（勾选时生效）
    AutoStackRule summon_rule; // 召唤物共享行动规则
};

inline SummonProfileFields MakeDefaultSummonFields()
{
    SummonProfileFields fields = {};
    fields.enabled = 0;   // 默认不召唤：只保活复活
    fields.count_th = 2;
    fields.hp_th = 750;
    fields.spell_pick = 0;
    fields.cond_combine = SUMMON_COMBINE_AND; // 默认「和」：两条件都满足才召
    fields.stop_enemy_mana = 1; // 默认勾选：敌方魔力≤阈值即停（阈值默认 6）
    fields.stop_mana_th = kSummonStopManaDefault;
    fields.summon_rule = MakeDefaultRule();
    fields.summon_rule.action = AA_DEFEND; // 召唤物默认防御（可改手动/散开/随机）
    return fields;
}

// ======================================================================
// 战斗存档记录（§17 智能存读档）：一场战斗一条 = 时间戳 + 激活方案 +
// 5 套完整方案。存档文件 <指纹>.json 按时间序保存 ≤30 条（集成层
// ConfigLog 负责 JSON 编解码与落盘；结构与内容比较是纯函数，在此可测）。
// ======================================================================
struct BattleStoreRecord {
    char time[20];                  // "yyyymmdd-hhmmss"
    int  active;                    // 0..4
    AutoStackRule rules[5][21];
    uint16_t stop_turns[5];
    SummonProfileFields summon[5];
    StatusProfileFields status[5];
};

// 两条记录内容是否完全相同（忽略时间戳）：「确定」时与文件里最后一
// 条比对，相同则不新增存档。
inline bool BattleStoreRecordContentEquals(const BattleStoreRecord& a,
    const BattleStoreRecord& b)
{
    // 字节级手写比较（本文件不依赖 <cstring>）。
    auto bytes_equal = [](const void* p, const void* q, int n) -> bool {
        const unsigned char* x = static_cast<const unsigned char*>(p);
        const unsigned char* y = static_cast<const unsigned char*>(q);
        for (int i = 0; i < n; ++i)
            if (x[i] != y[i]) return false;
        return true;
    };
    if (a.active != b.active) return false;
    for (int p = 0; p < 5; ++p) {
        if (a.stop_turns[p] != b.stop_turns[p]) return false;
        if (!bytes_equal(&a.summon[p], &b.summon[p], sizeof(SummonProfileFields)))
            return false;
        if (!bytes_equal(&a.status[p], &b.status[p], sizeof(StatusProfileFields)))
            return false;
        if (!bytes_equal(a.rules[p], b.rules[p], sizeof(a.rules[p])))
            return false;
    }
    return true;
}

// 时间戳格式校验：15 字符 yyyymmdd-hhmmss，除第 8 位为 '-' 外全数字，
// 且月份 01..12、日 01..31、时分秒 ≤ 59 的粗校验。
inline bool BattleStoreStampValid(const char* s)
{
    if (!s) return false;
    for (int i = 0; i < 15; ++i) {
        if (!s[i]) return false;
        if (i == 8) {
            if (s[i] != '-') return false;
        } else if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    if (s[15] != 0) return false;
    const int mon  = (s[4] - '0') * 10 + (s[5] - '0');
    const int day  = (s[6] - '0') * 10 + (s[7] - '0');
    const int hour = (s[9] - '0') * 10 + (s[10] - '0');
    const int min  = (s[11] - '0') * 10 + (s[12] - '0');
    const int sec  = (s[13] - '0') * 10 + (s[14] - '0');
    return mon >= 1 && mon <= 12 && day >= 1 && day <= 31 && hour <= 23
        && min <= 59 && sec <= 59;
}


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
    out[n++] = rule.protectMode;
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
    // 第 59 个整数是保活方式（ProtectMode，三选一；越界按不保活）。
    r.protectMode = (in[n] == (int)PM_COUNT_BELOW
            || in[n] == (int)PM_LOSS_GT_RESTORE)
        ? static_cast<ProtectMode>(in[n]) : PM_NONE;
    ++n;
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
// Decode 只接受以 "H3AP9 " 开头且整数个数恰好匹配的文本；
// 旧版存档（H3AP8 及更早）一律拒绝，重新配置即可。
inline int EncodeProfileStoreText(const int army_types[PROFILE_STORE_SLOTS],
    const int army_counts[PROFILE_STORE_SLOTS],
    const AutoStackRule rules[PROFILE_STORE_SLOTS],
    uint16_t stop_turns,
    const SummonProfileFields& summon,
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
    if (!append("H3AP9")) return -1;
    int* ints = new int[PROFILE_STORE_INTS];
    int n = 0;
    for (int s = 0; s < PROFILE_STORE_SLOTS; ++s) {
        ints[n++] = army_types[s];
        ints[n++] = army_counts[s];
    }
    ints[n++] = summon.enabled ? 1 : 0;
    int turns = stop_turns;
    if (turns < 0) turns = 0;
    if (turns > 999) turns = 999;
    ints[n++] = turns;
    ints[n++] = summon.count_th;
    ints[n++] = summon.hp_th;
    ints[n++] = summon.spell_pick;
    ints[n++] = (summon.cond_combine == SUMMON_COMBINE_OR)
        ? SUMMON_COMBINE_OR : SUMMON_COMBINE_AND;
    ints[n++] = summon.stop_enemy_mana ? 1 : 0;
    int mana_th = summon.stop_mana_th;
    if (mana_th < 0) mana_th = 0;
    if (mana_th > 32767) mana_th = 32767;
    ints[n++] = mana_th;
    bool ok = true;
    if (ok) {
        __try {
            EncodeRuleInts(summon.summon_rule, ints + n);
        } __except (1) {
            delete[] ints;
            return -200000;
        }
        n += PROFILE_STORE_RULE_FIELDS;
    }
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
    AutoStackRule rules[PROFILE_STORE_SLOTS],
    uint16_t* stop_turns,
    SummonProfileFields* summon)
{
    if (!text || !army_types || !army_counts || !rules
        || !stop_turns || !summon) return false;
    // 只认 H3AP9（PROFILE_STORE_INTS，每规则 60 整数含保活方式、
    // 召唤 7 整数含启用位与条件组合）。
    // 旧版存档（H3AP8 及更早）不兼容，直接拒绝，用户重新配置即可。
    static const char magic[6] = "H3AP9";
    for (int i = 0; i < 5; ++i)
        if (text[i] != magic[i]) return false;
    const int expected = PROFILE_STORE_INTS;
    const char* p = text + 5;

    int* ints = new int[PROFILE_STORE_INTS];
    int count = 0;
    bool bad = false;
    while (*p && !bad) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (!*p) break;
        if (count >= expected) { bad = true; break; }
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
    if (bad || count != expected) {
        // 含更旧格式（H3AP5 1304 整数 / H3AP2 6247 / 3575/3580 交错坏档），
        // 一律拒绝。
        delete[] ints;
        return false;
    }

    uint16_t decoded_stop = 0;
    SummonProfileFields decoded_summon = {};
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
    // 召唤启用位：0=不召唤（只保活复活），1=启用兜底召唤。
    if (ints[n] < 0 || ints[n] > 1) {
        delete[] ints;
        return false;
    }
    decoded_summon.enabled = ints[n++];
    if (ints[n] < 0 || ints[n] > 999) { delete[] ints; return false; }
    decoded_stop = static_cast<uint16_t>(ints[n++]);
    decoded_summon.count_th = ints[n++];
    decoded_summon.hp_th = ints[n++];
    decoded_summon.spell_pick = ints[n++];
    decoded_summon.cond_combine = ints[n++];
    decoded_summon.stop_enemy_mana = ints[n++];
    decoded_summon.stop_mana_th = ints[n++];
    if (decoded_summon.count_th < 2
        || decoded_summon.count_th > PROFILE_STORE_SLOTS) {
        delete[] ints; return false;
    }
    if (decoded_summon.hp_th < 0) { delete[] ints; return false; }
    if (decoded_summon.spell_pick < 0
        || decoded_summon.spell_pick > SUMMON_ELEMENT_COUNT) {
        delete[] ints; return false;
    }
    if (decoded_summon.cond_combine != SUMMON_COMBINE_AND
        && decoded_summon.cond_combine != SUMMON_COMBINE_OR) {
        delete[] ints; return false;
    }
    if (decoded_summon.stop_enemy_mana < 0
        || decoded_summon.stop_enemy_mana > 1) {
        delete[] ints; return false;
    }
    if (decoded_summon.stop_mana_th < 0
        || decoded_summon.stop_mana_th > 32767) {
        delete[] ints; return false;
    }
    if (!DecodeRuleInts(ints + n, &decoded_summon.summon_rule)) {
        delete[] ints; return false;
    }
    NormalizeSummonRule(&decoded_summon.summon_rule);
    n += PROFILE_STORE_RULE_FIELDS;
    for (int s = 0; s < PROFILE_STORE_SLOTS; ++s) {
        if (!DecodeRuleInts(ints + n, &decoded[s])) {
            delete[] ints; return false;
        }
        n += PROFILE_STORE_RULE_FIELDS;
    }
    delete[] ints;
    for (int s = 0; s < PROFILE_STORE_SLOTS; ++s) {
        army_types[s] = decoded_army_types[s];
        army_counts[s] = decoded_army_counts[s];
        rules[s] = decoded[s];
    }
    *stop_turns = decoded_stop;
    *summon = decoded_summon;
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
    if (rule->protectMode >= PM_COUNT) rule->protectMode = PM_NONE;

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
using H3AutoPolicy::SummonProfileFields;
using H3AutoPolicy::MakeDefaultSummonFields;
using H3AutoPolicy::BattleStoreRecord;
using H3AutoPolicy::BattleStoreRecordContentEquals;
using H3AutoPolicy::BattleStoreStampValid;
using H3AutoPolicy::AA_MANUAL;
using H3AutoPolicy::AA_DEFEND;
using H3AutoPolicy::AA_WAIT;
using H3AutoPolicy::AA_MOVE;
using H3AutoPolicy::AA_MELEE_ATTACK;
using H3AutoPolicy::AA_RANGED_ATTACK;
using H3AutoPolicy::AA_FIRST_AID;
using H3AutoPolicy::AA_SCATTER;
using H3AutoPolicy::AA_RANDOM_MOVE;
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
using H3AutoPolicy::AT_COUNT;
using H3AutoPolicy::ATS_COUNT;
using H3AutoPolicy::ProtectMode;
using H3AutoPolicy::PM_COUNT_BELOW;
using H3AutoPolicy::PM_LOSS_GT_RESTORE;
using H3AutoPolicy::PM_NONE;
using H3AutoPolicy::SUMMON_ELEMENT_COUNT;
using H3AutoPolicy::SUMMON_COMBINE_AND;
using H3AutoPolicy::SUMMON_COMBINE_OR;
using H3AutoPolicy::MakeDefaultRule;
using H3AutoPolicy::StatusProfileFields;
using H3AutoPolicy::MakeDefaultStatusFields;
using H3AutoPolicy::kStatusSlotCapacity;
using H3AutoPolicy::kBuffSpellIds;
using H3AutoPolicy::IsMassBuffSpell;
