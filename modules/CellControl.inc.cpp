// ========== CellControl.inc.cpp ==========
// 格子控件：左侧生物图标，右侧渐进式动态控件（行动 → 目标 → 降级）
// 见 H3Note/H3Auto行动与目标策略实现方案.md §11

#ifndef _CELL_CONTROL_INC_CPP
#define _CELL_CONTROL_INC_CPP

static void WriteLog(const char* fmt, ...);

void Fill(H3LoadedPcx16* scr, int x, int y, int w, int h, int r, int g, int b);
H3Font* GetSmallFont();
static H3LoadedPcx16* LoadPanelPcx24_(const char* asset_name, int expected_width,
    int expected_height, H3LoadedPcx16*& cache, bool& load_failed);
static void DrawTransparentPcx_(H3LoadedPcx16* source,
    H3LoadedPcx16* destination, int dst_x, int dst_y);

static H3LoadedPcx16* s_cc_icon_frame = nullptr;
static bool s_cc_icon_frame_load_failed = false;

// ========================================================================
// 公共类型
// ========================================================================

struct CellData
{
    INT32   creature_type;
    INT32   position;
    INT32   count_alive;
    H3LoadedDef* creature_def;
    INT     army_slot_ix;
    bool    is_ranged;   // 由面板打开时按 stack.info.shooter 精确判定并保存
    bool    has_artillery; // 英雄有炮术（弩车/箭塔：可选手动）
    bool    has_first_aid; // 英雄有急救术（帐篷：可选手动）
    AutoStackRule rule;
};

// ========================================================================
// 控件常量
// ========================================================================

static const int CC_CELL_W    = 568;
// 金框 342 内刚好 3 行：CELL_H = (342-16+4)/3 = 110（上下各 8px 内边距）。
static const int CC_CELL_H    = 110;

// 图标贴左上角。卡片金边约 2px，再留 3px 空隙 → 内容起点约 5。
// 金框资源比图标大 2px 且画在 icon-1，最终外缘大约在 4，视觉上有边距。
static const int CC_ICON_X   = 5;
static const int CC_ICON_Y   = 5;
static const int CC_ICON_W   = 58;
static const int CC_ICON_H   = 64;
static const int CC_ICON_FRAME_W = 60;
static const int CC_ICON_FRAME_H = 66;

static const int CC_LABEL_H  = 11;

// 一行一格（宽格 568×110）横排布局：
//   图标列（位置左下/数量右下） | 第二小列（仅够「行动前循环施法:」+ 行动/降级）
//   | 第三小列（循环施法槽 + 选择器/近战/移动，吃掉剩余宽度）
// 第二小列左缘贴图标金框右缘，留 4px 间距。
static const int CC_ICON_FRAME_RIGHT = (CC_ICON_X - 1) + CC_ICON_FRAME_W; // ≈64
static const int CC_COL2_X   = CC_ICON_FRAME_RIGHT + 4; // ≈68
static const int CC_COL2_W   = 118; // 刚好放下「行动前循环施法:」
static const int CC_COL3_X   = CC_COL2_X + CC_COL2_W + 6; // ≈192
static const int CC_COL3_RIGHT = 560; // 卡片右内边距
static const int CC_COL3_W   = CC_COL3_RIGHT - CC_COL3_X; // ≈368
static const int CC_ROW_H    = 22;
// 顶部循环施法行；原行动/目标两行整体下移，行间距略拉开适配 110 高。
static const int CC_SPELL_Y  = 6;
static const int CC_TOP_Y    = 36;  // 中行（行动/选择器/路径槽）
static const int CC_BOT_Y    = 66;  // 下行（降级/阵营/路径槽）
static const int CC_CHECKBOX_H = 14;
// 循环施法：标签占第二小列；槽位从第三小列起。
// 单数字槽固定小宽，按第三列宽度能摆几个就是几个（当前 10）。
static const int CC_SPELL_LABEL_W = CC_COL2_W;
static const int CC_SPELL_SLOT_GAP = 3;
static const int CC_SPELL_SLOT_W = 34; // 刚好显示一位数字
// 循环移动/近战槽：按文本宽度定槽宽，再反算每行/总容量（两行）。
static const int CC_PATH_SLOT_GAP = 3;
static const int CC_MOVE_SLOT_W = 43;  // A01
static const int CC_MELEE_SLOT_W = 71; // A01→B02
static const int CC_MOVE_SLOTS_PER_ROW =
    (CC_COL3_W + CC_PATH_SLOT_GAP) / (CC_MOVE_SLOT_W + CC_PATH_SLOT_GAP); // 8
static const int CC_MELEE_SLOTS_PER_ROW =
    (CC_COL3_W + CC_PATH_SLOT_GAP) / (CC_MELEE_SLOT_W + CC_PATH_SLOT_GAP); // 5

// 兼容旧名
static const int CC_COMBO_X  = CC_COL2_X;
static const int CC_COMBO_W  = CC_COL2_W; // 行动下拉用第二小列宽
static const int CC_COL_W    = CC_COL3_W; // 旧“内容列宽”=第三列
static const int CC_ROW1_Y   = CC_TOP_Y;

static const int CC_DROPDOWN_ITEM_H = 18;

// 标签（由 SettingsDlg 注入）
extern const char* g_action_labels[AA_COUNT];
extern const char* g_selector_labels[SEL_COUNT];

// 展开中的下拉类型
enum CellExpandKind {
    CEX_NONE = 0,
    CEX_ACTION,
    CEX_SELECTOR,
    CEX_STAND,    // 近战站立格（全战场坐标，可滚动）
    CEX_ATTACK,   // 近战攻击格（站立格相邻格）
    CEX_SPELL,    // 保留枚举；循环施法改为数字键录入，不再展开下拉
};

// ========================================================================
// CellControl 结构
// ========================================================================

struct CellControl
{
    CellData         data;
    H3LoadedPcx16*   buffer;

    CellExpandKind   expanded;
    int              hover_item;      // -1 = 无
    bool             action_pressed;
    bool             selector_pressed;
    bool             dirty;
    bool             has_data;
    H3LoadedDef*     def_cache;

    // 循环近战组合拾取请求：0=无，1..MELEE_PAIR_CAPACITY=要新增/重设的组合索引+1。
    int              melee_pair_pick_request;
    // 循环移动路径点拾取请求：0=无，1..MOVE_WAYPOINT_CAPACITY=要新增/重设的路径点索引+1。
    // 与循环近战一致：每次只进战场点 1 格，返回后覆盖/追加对应槽。
    int              move_path_pick_request;
    // 循环施法录入请求：0=无，1..SPELL_SLOT_CAPACITY=待按数字键的槽索引+1。
    // 面板保持打开，仅支持按 1-9/0 写入同一数字（不再点选快捷施法栏）。
    int              spell_pick_request;
    // 展开可滚动列表的滚动顶行（用于 CEX_STAND）
    int              dd_scroll;
};

// ========================================================================
// 规则/选项辅助
// ========================================================================

static bool CellControl_IsWarMachineType(int creature_type)
{
    return creature_type == eCreature::CATAPULT
        || creature_type == eCreature::BALLISTA
        || creature_type == eCreature::FIRST_AID_TENT
        || creature_type == eCreature::AMMO_CART
        || creature_type == eCreature::ARROW_TOWER;
}

static bool CellControl_IsRangedType(int creature_type)
{
    // 战争机器单独处理；普通远程用 creature info 的 shots 字段更准，
    // 这里用 H3API 的 isRanged 若可用，否则保守：只给常见远程标记。
    // 面板构建时会用 H3CombatCreature::info 更准确时再过滤。
    if (creature_type < 0) return false;
    if (creature_type == eCreature::BALLISTA
        || creature_type == eCreature::ARROW_TOWER)
        return true;
    return false;
}

// ========================================================================
// 战场 hex 辅助（17 列/行，有效列 1..15，有效行 0..10）
// hex = row*17 + col；相邻按 HoMM3 奇偶行错开规则。
// ========================================================================

static bool CellControl_HexValid(int hex)
{
    if (hex < 1 || hex > 185) return false;
    const int row = hex / 17;
    const int col = hex % 17;
    return row >= 0 && row <= 10 && col >= 1 && col <= 15;
}

// 填充某 hex 的相邻格（最多 6 个有效格），返回数量。
// 优先使用原版战斗管理器已构建的 adjacentSquares：它就是游戏实际认可的
// 六方向相邻表，避免手写奇偶行偏移方向与 H3 棋盘布局相反。
static int CellControl_HexNeighbors(int hex, int out[6])
{
    int n = 0;
    if (!CellControl_HexValid(hex) || !out) return 0;

    H3CombatManager* mgr = H3CombatManager::Get();
    if (mgr) {
        __try {
            const H3AdjacentSquares& adjacent = mgr->adjacentSquares[hex];
            const int16_t* original = &adjacent.topRight;
            for (int i = 0; i < 6; ++i) {
                const int candidate = original[i];
                if (!CellControl_HexValid(candidate)) continue;
                bool duplicate = false;
                for (int k = 0; k < n; ++k)
                    if (out[k] == candidate) { duplicate = true; break; }
                if (!duplicate) out[n++] = candidate;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            n = 0;
        }
        if (n > 0) return n;
    }

    // 无战斗管理器时的保底公式。H3 的可见第一行按“右移行”处理，
    // 因而与此前实现的奇偶方向相反（例如 61 的左下邻格应含 77）。
    const int row = hex / 17;
    const bool odd = (row & 1) != 0;
    int cand[6] = { hex - 1, hex + 1, 0, 0, 0, 0 };
    if (odd) {
        cand[2] = hex - 18; // 上左
        cand[3] = hex - 17; // 上右
        cand[4] = hex + 16; // 下左
        cand[5] = hex + 17; // 下右
    } else {
        cand[2] = hex - 17; // 上左
        cand[3] = hex - 16; // 上右
        cand[4] = hex + 17; // 下左
        cand[5] = hex + 18; // 下右
    }
    for (int i = 0; i < 6; ++i)
        if (CellControl_HexValid(cand[i])) out[n++] = cand[i];
    return n;
}

static bool CellControl_HexAdjacent(int a, int b)
{
    if (!CellControl_HexValid(a) || !CellControl_HexValid(b)) return false;
    int neighbors[6];
    const int n = CellControl_HexNeighbors(a, neighbors);
    for (int i = 0; i < n; ++i)
        if (neighbors[i] == b) return true;
    return false;
}

// 规范化循环施法序列：压紧有效快捷键、迁移旧单槽、同步兼容镜像。
static void CellControl_NormalizeSpellSlots(AutoStackRule* rule)
{
    if (!rule) return;

    auto is_valid_slot = [](int slot) -> bool {
        return slot == 0 || (slot >= 1 && slot <= 9);
    };

    int8_t slots[SPELL_SLOT_CAPACITY];
    int count = 0;
    int requested = rule->spellSlotCount;
    if (requested < 0) requested = 0;
    if (requested > SPELL_SLOT_CAPACITY) requested = SPELL_SLOT_CAPACITY;
    for (int i = 0; i < requested; ++i) {
        const int slot = rule->spellSlots[i];
        if (!is_valid_slot(slot)) continue;
        slots[count++] = static_cast<int8_t>(slot);
    }

    // 旧版只保存单槽 spellSlot + quickCastFirst：迁移为序列第 0 项。
    if (count == 0 && rule->quickCastFirst && is_valid_slot(rule->spellSlot)) {
        slots[0] = rule->spellSlot;
        count = 1;
    }

    for (int i = 0; i < SPELL_SLOT_CAPACITY; ++i)
        rule->spellSlots[i] = (i < count) ? slots[i] : static_cast<int8_t>(-1);
    rule->spellSlotCount = static_cast<int8_t>(count);
    if (count <= 0) {
        rule->quickCastFirst = false;
        // 保持默认镜像，便于旧逻辑读取。
        if (!is_valid_slot(rule->spellSlot))
            rule->spellSlot = 1;
    } else {
        rule->quickCastFirst = true;
        rule->spellSlot = rule->spellSlots[0];
    }
}

// 规范化循环近战序列：压紧有效组合、迁移旧的单组字段、同步兼容镜像。
static void CellControl_NormalizeMeleePairs(AutoTargetRule* target)
{
    if (!target) return;

    int requested = target->meleePairCount;
    if (requested < 0) requested = 0;
    if (requested > MELEE_PAIR_CAPACITY) requested = MELEE_PAIR_CAPACITY;

    int16_t stands[MELEE_PAIR_CAPACITY];
    int16_t attacks[MELEE_PAIR_CAPACITY];
    int count = 0;
    for (int i = 0; i < requested; ++i) {
        const int stand_hex = target->meleeStandHexes[i];
        const int attack_hex = target->meleeAttackHexes[i];
        if (!CellControl_HexAdjacent(attack_hex, stand_hex)) continue;
        stands[count] = static_cast<int16_t>(stand_hex);
        attacks[count] = static_cast<int16_t>(attack_hex);
        ++count;
    }

    // 旧版只保存一组 meleeStandHex/meleeAttackHex；首次加载时迁移为序列第 1 组。
    if (count == 0
        && CellControl_HexAdjacent(target->meleeAttackHex, target->meleeStandHex)) {
        stands[0] = target->meleeStandHex;
        attacks[0] = target->meleeAttackHex;
        count = 1;
    }

    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i) {
        target->meleeStandHexes[i] = (i < count) ? stands[i] : -1;
        target->meleeAttackHexes[i] = (i < count) ? attacks[i] : -1;
    }
    target->meleePairCount = static_cast<int8_t>(count);
    if (count == 0) {
        target->meleeStandHex = -1;
        target->meleeAttackHex = -1;
    } else {
        target->meleeStandHex = target->meleeStandHexes[0];
        target->meleeAttackHex = target->meleeAttackHexes[0];
    }
}

// 删除循环施法序列中的第 index 项并压紧。
static bool CellControl_RemoveSpellSlot(AutoStackRule* rule, int index)
{
    if (!rule || index < 0 || index >= rule->spellSlotCount) return false;
    for (int i = index; i + 1 < rule->spellSlotCount; ++i)
        rule->spellSlots[i] = rule->spellSlots[i + 1];
    rule->spellSlots[rule->spellSlotCount - 1] = -1;
    --rule->spellSlotCount;
    CellControl_NormalizeSpellSlots(rule);
    return true;
}

// 删除循环移动路径点中的第 index 项并压紧。
static bool CellControl_RemoveMoveWaypoint(AutoTargetRule* target, int index)
{
    if (!target || index < 0 || index >= target->moveWaypointCount) return false;
    for (int i = index; i + 1 < target->moveWaypointCount; ++i)
        target->moveWaypoints[i] = target->moveWaypoints[i + 1];
    target->moveWaypoints[target->moveWaypointCount - 1] = -1;
    --target->moveWaypointCount;
    return true;
}

// 删除循环近战组合中的第 index 项并压紧。
static bool CellControl_RemoveMeleePair(AutoTargetRule* target, int index)
{
    if (!target || index < 0 || index >= target->meleePairCount) return false;
    for (int i = index; i + 1 < target->meleePairCount; ++i) {
        target->meleeStandHexes[i] = target->meleeStandHexes[i + 1];
        target->meleeAttackHexes[i] = target->meleeAttackHexes[i + 1];
    }
    target->meleeStandHexes[target->meleePairCount - 1] = -1;
    target->meleeAttackHexes[target->meleePairCount - 1] = -1;
    --target->meleePairCount;
    CellControl_NormalizeMeleePairs(target);
    return true;
}

// 全战场有效坐标枚举（行优先，A01..K15），返回数量（<=165）。
static int CellControl_AllHexes(int out[165])
{
    int n = 0;
    for (int row = 0; row <= 10; ++row)
        for (int col = 1; col <= 15; ++col)
            out[n++] = row * 17 + col;
    return n;
}

// 根据单位类型填充可选行动列表，返回数量。
// has_artillery：炮术（弩车/箭塔）；has_first_aid：急救术（帐篷）。
static int CellControl_GetAllowedActions(int creature_type, bool is_ranged,
    bool has_artillery, bool has_first_aid, AutoActionKind out_actions[AA_COUNT])
{
    int n = 0;
    auto push = [&](AutoActionKind a) {
        if (n < AA_COUNT) out_actions[n++] = a;
    };

    switch (creature_type) {
    case eCreature::FIRST_AID_TENT:
        // 对齐弩车：有急救术 → 手动（默认）+ 急救；无 → 仅急救治疗。
        if (has_first_aid)
            push(AA_MANUAL);
        push(AA_FIRST_AID);
        break;
    case eCreature::CATAPULT:
        // 投石车不做攻城目标自动化：仅手动 / 防御。
        push(AA_MANUAL);
        push(AA_DEFEND);
        break;
    case eCreature::BALLISTA:
    case eCreature::ARROW_TOWER:
        // 有炮术：可选手动（默认）+ 远程攻击；无炮术：仅远程攻击。
        if (has_artillery)
            push(AA_MANUAL);
        push(AA_RANGED_ATTACK);
        break;
    case eCreature::AMMO_CART:
        // 不应进入面板
        push(AA_MANUAL);
        break;
    default:
        push(AA_MANUAL);
        push(AA_DEFEND);
        push(AA_MOVE);
        push(AA_MELEE_ATTACK);
        if (is_ranged)
            push(AA_RANGED_ATTACK);
        break;
    }
    return n;
}

// 行动是否需要目标区
static bool CellControl_ActionNeedsTarget(AutoActionKind action)
{
    switch (action) {
    case AA_MOVE:
    case AA_MELEE_ATTACK:
    case AA_RANGED_ATTACK:
    case AA_FIRST_AID:
        return true;
    default:
        return false;
    }
}

// 行动是否使用「两格」UI（站立/攻击 或 位置1/位置2）。
// 近战：站立格 + 相邻攻击格（两格 UI）。
// 循环移动改用独立的路径点列表 UI（moveWaypoints），不走两格逻辑。
static bool CellControl_ActionUsesTwoHex(AutoActionKind action)
{
    return action == AA_MELEE_ATTACK;
}

// 行动是否显示降级复选框（仅普通部队的主动策略）
static bool CellControl_ActionShowsFallback(int creature_type, AutoActionKind action)
{
    if (CellControl_IsWarMachineType(creature_type))
        return false;
    return action != AA_MANUAL && action != AA_DEFEND && action != AA_WAIT;
}

// 行动默认目标规则
static AutoTargetRule CellControl_DefaultTargetForAction(AutoActionKind action)
{
    AutoTargetRule t = {};
    t.meleeStandHex = -1;
    t.meleeAttackHex = -1;
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i) t.moveWaypoints[i] = -1;
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i) {
        t.meleeStandHexes[i] = -1;
        t.meleeAttackHexes[i] = -1;
    }

    switch (action) {
    case AA_MOVE:
        // 循环移动：路径点列表；不走选择器菜单。
        t.kind = AT_POSITION;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        t.meleeStandHex = -1;
        t.meleeAttackHex = -1;
        break;
    case AA_MELEE_ATTACK:
        // 循环近战：站立格 + 攻击格；不走选择器菜单。
        t.kind = AT_POSITION;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        t.meleeStandHex = -1;
        t.meleeAttackHex = -1;
        break;
    case AA_RANGED_ATTACK:
        t.kind = AT_STACK;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        break;
    case AA_FIRST_AID:
        t.kind = AT_STACK;
        t.side = ATS_OWN;
        t.selector = SEL_WOUND_VALUE; // 急救默认失血数值最大
        break;
    default:
        t.kind = AT_NONE;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        break;
    }
    return t;
}

// 前向声明：选择器可选集（实现见后）
static int CellControl_GetAllowedSelectors(AutoActionKind action, AutoTargetKind kind,
    AutoTargetSelector out_sels[SEL_COUNT]);

// 行动改变时规范化规则
static void CellControl_NormalizeRule(AutoStackRule* rule, int creature_type,
    bool is_ranged, bool has_artillery, bool has_first_aid)
{
    if (!rule) return;

    AutoActionKind allowed[AA_COUNT] = {};
    const int n = CellControl_GetAllowedActions(creature_type, is_ranged,
        has_artillery, has_first_aid, allowed);
    bool ok = false;
    for (int i = 0; i < n; ++i) {
        if (allowed[i] == rule->action) { ok = true; break; }
    }
    // 非法行动吸附到允许集首项：
    // 弩车/箭塔：有炮术首项=手动，无=远程；帐篷：有急救术首项=手动，无=急救。
    if (!ok)
        rule->action = (n > 0) ? allowed[0] : AA_MANUAL;

    // 循环施法与行动策略正交：即使当前行动是手动/防御，也要完成旧单槽迁移。
    CellControl_NormalizeSpellSlots(rule);

    if (!CellControl_ActionNeedsTarget(rule->action)) {
        rule->target = CellControl_DefaultTargetForAction(AA_MANUAL);
        rule->allowDefendFallback = false;
        return;
    }

    // 目标默认值：若当前目标不合法则重置
    const AutoTargetRule def = CellControl_DefaultTargetForAction(rule->action);
    if (rule->target.kind == AT_NONE)
        rule->target = def;

    switch (rule->action) {
    case AA_MELEE_ATTACK:
        rule->target.kind = AT_POSITION;
        rule->target.side = ATS_ENEMY;
        rule->target.selector = SEL_RANDOM;
        CellControl_NormalizeMeleePairs(&rule->target);
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
        // 循环移动：路径点列表，不走选择器菜单。
        rule->target.kind = AT_POSITION;
        rule->target.selector = SEL_RANDOM;
        if (rule->target.meleeStandHex < 1 || rule->target.meleeStandHex > 185)
            rule->target.meleeStandHex = -1;
        if (rule->target.meleeAttackHex < 1 || rule->target.meleeAttackHex > 185)
            rule->target.meleeAttackHex = -1;
        break;
    default:
        break;
    }

    // 收敛选择器到当前允许集合（近战/移动不显示选择器菜单）。
    if (!CellControl_ActionUsesTwoHex(rule->action)) {
        AutoTargetSelector sels[SEL_COUNT] = {};
        const int sn = CellControl_GetAllowedSelectors(rule->action,
            rule->target.kind, sels);
        bool sel_ok = false;
        for (int i = 0; i < sn; ++i)
            if (sels[i] == rule->target.selector) { sel_ok = true; break; }
        if (!sel_ok && sn > 0)
            rule->target.selector = sels[0];
    }

    if (!CellControl_ActionShowsFallback(creature_type, rule->action))
        rule->allowDefendFallback = false;
}

// 填充选择器选项
static int CellControl_GetAllowedSelectors(AutoActionKind action, AutoTargetKind kind,
    AutoTargetSelector out_sels[SEL_COUNT])
{
    int n = 0;
    auto push = [&](AutoTargetSelector s) {
        if (n < SEL_COUNT) out_sels[n++] = s;
    };

    if (CellControl_ActionUsesTwoHex(action)) {
        // 近战 / 循环移动：不用选择器菜单
        return n;
    }
    if (action == AA_FIRST_AID) {
        // 急救：失血数值 / 失血比例 / 随机（候选仅己方伤员）
        push(SEL_WOUND_VALUE);
        push(SEL_WOUND_RATIO);
        push(SEL_RANDOM);
        return n;
    }
    // 远程等：随机 / 远程高速优先 / 数量最多
    push(SEL_RANDOM);
    push(SEL_RANGED_SPEED);
    push(SEL_COUNT_HIGH);
    return n;
}

// 目标第二行标签（阵营固定时显示说明）
static const char* CellControl_FixedSideHint(AutoActionKind action)
{
    switch (action) {
    case AA_MELEE_ATTACK:
        return "循环站立位+攻击位";
    case AA_RANGED_ATTACK:
        return nullptr; // 远程攻击不显示固定说明
    case AA_FIRST_AID:
        return "己方伤员";
    case AA_MOVE:
        return nullptr; // 用阵营下拉
    default:
        return "无目标";
    }
}

// ========================================================================
// 公共 API
// ========================================================================

static void CellControl_Init(CellControl* ctrl)
{
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->data.rule = MakeDefaultRule_();
    ctrl->data.position = -1;
    ctrl->data.count_alive = 0;
    ctrl->data.creature_def = nullptr;
    ctrl->data.creature_type = -1;
    ctrl->data.army_slot_ix = -1;
    ctrl->expanded = CEX_NONE;
    ctrl->hover_item = -1;
    ctrl->melee_pair_pick_request = 0;
    ctrl->move_path_pick_request = 0;
    ctrl->spell_pick_request = 0;
}

static void CellControl_Destroy(CellControl* ctrl)
{
    if (ctrl->buffer) {
        ctrl->buffer->Destroy();
        ctrl->buffer = nullptr;
    }
    ctrl->dirty = true;
}

static void CellControl_EnsureMeleeDefaults(CellControl* ctrl)
{
    if (!ctrl || !ctrl->has_data) return;
    if (!CellControl_ActionUsesTwoHex(ctrl->data.rule.action)) return;
    CellControl_NormalizeMeleePairs(&ctrl->data.rule.target);
}

static void CellControl_SetData(CellControl* ctrl, const CellData* data)
{
    if (!ctrl || !data) return;
    ctrl->data = *data;
    ctrl->def_cache = data->creature_def;
    ctrl->has_data = true;
    // is_ranged 已由调用方（OpenSettingsPanel_ / SetData 前）按 stack.info.shooter 精确填充；
    // 若未填充（为 false）且类型为战争机器，用类型兵底。
    if (!ctrl->data.is_ranged)
        ctrl->data.is_ranged = CellControl_IsRangedType(ctrl->data.creature_type);
    CellControl_NormalizeRule(&ctrl->data.rule, ctrl->data.creature_type,
        ctrl->data.is_ranged, ctrl->data.has_artillery, ctrl->data.has_first_aid);
    CellControl_EnsureMeleeDefaults(ctrl);
    ctrl->expanded = CEX_NONE;
    ctrl->hover_item = -1;
    ctrl->spell_pick_request = 0;
    ctrl->dd_scroll = 0;
    ctrl->dirty = true;
}

static RECT CellControl_GetCellRect(int cell_panel_x, int cell_panel_y)
{
    RECT rc = {};
    rc.left = cell_panel_x;
    rc.top  = cell_panel_y;
    rc.right  = rc.left + CC_CELL_W;
    rc.bottom = rc.top  + CC_CELL_H;
    return rc;
}

// 前向声明：展开项数（实现见后）
static int CellControl_GetExpandedItemCount(CellControl* ctrl);

// 三小列布局下每个下拉所在小列 X（相对格子）。
// 行动/站立 = 小列2；选择器/攻击 = 小列3。
static int CellControl_GetExpandListLeftX(CellControl* ctrl)
{
    if (!ctrl) return CC_COL2_X;
    switch (ctrl->expanded) {
    case CEX_ACTION:   return CC_COL2_X;
    case CEX_STAND:    return CC_COL3_X;
    case CEX_SELECTOR: return CC_COL3_X;
    case CEX_ATTACK:   return CC_COL3_X;
    default:           return CC_COL2_X;
    }
}

// 展开列表宽度：行动=第二列，循环施法=槽宽，其余=第三列
static int CellControl_GetExpandListWidth(CellControl* ctrl)
{
    if (!ctrl) return CC_COL3_W;
    if (ctrl->expanded == CEX_SPELL) return CC_SPELL_SLOT_W;
    if (ctrl->expanded == CEX_ACTION) return CC_COL2_W;
    return CC_COL3_W;
}

// 当前展开列表的局部 Y（列表顶部，相对格子）= 其所在行下边缘。
// 循环施法=顶行；行动/选择器=中行；攻击=下行。
static int CellControl_GetExpandListTopY(CellControl* ctrl)
{
    if (!ctrl) return CC_TOP_Y + CC_ROW_H;
    switch (ctrl->expanded) {
    case CEX_ACTION:   return CC_TOP_Y + CC_ROW_H;
    case CEX_SELECTOR: return CC_TOP_Y + CC_ROW_H;
    case CEX_STAND:    return CC_TOP_Y + CC_ROW_H;
    case CEX_ATTACK:   return CC_BOT_Y + CC_ROW_H;
    case CEX_SPELL:    return CC_SPELL_Y + CC_ROW_H;
    default:           return CC_TOP_Y + CC_ROW_H;
    }
}

// 展开列表矩形（按展开类型决定小列 X + 行 Y；高度按可见项数）
static bool CellControl_GetExpandRectForCtrl(CellControl* ctrl,
    int cell_panel_x, int cell_panel_y, RECT* out_rc)
{
    if (!out_rc || !ctrl || ctrl->expanded == CEX_NONE) return false;
    const int item_count = CellControl_GetExpandedItemCount(ctrl);
    if (item_count <= 0) return false;
    out_rc->left   = cell_panel_x + CellControl_GetExpandListLeftX(ctrl);
    out_rc->top    = cell_panel_y + CellControl_GetExpandListTopY(ctrl);
    out_rc->right  = out_rc->left + CellControl_GetExpandListWidth(ctrl);
    out_rc->bottom = out_rc->top + item_count * CC_DROPDOWN_ITEM_H;
    return true;
}

// ========================================================================
// 内部绘制工具
// ========================================================================

static H3LoadedPcx16* CellControl_EnsureBuffer(CellControl* ctrl)
{
    if (!ctrl->buffer || ctrl->buffer->width != CC_CELL_W
        || ctrl->buffer->height != CC_CELL_H)
    {
        if (ctrl->buffer) ctrl->buffer->Destroy();
        ctrl->buffer = H3LoadedPcx16::Create(CC_CELL_W, CC_CELL_H);
    }
    return ctrl->buffer;
}

static void CellControl_FormatPosition(char* buf, int bufsize, int hex)
{
    if (hex < 1 || hex > 185) {
        strncpy(buf, "--", bufsize - 1);
        buf[bufsize - 1] = 0;
        return;
    }
    const int row = hex / 17;
    const int col = hex % 17;
    if (row < 0 || row > 10 || col < 1 || col > 15) {
        strncpy(buf, "--", bufsize - 1);
        buf[bufsize - 1] = 0;
        return;
    }
    _snprintf(buf, bufsize, "%c%02d", 'A' + row, col);
}

// 格式化单个循环移动路径点（A01 样式）。
static void CellControl_FormatWaypoint(char* buf, int bufsize,
    const AutoTargetRule& target, int index)
{
    if (!buf || bufsize <= 0) return;
    buf[0] = 0;
    if (index < 0 || index >= target.moveWaypointCount
        || index >= MOVE_WAYPOINT_CAPACITY) {
        strncpy(buf, "+", bufsize - 1);
        buf[bufsize - 1] = 0;
        return;
    }
    CellControl_FormatPosition(buf, bufsize, target.moveWaypoints[index]);
}

static void CellControl_FormatMeleePair(char* buf, int bufsize,
    const AutoTargetRule& target, int index)
{
    if (!buf || bufsize <= 0) return;
    buf[0] = 0;
    if (index < 0 || index >= target.meleePairCount
        || index >= MELEE_PAIR_CAPACITY) {
        strncpy(buf, "+", bufsize - 1);
        buf[bufsize - 1] = 0;
        return;
    }
    char stand_pos[8] = {};
    char attack_pos[8] = {};
    CellControl_FormatPosition(stand_pos, sizeof(stand_pos),
        target.meleeStandHexes[index]);
    CellControl_FormatPosition(attack_pos, sizeof(attack_pos),
        target.meleeAttackHexes[index]);
    _snprintf(buf, bufsize, "%s→%s", stand_pos, attack_pos);
    buf[bufsize - 1] = 0;
}

static void CellControl_DrawText(H3LoadedPcx16* scr, H3Font* fnt,
    const char* text, int x, int y, int w, int h, INT32 color,
    eTextAlignment align = eTextAlignment::MIDDLE_CENTER)
{
    if (!fnt || !text || w <= 0 || h <= 0) return;

    bool ascii = true;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p >= 0x80) { ascii = false; break; }
    }
    if (ascii) {
        scr->TextDraw(fnt, text, x, y, w, h, (eTextColor)color, align);
        return;
    }

    wchar_t wide[256] = {};
    char gbk[512] = {};
    const int wide_len = MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, _countof(wide));
    if (wide_len > 0
        && WideCharToMultiByte(936, 0, wide, -1, gbk, sizeof(gbk), nullptr, nullptr) > 0)
    {
        scr->TextDraw(fnt, gbk, x, y, w, h, (eTextColor)color, align);
    } else {
        scr->TextDraw(fnt, text, x, y, w, h, (eTextColor)color, align);
    }
}

static void CellControl_DrawButtonBg(H3LoadedPcx16* scr, int x, int y, int w, int h,
    bool pressed, bool hovered)
{
    if (w <= 0 || h <= 0) return;
    BYTE r, g, b;
    if (pressed) {
        r = 104; g = 70; b = 28;
    } else if (hovered) {
        r = 210; g = 170; b = 72;
    } else {
        r = 74; g = 52; b = 24;
    }
    Fill(scr, x, y, w, h, r, g, b);
    scr->DrawFrame(x, y, w, h, pressed ? (BYTE)168 : (BYTE)210,
        pressed ? (BYTE)141 : (BYTE)170, pressed ? (BYTE)68 : (BYTE)72);
}

static void CellControl_DrawArrow(H3LoadedPcx16* scr, int x, int y, bool down)
{
    if (down) {
        Fill(scr, x,     y,     7, 1, 235, 205, 116);
        Fill(scr, x + 1, y + 1, 5, 1, 235, 205, 116);
        Fill(scr, x + 2, y + 2, 3, 1, 235, 205, 116);
        Fill(scr, x + 3, y + 3, 1, 1, 235, 205, 116);
    } else {
        Fill(scr, x + 3, y,     1, 1, 235, 205, 116);
        Fill(scr, x + 2, y + 1, 3, 1, 235, 205, 116);
        Fill(scr, x + 1, y + 2, 5, 1, 235, 205, 116);
        Fill(scr, x,     y + 3, 7, 1, 235, 205, 116);
    }
}

// 方块「＋」拾取按钮：金框凹槽 + 居中加号。active=拾取进行中（高亮）。
static void CellControl_DrawPlusButton(H3LoadedPcx16* scr, int x, int y, int size,
    bool active)
{
    if (size <= 0) return;
    BYTE bg_r = active ? 210 : 74, bg_g = active ? 170 : 52, bg_b = active ? 72 : 24;
    Fill(scr, x, y, size, size, bg_r, bg_g, bg_b);
    scr->DrawFrame(x, y, size, size, (BYTE)210, (BYTE)170, (BYTE)72);
    // 加号：横竖各一条，居中
    const int cx = x + size / 2;
    const int cy = y + size / 2;
    const int arm = size / 2 - 3;
    if (arm > 0) {
        Fill(scr, cx - arm, cy, arm * 2 + 1, 2, 235, 205, 116); // 横
        Fill(scr, cx, cy - arm, 2, arm * 2 + 1, 235, 205, 116); // 竖
    }
}

static void CellControl_ClearBuffer(H3LoadedPcx16* scr)
{
    const bool mode_32 = H3BitMode::Get() == 4;
    if (mode_32) {
        DWORD* row = reinterpret_cast<DWORD*>(scr->buffer);
        for (int i = 0; i < scr->width * CC_CELL_H; ++i)
            row[i] = 0xFF00FFFFu;
    } else {
        WORD* row = reinterpret_cast<WORD*>(scr->buffer);
        for (int i = 0; i < scr->width * CC_CELL_H; ++i)
            row[i] = 0x7FDF;
    }
}

static H3LoadedDef* CellControl_GetPortraitDef()
{
    static H3LoadedDef* s_portrait = nullptr;
    static bool s_tried = false;
    if (!s_tried) {
        s_tried = true;
        s_portrait = H3LoadedDef::Load("TwCrPort.def");
    }
    return s_portrait;
}

static void CellControl_DrawCreatureIcon(CellControl* ctrl, H3LoadedPcx16* scr)
{
    if (ctrl->data.creature_type < 0) return;
    H3LoadedDef* def = CellControl_GetPortraitDef();
    if (!def) return;
    const int group = 0;
    const int frame = ctrl->data.creature_type + 2;
    if (!def->groups || def->groupsCount <= group) return;
    if (!def->activeGroups || !def->activeGroups[group]) return;
    H3LoadedDef::DefGroup* grp = def->groups[group];
    if (!grp || grp->count <= frame || !grp->frames) return;
    if (!grp->frames[frame]) return;
    __try {
        Fill(scr, CC_ICON_X, CC_ICON_Y, CC_ICON_W, CC_ICON_H, 0, 0, 0);
        def->DrawToPcx16(group, frame, scr, CC_ICON_X, CC_ICON_Y);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    H3LoadedPcx16* frameImg = LoadPanelPcx24_("HA_icon_frame.pcx",
        CC_ICON_FRAME_W, CC_ICON_FRAME_H, s_cc_icon_frame, s_cc_icon_frame_load_failed);
    if (frameImg)
        DrawTransparentPcx_(frameImg, scr, CC_ICON_X - 1, CC_ICON_Y - 1);
}

// ========================================================================
// 绘制：收起状态
// ========================================================================

static void CellControl_DrawCollapsed(CellControl* ctrl)
{
    if (!ctrl || !ctrl->has_data) return;
    H3LoadedPcx16* scr = ctrl->buffer;
    if (!scr || scr->width != CC_CELL_W || scr->height != CC_CELL_H) {
        if (scr) scr->Destroy();
        ctrl->buffer = H3LoadedPcx16::Create(CC_CELL_W, CC_CELL_H);
        scr = ctrl->buffer;
    }
    if (!scr || !scr->buffer) return;

    CellControl_ClearBuffer(scr);
    H3Font* fntS = GetSmallFont();
    if (!fntS) return;

    CellControl_DrawCreatureIcon(ctrl, scr);

    const AutoStackRule& rule = ctrl->data.rule;
    const bool needs_target = CellControl_ActionNeedsTarget(rule.action);
    const bool shows_fallback = CellControl_ActionShowsFallback(
        ctrl->data.creature_type, rule.action);

    // ---- 行动下拉 ----
    const char* action_label =
        (rule.action >= 0 && rule.action < AA_COUNT && g_action_labels[rule.action])
        ? g_action_labels[rule.action] : "---";
    CellControl_DrawButtonBg(scr, CC_COMBO_X, CC_ROW1_Y, CC_COMBO_W, CC_ROW_H,
        ctrl->action_pressed, false);
    CellControl_DrawText(scr, fntS, action_label,
        CC_COMBO_X + 4, CC_ROW1_Y, CC_COMBO_W - 20, CC_ROW_H,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
    CellControl_DrawArrow(scr, CC_COMBO_X + CC_COMBO_W - 14,
        CC_ROW1_Y + CC_ROW_H / 2 - 3, ctrl->expanded != CEX_ACTION);

    // 小列2 上行 = 行动下拉（已画于上）。
    // 小列2 下行 = 降级复选框（普通部队主动行动）；近战时下行 = 站立格。
    // 小列3 上行 = 选择器 / 近战攻击格；下行 = 阵营 / 固定说明。
    if (needs_target) {
        if (rule.action == AA_MOVE) {
            // 循环移动：两行槽位，每槽一个路径点；槽宽按 A01 文本定，
            // 一行能摆几个就几个（当前 8×2=16）。
            const int gap = CC_PATH_SLOT_GAP;
            const int slot_w = CC_MOVE_SLOT_W;
            const int per_row = CC_MOVE_SLOTS_PER_ROW;
            const int count = rule.target.moveWaypointCount;
            for (int index = 0; index < MOVE_WAYPOINT_CAPACITY; ++index) {
                const int row = index / per_row;
                const int col = index % per_row;
                if (row > 1) break;
                const int x = CC_COL3_X + col * (slot_w + gap);
                const int y = (row == 0) ? CC_TOP_Y : CC_BOT_Y;
                const bool existing = index < count;
                const bool add_slot = index == count && count < MOVE_WAYPOINT_CAPACITY;
                if (!existing && !add_slot) continue;

                const bool requested = ctrl->move_path_pick_request == index + 1;
                CellControl_DrawButtonBg(scr, x, y, slot_w, CC_ROW_H,
                    requested, false);
                if (add_slot) {
                    CellControl_DrawPlusButton(scr,
                        x + (slot_w - 16) / 2, y + (CC_ROW_H - 16) / 2,
                        16, requested);
                } else {
                    char wp_buf[12] = {};
                    CellControl_FormatWaypoint(wp_buf, sizeof(wp_buf),
                        rule.target, index);
                    CellControl_DrawText(scr, fntS, wp_buf,
                        x + 1, y, slot_w - 2, CC_ROW_H,
                        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
                }
            }
        } else if (CellControl_ActionUsesTwoHex(rule.action)) {
            // 循环近战：两行槽位，显示“站立位→攻击位”；槽宽按文本定，
            // 一行能摆几个就几个（当前 5×2=10）。
            const int gap = CC_PATH_SLOT_GAP;
            const int slot_w = CC_MELEE_SLOT_W;
            const int per_row = CC_MELEE_SLOTS_PER_ROW;
            const int count = rule.target.meleePairCount;
            for (int index = 0; index < MELEE_PAIR_CAPACITY; ++index) {
                const int row = index / per_row;
                const int col = index % per_row;
                if (row > 1) break;
                const int x = CC_COL3_X + col * (slot_w + gap);
                const int y = (row == 0) ? CC_TOP_Y : CC_BOT_Y;
                const bool existing = index < count;
                const bool add_slot = index == count && count < MELEE_PAIR_CAPACITY;
                const bool visible = existing || add_slot;
                if (!visible) continue;

                const bool requested = ctrl->melee_pair_pick_request == index + 1;
                CellControl_DrawButtonBg(scr, x, y, slot_w, CC_ROW_H,
                    requested, false);
                if (add_slot) {
                    CellControl_DrawPlusButton(scr,
                        x + (slot_w - 16) / 2, y + (CC_ROW_H - 16) / 2,
                        16, requested);
                } else {
                    char pair_buf[24] = {};
                    CellControl_FormatMeleePair(pair_buf, sizeof(pair_buf),
                        rule.target, index);
                    CellControl_DrawText(scr, fntS, pair_buf,
                        x + 1, y, slot_w - 2, CC_ROW_H,
                        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
                }
            }
        } else {
            // 选择器：小列3上行
            const char* sel_label =
                (rule.target.selector < SEL_COUNT && g_selector_labels[rule.target.selector])
                ? g_selector_labels[rule.target.selector] : "---";
            CellControl_DrawButtonBg(scr, CC_COL3_X, CC_TOP_Y, CC_COL_W, CC_ROW_H,
                ctrl->selector_pressed, false);
            CellControl_DrawText(scr, fntS, sel_label,
                CC_COL3_X + 4, CC_TOP_Y, CC_COL_W - 20, CC_ROW_H,
                (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
            CellControl_DrawArrow(scr, CC_COL3_X + CC_COL_W - 14,
                CC_TOP_Y + CC_ROW_H / 2 - 3, ctrl->expanded != CEX_SELECTOR);

            // 固定目标说明：小列3下行
            const char* hint = CellControl_FixedSideHint(rule.action);
            if (hint) {
                Fill(scr, CC_COL3_X, CC_BOT_Y, CC_COL_W, CC_ROW_H, 48, 36, 20);
                scr->DrawFrame(CC_COL3_X, CC_BOT_Y, CC_COL_W, CC_ROW_H,
                    (BYTE)120, (BYTE)96, (BYTE)48);
                CellControl_DrawText(scr, fntS, hint,
                    CC_COL3_X + 4, CC_BOT_Y, CC_COL_W - 8, CC_ROW_H,
                    (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
            }
        }
    }

    // ---- 降级复选框：小列2下行（近战与非近战统一）----
    if (shows_fallback) {
        const int cb_x = CC_COL2_X + 2;
        const int cb_y = CC_BOT_Y + 3;
        const int cb_box = 10;
        Fill(scr, cb_x, cb_y, cb_box, cb_box, 40, 28, 12);
        scr->DrawFrame(cb_x, cb_y, cb_box, cb_box,
            (BYTE)184, (BYTE)139, (BYTE)62);
        if (rule.allowDefendFallback) {
            Fill(scr, cb_x + 2, cb_y + 4, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 3, cb_y + 5, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 4, cb_y + 6, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 5, cb_y + 5, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 6, cb_y + 4, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 7, cb_y + 3, 2, 2, 235, 205, 116);
        }
        CellControl_DrawText(scr, fntS, "允许降级为防御",
            cb_x + cb_box + 4, cb_y - 1, CC_COL2_W - cb_box - 8, CC_CHECKBOX_H,
            (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
    }

    // ---- 顶部：行动前循环施法（标签 + 单行最多 SPELL_SLOT_CAPACITY 个快捷键槽）----
    {
        CellControl_DrawText(scr, fntS, "行动前循环施法:",
            CC_COL2_X, CC_SPELL_Y, CC_SPELL_LABEL_W, CC_ROW_H,
            (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);

        const int count = rule.spellSlotCount;
        for (int index = 0; index < SPELL_SLOT_CAPACITY; ++index) {
            const bool existing = index < count;
            const bool add_slot = index == count && count < SPELL_SLOT_CAPACITY;
            if (!existing && !add_slot) continue;

            const int x = CC_COL3_X
                + index * (CC_SPELL_SLOT_W + CC_SPELL_SLOT_GAP);
            const bool requested = ctrl->spell_pick_request == index + 1;
            CellControl_DrawButtonBg(scr, x, CC_SPELL_Y, CC_SPELL_SLOT_W, CC_ROW_H,
                requested, false);
            if (add_slot) {
                CellControl_DrawPlusButton(scr,
                    x + (CC_SPELL_SLOT_W - 16) / 2,
                    CC_SPELL_Y + (CC_ROW_H - 16) / 2,
                    16, requested);
            } else {
                char slot_buf[8] = {};
                _snprintf(slot_buf, sizeof(slot_buf), "%d",
                    (int)rule.spellSlots[index]);
                CellControl_DrawText(scr, fntS, slot_buf,
                    x + 2, CC_SPELL_Y, CC_SPELL_SLOT_W - 4, CC_ROW_H,
                    (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
            }
        }
    }

    // ---- 位置/数量：第一小列左下角向上排（位置在下、数量在上），明确左对齐 ----
    // 放在图标列下方/旁侧左缘，避免宽列 + TextDraw 看起来像右对齐。
    const int meta_x = CC_ICON_X;
    const int meta_w = CC_ICON_W;
    const int pos_y   = CC_CELL_H - CC_LABEL_H - 4; // 位置：贴格子下边缘
    const int count_y = pos_y - CC_LABEL_H - 1;     // 数量：位置上方

    char pos_buf[8] = {};
    CellControl_FormatPosition(pos_buf, sizeof(pos_buf), ctrl->data.position);
    CellControl_DrawText(scr, fntS, pos_buf,
        meta_x, pos_y, meta_w, CC_LABEL_H,
        (INT32)eTextColor::LIGHT_GREEN, eTextAlignment::HLEFT);

    char count_buf[16] = {};
    if (ctrl->data.count_alive > 0)
        _snprintf(count_buf, sizeof(count_buf), "%d", ctrl->data.count_alive);
    else
        strncpy(count_buf, "--", sizeof(count_buf) - 1);
    CellControl_DrawText(scr, fntS, count_buf,
        meta_x, count_y, meta_w, CC_LABEL_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_RIGHT);

    ctrl->dirty = false;
}

// ========================================================================
// 绘制：展开列表
// ========================================================================

static void CellControl_DrawDropdownItem(H3LoadedPcx16* scr, H3Font* fnt,
    const char* label, int item_index,
    int cell_panel_x, int cell_panel_y,
    bool is_selected, bool is_hovered, bool cool_theme,
    int list_top_y, int list_left_x, int item_w_override = -1)
{
    int item_x = cell_panel_x + list_left_x;
    int item_y = cell_panel_y + list_top_y + item_index * CC_DROPDOWN_ITEM_H;
    int item_w = (item_w_override > 0) ? item_w_override : CC_COL_W;
    int item_h = CC_DROPDOWN_ITEM_H;

    BYTE bg_r, bg_g, bg_b, frame_r, frame_g, frame_b;
    INT32 text_color;
    if (!cool_theme) {
        if (is_hovered) {
            bg_r = 184; bg_g = 136; bg_b = 48;
            frame_r = 246; frame_g = 214; frame_b = 116;
            text_color = (INT32)eTextColor::WHITE;
        } else if (is_selected) {
            bg_r = 136; bg_g = 88; bg_b = 24;
            frame_r = 232; frame_g = 184; frame_b = 76;
            text_color = (INT32)eTextColor::WHITE;
        } else {
            bg_r = 68; bg_g = 42; bg_b = 18;
            frame_r = 166; frame_g = 112; frame_b = 40;
            text_color = (INT32)eTextColor::YELLOW;
        }
    } else {
        if (is_hovered) {
            bg_r = 66; bg_g = 154; bg_b = 170;
            frame_r = 150; frame_g = 238; frame_b = 244;
            text_color = (INT32)eTextColor::WHITE;
        } else if (is_selected) {
            bg_r = 34; bg_g = 90; bg_b = 110;
            frame_r = 82; frame_g = 190; frame_b = 204;
            text_color = (INT32)eTextColor::CYAN;
        } else {
            bg_r = 25; bg_g = 42; bg_b = 50;
            frame_r = 62; frame_g = 116; frame_b = 128;
            text_color = (INT32)eTextColor::CYAN2;
        }
    }
    Fill(scr, item_x, item_y, item_w, item_h, bg_r, bg_g, bg_b);
    scr->DrawFrame(item_x, item_y, item_w, item_h, frame_r, frame_g, frame_b);
    CellControl_DrawText(scr, fnt, label,
        item_x + 4, item_y, item_w - 20, item_h,
        text_color, eTextAlignment::MIDDLE_LEFT);
}

// 展开行动列表
static void CellControl_DrawActionDropdownTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y, int hover_idx)
{
    if (!ctrl || ctrl->expanded != CEX_ACTION || !scr) return;
    H3Font* fntS = GetSmallFont();
    if (!fntS) return;

    AutoActionKind allowed[AA_COUNT] = {};
    const int n = CellControl_GetAllowedActions(ctrl->data.creature_type,
        ctrl->data.is_ranged, ctrl->data.has_artillery, ctrl->data.has_first_aid,
        allowed);
    for (int i = 0; i < n; ++i) {
        const AutoActionKind a = allowed[i];
        const char* label = (a < AA_COUNT && g_action_labels[a]) ? g_action_labels[a] : "---";
        CellControl_DrawDropdownItem(scr, fntS, label, i,
            cell_panel_x, cell_panel_y,
            a == ctrl->data.rule.action, i == hover_idx, false,
            CC_TOP_Y + CC_ROW_H, CC_COL2_X, CC_COL2_W);
    }
}

// 展开选择器列表
static void CellControl_DrawSelectorDropdownTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y, int hover_idx)
{
    if (!ctrl || ctrl->expanded != CEX_SELECTOR || !scr) return;
    if (!CellControl_ActionNeedsTarget(ctrl->data.rule.action)) return;
    H3Font* fntS = GetSmallFont();
    if (!fntS) return;

    AutoTargetSelector allowed[SEL_COUNT] = {};
    const int n = CellControl_GetAllowedSelectors(ctrl->data.rule.action,
        ctrl->data.rule.target.kind, allowed);
    for (int i = 0; i < n; ++i) {
        const AutoTargetSelector s = allowed[i];
        const char* label = (s < SEL_COUNT && g_selector_labels[s]) ? g_selector_labels[s] : "---";
        CellControl_DrawDropdownItem(scr, fntS, label, i,
            cell_panel_x, cell_panel_y,
            s == ctrl->data.rule.target.selector, i == hover_idx, true,
            CC_TOP_Y + CC_ROW_H, CC_COL3_X);
    }
}

// 近战 hex 下拉每次最多可见项数（超出用滚动）
static const int CC_DD_MAX_VISIBLE = 6;

// 构建当前近战下拉的 hex 候选列表（站立=全战场，攻击=站立格相邻）。
// 返回项数；下拉展开起点从第二行(CC_ROW2_Y)开始（站立），第三行(CC_ROW3_Y)开始（攻击）。
static int CellControl_BuildMeleeHexList(CellControl* ctrl, int out[165])
{
    if (!ctrl) return 0;
    if (ctrl->expanded == CEX_STAND)
        return CellControl_AllHexes(out);
    if (ctrl->expanded == CEX_ATTACK) {
        // 循环移动：第二点也是全战场任意格（无相邻约束）
        if (ctrl->data.rule.action == AA_MOVE)
            return CellControl_AllHexes(out);
        // 近战：攻击格必须是站立格的相邻格
        int stand = ctrl->data.rule.target.meleeStandHex;
        if (!CellControl_HexValid(stand))
            stand = ctrl->data.position; // 默认当前位置
        int nb[6];
        const int n = CellControl_HexNeighbors(stand, nb);
        for (int i = 0; i < n; ++i) out[i] = nb[i];
        return n;
    }
    return 0;
}

// 可滚动 hex 下拉绘制：从指定行 Y 下方展开，最多 CC_DD_MAX_VISIBLE 项，
// 带滚动偏移 ctrl->dd_scroll。cool_theme=true 用冷色。
static void CellControl_DrawHexDropdownTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y, int hover_idx, int col_x, int row_y, bool cool_theme)
{
    if (!ctrl || !scr) return;
    H3Font* fntS = GetSmallFont();
    if (!fntS) return;
    int hexes[165];
    const int n = CellControl_BuildMeleeHexList(ctrl, hexes);
    if (n <= 0) return;

    int scroll = ctrl->dd_scroll;
    if (scroll < 0) scroll = 0;
    const int max_scroll = (n > CC_DD_MAX_VISIBLE) ? (n - CC_DD_MAX_VISIBLE) : 0;
    if (scroll > max_scroll) scroll = max_scroll;

    const int cur_hex = (ctrl->expanded == CEX_STAND)
        ? ctrl->data.rule.target.meleeStandHex
        : ctrl->data.rule.target.meleeAttackHex;

    const int list_x = cell_panel_x + col_x;
    const int list_top = cell_panel_y + row_y + CC_ROW_H;
    const int list_w = (col_x == CC_COL2_X) ? CC_COL2_W : CC_COL3_W;
    const int vis = (n < CC_DD_MAX_VISIBLE) ? n : CC_DD_MAX_VISIBLE;
    for (int v = 0; v < vis; ++v) {
        const int i = scroll + v;
        if (i >= n) break;
        char buf[8] = {};
        CellControl_FormatPosition(buf, sizeof(buf), hexes[i]);
        const int item_x = list_x;
        const int item_y = list_top + v * CC_DROPDOWN_ITEM_H;
        const bool is_sel = (hexes[i] == cur_hex);
        const bool is_hov = (i == hover_idx);
        BYTE bg_r,bg_g,bg_b,fr,fg,fb; INT32 tc;
        if (cool_theme) {
            if (is_hov) { bg_r=66;bg_g=154;bg_b=170; fr=150;fg=238;fb=244; tc=(INT32)eTextColor::WHITE; }
            else if (is_sel) { bg_r=34;bg_g=90;bg_b=110; fr=82;fg=190;fb=204; tc=(INT32)eTextColor::CYAN; }
            else { bg_r=25;bg_g=42;bg_b=50; fr=62;fg=116;fb=128; tc=(INT32)eTextColor::CYAN2; }
        } else {
            if (is_hov) { bg_r=184;bg_g=136;bg_b=48; fr=246;fg=214;fb=116; tc=(INT32)eTextColor::WHITE; }
            else if (is_sel) { bg_r=136;bg_g=88;bg_b=24; fr=232;fg=184;fb=76; tc=(INT32)eTextColor::WHITE; }
            else { bg_r=68;bg_g=42;bg_b=18; fr=166;fg=112;fb=40; tc=(INT32)eTextColor::YELLOW; }
        }
        Fill(scr, item_x, item_y, list_w, CC_DROPDOWN_ITEM_H, bg_r, bg_g, bg_b);
        scr->DrawFrame(item_x, item_y, list_w, CC_DROPDOWN_ITEM_H, fr, fg, fb);
        CellControl_DrawText(scr, fntS, buf,
            item_x + 4, item_y, list_w - 20, CC_DROPDOWN_ITEM_H,
            tc, eTextAlignment::MIDDLE_LEFT);
        // 滚动指示：首/末可见项画箭头
        if (v == 0 && scroll > 0)
            CellControl_DrawArrow(scr, item_x + list_w - 12, item_y + 2, false);
        if (v == vis - 1 && scroll < max_scroll)
            CellControl_DrawArrow(scr, item_x + list_w - 12, item_y + CC_DROPDOWN_ITEM_H - 6, true);
    }
}

// 魔法槽位标签：索引 0..9 → "1".."9","0"
static const char* CellControl_SpellSlotLabel(int idx)
{
    static const char* k[10] = { "1","2","3","4","5","6","7","8","9","0" };
    return (idx >= 0 && idx < 10) ? k[idx] : "1";
}

// 魔法槽位值(1-9,0) → 下拉索引(0..9)
static int CellControl_SpellSlotToIndex(int slot)
{
    if (slot == 0) return 9;
    if (slot >= 1 && slot <= 9) return slot - 1;
    return 0;
}
// 下拉索引(0..9) → 魔法槽位值(1-9,0)
static int CellControl_IndexToSpellSlot(int idx)
{
    if (idx == 9) return 0;
    if (idx >= 0 && idx < 9) return idx + 1;
    return 1;
}

// 展开循环施法快捷键列表（10 项：1-9,0），从对应槽位下方展开
static void CellControl_DrawSpellDropdownTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y, int hover_idx)
{
    if (!ctrl || ctrl->expanded != CEX_SPELL || !scr) return;
    H3Font* fntS = GetSmallFont();
    if (!fntS) return;

    int slot_index = 0; // 旧下拉路径已废弃
    if (slot_index < 0) slot_index = 0;
    if (slot_index >= SPELL_SLOT_CAPACITY) slot_index = SPELL_SLOT_CAPACITY - 1;

    int current_slot = -1;
    if (slot_index < ctrl->data.rule.spellSlotCount)
        current_slot = ctrl->data.rule.spellSlots[slot_index];
    const int cur = (current_slot >= 0)
        ? CellControl_SpellSlotToIndex(current_slot) : -1;

    const int list_left = CellControl_GetExpandListLeftX(ctrl);
    const int list_top = CellControl_GetExpandListTopY(ctrl);
    const int item_w = CellControl_GetExpandListWidth(ctrl);
    for (int i = 0; i < 10; ++i) {
        CellControl_DrawDropdownItem(scr, fntS, CellControl_SpellSlotLabel(i), i,
            cell_panel_x, cell_panel_y,
            i == cur, i == hover_idx, false,
            list_top, list_left, item_w);
    }
}

// 统一绘制当前展开列表（面板层第二趟调用）
static void CellControl_DrawExpandedTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y, int hover_idx)
{
    if (!ctrl || !scr) return;
    switch (ctrl->expanded) {
    case CEX_ACTION:
        CellControl_DrawActionDropdownTo(ctrl, scr, cell_panel_x, cell_panel_y, hover_idx);
        break;
    case CEX_SPELL:
        CellControl_DrawSpellDropdownTo(ctrl, scr, cell_panel_x, cell_panel_y, hover_idx);
        break;
    case CEX_SELECTOR:
        CellControl_DrawSelectorDropdownTo(ctrl, scr, cell_panel_x, cell_panel_y, hover_idx);
        break;
    case CEX_STAND:
        CellControl_DrawHexDropdownTo(ctrl, scr, cell_panel_x, cell_panel_y, hover_idx, CC_COL3_X, CC_TOP_Y, true);
        break;
    case CEX_ATTACK:
        CellControl_DrawHexDropdownTo(ctrl, scr, cell_panel_x, cell_panel_y, hover_idx, CC_COL3_X, CC_BOT_Y, true);
        break;
    default:
        break;
    }
}

// 当前展开列表项数
static int CellControl_GetExpandedItemCount(CellControl* ctrl)
{
    if (!ctrl) return 0;
    switch (ctrl->expanded) {
    case CEX_ACTION: {
        AutoActionKind allowed[AA_COUNT] = {};
        return CellControl_GetAllowedActions(ctrl->data.creature_type,
            ctrl->data.is_ranged, ctrl->data.has_artillery,
            ctrl->data.has_first_aid, allowed);
    }
    case CEX_SELECTOR: {
        AutoTargetSelector allowed[SEL_COUNT] = {};
        return CellControl_GetAllowedSelectors(ctrl->data.rule.action,
            ctrl->data.rule.target.kind, allowed);
    }
    case CEX_STAND:
    case CEX_ATTACK: {
        int hexes[165];
        const int n = CellControl_BuildMeleeHexList(ctrl, hexes);
        return (n < CC_DD_MAX_VISIBLE) ? n : CC_DD_MAX_VISIBLE;
    }
    case CEX_SPELL:
        return 10;  // 魔法槽位 1-9,0
    default:
        return 0;
    }
}

// ========================================================================
// 命中测试
// ========================================================================

enum CellHitArea
{
    CELL_HIT_NONE = 0,
    CELL_HIT_ICON,
    CELL_HIT_ACTION,
    CELL_HIT_SELECTOR,
    CELL_HIT_CHECKBOX,
    CELL_HIT_DROP,       // 当前展开列表
    // 动态槽位：命中值 = BASE + index，容量由布局常量决定。
    CELL_HIT_MELEE_PAIR_BASE = 100,
    CELL_HIT_MOVE_WP_BASE    = 200,
    CELL_HIT_SPELL_BASE      = 300,
};

static CellHitArea CellControl_HitTestInCell(CellControl* ctrl, int local_x, int local_y)
{
    if (!ctrl) return CELL_HIT_NONE;
    const bool needs_target = CellControl_ActionNeedsTarget(ctrl->data.rule.action);
    const bool shows_fallback = CellControl_ActionShowsFallback(
        ctrl->data.creature_type, ctrl->data.rule.action);
    const bool melee = CellControl_ActionUsesTwoHex(ctrl->data.rule.action);

    auto in_box = [&](int x, int y, int w) -> bool {
        return local_x >= x && local_x < x + w
            && local_y >= y && local_y < y + CC_ROW_H;
    }; 

    // 展开列表优先（按当前展开的小列 X + 行 Y）
    if (ctrl->expanded != CEX_NONE) {
        const int lx = CellControl_GetExpandListLeftX(ctrl);
        if (local_x >= lx && local_x < lx + CellControl_GetExpandListWidth(ctrl)) {
            const int n = CellControl_GetExpandedItemCount(ctrl);
            const int list_top = CellControl_GetExpandListTopY(ctrl);
            const int rel_y = local_y - list_top;
            if (rel_y >= 0 && n > 0) {
                const int idx = rel_y / CC_DROPDOWN_ITEM_H;
                if (idx >= 0 && idx < n)
                    return CELL_HIT_DROP;
            }
        }
    }

    // 行动下拉：小列2 上行
    if (ctrl->expanded != CEX_ACTION && in_box(CC_COL2_X, CC_TOP_Y, CC_COL2_W))
        return CELL_HIT_ACTION;

    if (needs_target) {
        if (ctrl->data.rule.action == AA_MOVE) {
            // 循环移动：按布局反算的槽宽命中；只暴露已有项和末尾追加项。
            const int gap = CC_PATH_SLOT_GAP;
            const int slot_w = CC_MOVE_SLOT_W;
            const int per_row = CC_MOVE_SLOTS_PER_ROW;
            for (int index = 0; index < MOVE_WAYPOINT_CAPACITY; ++index) {
                const int row = index / per_row;
                const int col = index % per_row;
                if (row > 1) break;
                const int x = CC_COL3_X + col * (slot_w + gap);
                const int y = (row == 0) ? CC_TOP_Y : CC_BOT_Y;
                if (local_x >= x && local_x < x + slot_w
                    && local_y >= y && local_y < y + CC_ROW_H
                    && (index < ctrl->data.rule.target.moveWaypointCount
                        || (index == ctrl->data.rule.target.moveWaypointCount
                            && index < MOVE_WAYPOINT_CAPACITY)))
                {
                    return static_cast<CellHitArea>(CELL_HIT_MOVE_WP_BASE + index);
                }
            }
        } else if (melee) {
            // 循环近战：按布局反算的槽宽命中；只暴露已有项和末尾追加项。
            const int gap = CC_PATH_SLOT_GAP;
            const int slot_w = CC_MELEE_SLOT_W;
            const int per_row = CC_MELEE_SLOTS_PER_ROW;
            for (int index = 0; index < MELEE_PAIR_CAPACITY; ++index) {
                const int row = index / per_row;
                const int col = index % per_row;
                if (row > 1) break;
                const int x = CC_COL3_X + col * (slot_w + gap);
                const int y = (row == 0) ? CC_TOP_Y : CC_BOT_Y;
                if (local_x >= x && local_x < x + slot_w
                    && local_y >= y && local_y < y + CC_ROW_H
                    && (index < ctrl->data.rule.target.meleePairCount
                        || (index == ctrl->data.rule.target.meleePairCount
                            && index < MELEE_PAIR_CAPACITY)))
                {
                    return static_cast<CellHitArea>(CELL_HIT_MELEE_PAIR_BASE + index);
                }
            }
        } else {
            // 选择器：小列3上行
            if (ctrl->expanded != CEX_SELECTOR && in_box(CC_COL3_X, CC_TOP_Y, CC_COL3_W))
                return CELL_HIT_SELECTOR;
        }
    }

    // 降级复选框：统一在小列2下行（行动下拉正下方）
    if (shows_fallback) {
        if (local_x >= CC_COL2_X && local_x < CC_COL2_X + CC_COL2_W
            && local_y >= CC_BOT_Y && local_y < CC_BOT_Y + CC_ROW_H)
            return CELL_HIT_CHECKBOX;
    }

    // 顶部循环施法槽位：已有槽可展开重设，末尾「＋」追加。
    {
        const int count = ctrl->data.rule.spellSlotCount;
        for (int index = 0; index < SPELL_SLOT_CAPACITY; ++index) {
            const bool existing = index < count;
            const bool add_slot = index == count && count < SPELL_SLOT_CAPACITY;
            if (!existing && !add_slot) continue;
            const int x = CC_COL3_X
                + index * (CC_SPELL_SLOT_W + CC_SPELL_SLOT_GAP);
            if (local_x >= x && local_x < x + CC_SPELL_SLOT_W
                && local_y >= CC_SPELL_Y && local_y < CC_SPELL_Y + CC_ROW_H)
                return static_cast<CellHitArea>(CELL_HIT_SPELL_BASE + index);
        }
    }

    if (local_x >= CC_ICON_X && local_x < CC_ICON_X + CC_ICON_W
        && local_y >= CC_ICON_Y && local_y < CC_ICON_Y + CC_ICON_H + CC_LABEL_H)
        return CELL_HIT_ICON;

    return CELL_HIT_NONE;
}

// 展开列表可见行索引（-1 无效，不含滚动偏移）。
static int CellControl_HitExpandVisibleIndex(CellControl* ctrl, int local_x, int local_y)
{
    if (!ctrl || ctrl->expanded == CEX_NONE) return -1;
    const int lx = CellControl_GetExpandListLeftX(ctrl);
    if (local_x < lx || local_x >= lx + CellControl_GetExpandListWidth(ctrl)) return -1;
    const int n = CellControl_GetExpandedItemCount(ctrl);
    if (n <= 0) return -1;
    const int list_top = CellControl_GetExpandListTopY(ctrl);
    const int rel_y = local_y - list_top;
    if (rel_y < 0) return -1;
    const int idx = rel_y / CC_DROPDOWN_ITEM_H;
    if (idx < 0 || idx >= n) return -1;
    return idx;
}

// 展开列表绝对项索引（站立/攻击含 dd_scroll；其它下拉等同可见索引）
static int CellControl_HitExpandIndex(CellControl* ctrl, int local_x, int local_y)
{
    const int vis = CellControl_HitExpandVisibleIndex(ctrl, local_x, local_y);
    if (vis < 0) return -1;
    if (ctrl->expanded == CEX_STAND || ctrl->expanded == CEX_ATTACK)
        return vis + (ctrl->dd_scroll > 0 ? ctrl->dd_scroll : 0);
    return vis;
}

// 滚轮滚动当前展开 hex 列表。delta>0 向上，<0 向下。返回 true 表示有变化。
static bool CellControl_ScrollExpanded(CellControl* ctrl, int delta)
{
    if (!ctrl || delta == 0) return false;
    if (ctrl->expanded != CEX_STAND && ctrl->expanded != CEX_ATTACK)
        return false;
    int hexes[165];
    const int n = CellControl_BuildMeleeHexList(ctrl, hexes);
    if (n <= CC_DD_MAX_VISIBLE) return false;
    const int max_scroll = n - CC_DD_MAX_VISIBLE;
    int scroll = ctrl->dd_scroll;
    if (delta > 0) scroll -= 1;
    else scroll += 1;
    if (scroll < 0) scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;
    if (scroll == ctrl->dd_scroll) return false;
    ctrl->dd_scroll = scroll;
    ctrl->dirty = true;
    return true;
}

// ========================================================================
// 鼠标输入
// ========================================================================

static bool CellControl_OnMouse(CellControl* ctrl, int msg_type,
    int mouse_local_x, int mouse_local_y, bool /*expand_up_unused*/)
{
    if (!ctrl) return false;
    CellHitArea hit = CellControl_HitTestInCell(ctrl, mouse_local_x, mouse_local_y);

    if (msg_type == 4) { // LButtonDown
        if (hit == CELL_HIT_ACTION) {
            ctrl->expanded = (ctrl->expanded == CEX_ACTION) ? CEX_NONE : CEX_ACTION;
            ctrl->action_pressed = true;
            ctrl->hover_item = -1;
            ctrl->spell_pick_request = 0;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_SELECTOR) {
            ctrl->expanded = (ctrl->expanded == CEX_SELECTOR) ? CEX_NONE : CEX_SELECTOR;
            ctrl->selector_pressed = true;
            ctrl->hover_item = -1;
            ctrl->spell_pick_request = 0;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_CHECKBOX) {
            ctrl->data.rule.allowDefendFallback = !ctrl->data.rule.allowDefendFallback;
            ctrl->expanded = CEX_NONE;
            ctrl->spell_pick_request = 0;
            ctrl->dirty = true;
            return true;
        }
        if (hit >= CELL_HIT_MELEE_PAIR_BASE
            && hit < CELL_HIT_MELEE_PAIR_BASE + MELEE_PAIR_CAPACITY) {
            // 组合设置始终连续拾取：先站立格，再相邻攻击格。
            ctrl->expanded = CEX_NONE;
            ctrl->spell_pick_request = 0;
            ctrl->melee_pair_pick_request =
                static_cast<int>(hit - CELL_HIT_MELEE_PAIR_BASE) + 1;
            ctrl->dirty = true;
            return true;
        }
        if (hit >= CELL_HIT_SPELL_BASE
            && hit < CELL_HIT_SPELL_BASE + SPELL_SLOT_CAPACITY) {
            // 面板保持打开，等待直接按 1-9/0 写入该槽。
            ctrl->expanded = CEX_NONE;
            ctrl->spell_pick_request =
                (hit - CELL_HIT_SPELL_BASE) + 1; // 1..SPELL_SLOT_CAPACITY
            ctrl->hover_item = -1;
            ctrl->dirty = true;
            return true;
        }
        if (hit >= CELL_HIT_MOVE_WP_BASE
            && hit < CELL_HIT_MOVE_WP_BASE + MOVE_WAYPOINT_CAPACITY) {
            // 路径点设置：隐藏面板后进战场点 1 格；可覆盖已有或末尾追加。
            ctrl->expanded = CEX_NONE;
            ctrl->spell_pick_request = 0;
            ctrl->move_path_pick_request =
                (hit - CELL_HIT_MOVE_WP_BASE) + 1; // 1..MOVE_WAYPOINT_CAPACITY
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_DROP)
            return true; // 按下只消费，松开再选

        if (ctrl->expanded != CEX_NONE) {
            ctrl->expanded = CEX_NONE;
            ctrl->spell_pick_request = 0;
            ctrl->dirty = true;
        }
    }

    // 右键删除：仅对“已有项”生效；「＋」追加槽右键无效。
    // msg_type 5 = 面板层转发的右键松开。
    if (msg_type == 5) {
        ctrl->expanded = CEX_NONE;
        ctrl->spell_pick_request = 0;
        ctrl->move_path_pick_request = 0;
        ctrl->melee_pair_pick_request = 0;

        if (hit >= CELL_HIT_SPELL_BASE
            && hit < CELL_HIT_SPELL_BASE + SPELL_SLOT_CAPACITY) {
            const int index = hit - CELL_HIT_SPELL_BASE;
            if (index < ctrl->data.rule.spellSlotCount
                && CellControl_RemoveSpellSlot(&ctrl->data.rule, index)) {
                ctrl->dirty = true;
                return true;
            }
            return true; // 右键在「＋」上也消费，避免穿透
        }
        if (hit >= CELL_HIT_MOVE_WP_BASE
            && hit < CELL_HIT_MOVE_WP_BASE + MOVE_WAYPOINT_CAPACITY) {
            const int index = hit - CELL_HIT_MOVE_WP_BASE;
            if (index < ctrl->data.rule.target.moveWaypointCount
                && CellControl_RemoveMoveWaypoint(&ctrl->data.rule.target, index)) {
                ctrl->dirty = true;
                return true;
            }
            return true;
        }
        if (hit >= CELL_HIT_MELEE_PAIR_BASE
            && hit < CELL_HIT_MELEE_PAIR_BASE + MELEE_PAIR_CAPACITY) {
            const int index = hit - CELL_HIT_MELEE_PAIR_BASE;
            if (index < ctrl->data.rule.target.meleePairCount
                && CellControl_RemoveMeleePair(&ctrl->data.rule.target, index)) {
                ctrl->dirty = true;
                return true;
            }
            return true;
        }
        return false;
    }

    if (msg_type == 8) { // LButtonUp
        if (hit == CELL_HIT_DROP && ctrl->expanded != CEX_NONE) {
            const int idx = CellControl_HitExpandIndex(ctrl, mouse_local_x, mouse_local_y);
            if (idx >= 0) {
                if (ctrl->expanded == CEX_ACTION) {
                    AutoActionKind allowed[AA_COUNT] = {};
                    const int n = CellControl_GetAllowedActions(ctrl->data.creature_type,
                        ctrl->data.is_ranged, ctrl->data.has_artillery,
                        ctrl->data.has_first_aid, allowed);
                    if (idx < n) {
                        ctrl->data.rule.action = allowed[idx];
                        ctrl->data.rule.target =
                            CellControl_DefaultTargetForAction(ctrl->data.rule.action);
                        CellControl_NormalizeRule(&ctrl->data.rule, ctrl->data.creature_type,
                            ctrl->data.is_ranged, ctrl->data.has_artillery,
                            ctrl->data.has_first_aid);
                        CellControl_EnsureMeleeDefaults(ctrl);
                    }
                } else if (ctrl->expanded == CEX_SELECTOR) {
                    AutoTargetSelector allowed[SEL_COUNT] = {};
                    const int n = CellControl_GetAllowedSelectors(ctrl->data.rule.action,
                        ctrl->data.rule.target.kind, allowed);
                    if (idx < n)
                        ctrl->data.rule.target.selector = allowed[idx];
                } else if (ctrl->expanded == CEX_STAND || ctrl->expanded == CEX_ATTACK) {
                    int hexes[165];
                    const int n = CellControl_BuildMeleeHexList(ctrl, hexes);
                    if (idx >= 0 && idx < n) {
                        if (ctrl->expanded == CEX_STAND) {
                            ctrl->data.rule.target.meleeStandHex = (int16_t)hexes[idx];
                            // 近战：站立格变了，攻击格若不再相邻则清空；
                            // 循环移动：两点独立，不做相邻约束。
                            if (ctrl->data.rule.action == AA_MELEE_ATTACK) {
                                int nb[6];
                                const int m = CellControl_HexNeighbors(hexes[idx], nb);
                                bool still = false;
                                for (int k = 0; k < m; ++k)
                                    if (nb[k] == ctrl->data.rule.target.meleeAttackHex) { still = true; break; }
                                if (!still) ctrl->data.rule.target.meleeAttackHex = -1;
                            }
                        } else {
                            ctrl->data.rule.target.meleeAttackHex = (int16_t)hexes[idx];
                        }
                    }
                }
                ctrl->expanded = CEX_NONE;
                ctrl->dd_scroll = 0;
                ctrl->action_pressed = false;
                ctrl->selector_pressed = false;
                ctrl->dirty = true;
                return true;
            }
        }
        ctrl->action_pressed = false;
        ctrl->selector_pressed = false;
        ctrl->dirty = true;
    }

    return false;
}

#endif // _CELL_CONTROL_INC_CPP
