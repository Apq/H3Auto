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
    AutoStackRule rule;
};

// ========================================================================
// 控件常量
// ========================================================================

static const int CC_CELL_W    = 568;
static const int CC_CELL_H    = 72;

static const int CC_ICON_X   = 4;
static const int CC_ICON_Y   = 4;
static const int CC_ICON_W   = 58;
static const int CC_ICON_H   = 64;
static const int CC_ICON_FRAME_W = 60;
static const int CC_ICON_FRAME_H = 66;

static const int CC_LABEL_H  = 11;

// 一行一格（宽格 568×72）横排小列布局：
//   图标(x=4,宽58) | 位置/数量小列(上下叠放) | 行动下拉(上)+允许降级(下) | 选择器/攻击格(上)+阵营/站立格/降级(下)
// 图标右缘 = 4+58 = 62。位置/数量小列宽 90（约放 10 位数字），下拉列相应左移加宽。
static const int CC_POS_X    = 68;   // 位置/数量小列
static const int CC_POS_W    = 74;   // 放得下数量/位置（右缘 142）
static const int CC_COL2_X   = 148;  // 行动下拉/站立格（避开位置数量列）
static const int CC_COL_W    = 202;
static const int CC_COL3_X   = CC_COL2_X + CC_COL_W + 8;  // 366（右缘 560 < 568）
static const int CC_ROW_H    = 20;
static const int CC_TOP_Y    = 6;
static const int CC_BOT_Y    = 32;
static const int CC_CHECKBOX_H = 14;

// 位置/数量小列纵向 4 行：快捷施法复选框(顶) / 选择魔法行 / 数量 / 位置(贴下边缘)
static const int CC_SPELL_CHK_Y = 3;   // 「先快捷施法」复选框行
static const int CC_SPELL_SEL_Y = 18;  // 「选择魔法」标签+下拉行
static const int CC_SPELL_SEL_H = 14;  // 魔法下拉行高

// 兼容旧名（行动列 = 小列2）
static const int CC_COMBO_X  = CC_COL2_X;
static const int CC_COMBO_W  = CC_COL_W;
static const int CC_ROW1_Y   = CC_TOP_Y;

static const int CC_DROPDOWN_ITEM_H = 18;

// 标签（由 SettingsDlg 注入）
extern const char* g_action_labels[AA_COUNT];
extern const char* g_side_labels[ATS_COUNT];
extern const char* g_selector_labels[SEL_COUNT];

// 展开中的下拉类型
enum CellExpandKind {
    CEX_NONE = 0,
    CEX_ACTION,
    CEX_SIDE,
    CEX_SELECTOR,
    CEX_STAND,    // 近战站立格（全战场坐标，可滚动）
    CEX_ATTACK,   // 近战攻击格（站立格相邻格）
    CEX_SPELL,    // 快捷施法魔法槽位（1-9,0）
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
    bool             side_pressed;
    bool             selector_pressed;
    bool             dirty;
    bool             has_data;
    H3LoadedDef*     def_cache;

    // 近战选格请求：1=选站立格，2=选攻击格，0=无（旧机制，保留兼容）
    int              melee_pick_request;
    // 循环移动路径拾取请求：1=请求进入战场连续拾取，0=无
    int              move_path_pick_request;
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
static int CellControl_HexNeighbors(int hex, int out[6])
{
    int n = 0;
    if (!CellControl_HexValid(hex)) return 0;
    const int row = hex / 17;
    const bool odd = (row & 1) != 0;
    int cand[6];
    // 同行左右
    cand[0] = hex - 1;
    cand[1] = hex + 1;
    if (!odd) {
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
    for (int i = 0; i < 6; ++i) {
        if (CellControl_HexValid(cand[i]))
            out[n++] = cand[i];
    }
    return n;
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
static int CellControl_GetAllowedActions(int creature_type, bool is_ranged,
    AutoActionKind out_actions[AA_COUNT])
{
    int n = 0;
    auto push = [&](AutoActionKind a) {
        if (n < AA_COUNT) out_actions[n++] = a;
    };

    switch (creature_type) {
    case eCreature::FIRST_AID_TENT:
        push(AA_MANUAL);
        push(AA_FIRST_AID);
        break;
    case eCreature::CATAPULT:
        push(AA_MANUAL);
        push(AA_CATAPULT);
        break;
    case eCreature::BALLISTA:
    case eCreature::ARROW_TOWER:
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
    case AA_CATAPULT:
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
    t.fixedSide = -1;
    t.fixedSlot = -1;
    t.fixedCreatureId = -1;
    t.fixedHex = -1;

    switch (action) {
    case AA_MOVE:
        // 循环移动：两个全战场路径点（复用 meleeStandHex/meleeAttackHex）
        t.kind = AT_POSITION;
        t.side = ATS_ENEMY;
        t.selector = SEL_FIXED;
        t.meleeStandHex = -1;
        t.meleeAttackHex = -1;
        break;
    case AA_MELEE_ATTACK:
        // 近战：站立格 + 攻击格（模拟点击）
        t.kind = AT_POSITION;
        t.side = ATS_ENEMY;
        t.selector = SEL_FIXED;
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
        t.selector = SEL_MOST_WOUNDED;
        break;
    case AA_CATAPULT:
        t.kind = AT_POSITION;
        t.side = ATS_ENEMY;
        t.selector = SEL_SEQUENTIAL;
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
static void CellControl_NormalizeRule(AutoStackRule* rule, int creature_type, bool is_ranged)
{
    if (!rule) return;

    AutoActionKind allowed[AA_COUNT] = {};
    const int n = CellControl_GetAllowedActions(creature_type, is_ranged, allowed);
    bool ok = false;
    for (int i = 0; i < n; ++i) {
        if (allowed[i] == rule->action) { ok = true; break; }
    }
    if (!ok)
        rule->action = (n > 0) ? allowed[0] : AA_MANUAL;

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
        rule->target.selector = SEL_FIXED;
        if (rule->target.meleeStandHex < 1 || rule->target.meleeStandHex > 185)
            rule->target.meleeStandHex = -1;
        if (rule->target.meleeAttackHex < 1 || rule->target.meleeAttackHex > 185)
            rule->target.meleeAttackHex = -1;
        break;
    case AA_RANGED_ATTACK:
        rule->target.kind = AT_STACK;
        rule->target.side = ATS_ENEMY;
        if (rule->target.selector == SEL_MOST_WOUNDED)
            rule->target.selector = SEL_RANDOM;
        break;
    case AA_FIRST_AID:
        rule->target.kind = AT_STACK;
        rule->target.side = ATS_OWN;
        break;
    case AA_CATAPULT:
        rule->target.kind = AT_POSITION;
        if (rule->target.selector != SEL_FIXED
            && rule->target.selector != SEL_SEQUENTIAL
            && rule->target.selector != SEL_RANDOM)
            rule->target.selector = SEL_SEQUENTIAL;
        break;
    case AA_MOVE:
        // 循环移动：两个全战场路径点（复用 meleeStandHex/meleeAttackHex）
        rule->target.kind = AT_POSITION;
        rule->target.selector = SEL_FIXED;
        if (rule->target.meleeStandHex < 1 || rule->target.meleeStandHex > 185)
            rule->target.meleeStandHex = -1;
        if (rule->target.meleeAttackHex < 1 || rule->target.meleeAttackHex > 185)
            rule->target.meleeAttackHex = -1;
        break;
    default:
        break;
    }

    // 收敛选择器到当前允许集合（近战/移动固定 SEL_FIXED 已在上面单独设定）。
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
        // 近战（站立格+攻击格）、循环移动（两个路径点）：不用选择器菜单
        push(SEL_FIXED);
        return n;
    }
    // 统一简化选择器：无 / 远程高速优先 / 数量最多 / 随机
    // （目标是减少重复操作，不接管全部 AI 决策）
    push(SEL_NONE);
    push(SEL_RANGED_SPEED);
    push(SEL_COUNT_HIGH);
    push(SEL_RANDOM);
    return n;
}

// 目标第二行：阵营（仅部分行动需要）。移动改用两格路径点，不再用阵营下拉。
static bool CellControl_ShowsSideRow(AutoActionKind action)
{
    (void)action;
    return false; // 阵营下拉已停用（移动=两格路径点，攻击/急救阵营固定）
}

// 目标第二行标签（阵营固定时显示说明）
static const char* CellControl_FixedSideHint(AutoActionKind action)
{
    switch (action) {
    case AA_MELEE_ATTACK:
        return "站立格+攻击格";
    case AA_RANGED_ATTACK:
        return nullptr; // 远程攻击不显示固定说明
    case AA_FIRST_AID:
        return "己方伤员";
    case AA_CATAPULT:
        return "城墙位置";
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
    ctrl->melee_pick_request = 0;
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
    // 第一点默认取当前位置；第二点保持未选（--）直到用户选择。
    if (!CellControl_HexValid(ctrl->data.rule.target.meleeStandHex)
        && CellControl_HexValid(ctrl->data.position))
        ctrl->data.rule.target.meleeStandHex = (int16_t)ctrl->data.position;
    // 仅近战：第二点（攻击格）必须与第一点（站立格）相邻，否则清空。
    // 循环移动：两点独立，无相邻约束。
    if (ctrl->data.rule.action == AA_MELEE_ATTACK
        && CellControl_HexValid(ctrl->data.rule.target.meleeAttackHex)
        && CellControl_HexValid(ctrl->data.rule.target.meleeStandHex)) {
        int nb[6];
        const int m = CellControl_HexNeighbors(ctrl->data.rule.target.meleeStandHex, nb);
        bool still = false;
        for (int k = 0; k < m; ++k)
            if (nb[k] == ctrl->data.rule.target.meleeAttackHex) { still = true; break; }
        if (!still) ctrl->data.rule.target.meleeAttackHex = -1;
    }
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
        ctrl->data.is_ranged);
    CellControl_EnsureMeleeDefaults(ctrl);
    ctrl->expanded = CEX_NONE;
    ctrl->hover_item = -1;
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
// 行动/站立 = 小列2；选择器/阵营/攻击 = 小列3。
static int CellControl_GetExpandListLeftX(CellControl* ctrl)
{
    if (!ctrl) return CC_COL2_X;
    switch (ctrl->expanded) {
    case CEX_ACTION:   return CC_COL2_X;
    case CEX_STAND:    return CC_COL3_X;
    case CEX_SIDE:     return CC_COL3_X;
    case CEX_SELECTOR: return CC_COL3_X;
    case CEX_ATTACK:   return CC_COL3_X;
    case CEX_SPELL:    return CC_POS_X;
    default:           return CC_COL2_X;
    }
}

// 展开列表宽度（魔法下拉用位置/数量小列宽，其余用下拉列宽）
static int CellControl_GetExpandListWidth(CellControl* ctrl)
{
    if (ctrl && ctrl->expanded == CEX_SPELL) return CC_POS_W;
    return CC_COL_W;
}

// 当前展开列表的局部 Y（列表顶部，相对格子）= 其所在行下边缘。
// 行动=上行；选择器/攻击=上行；阵营/站立=下行。
static int CellControl_GetExpandListTopY(CellControl* ctrl)
{
    if (!ctrl) return CC_TOP_Y + CC_ROW_H;
    switch (ctrl->expanded) {
    case CEX_ACTION:   return CC_TOP_Y + CC_ROW_H;
    case CEX_SELECTOR: return CC_TOP_Y + CC_ROW_H;
    case CEX_STAND:    return CC_TOP_Y + CC_ROW_H;
    case CEX_ATTACK:   return CC_BOT_Y + CC_ROW_H;
    case CEX_SIDE:     return CC_BOT_Y + CC_ROW_H;
    case CEX_SPELL:    return CC_SPELL_SEL_Y + CC_SPELL_SEL_H;
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

// 格式化移动路径点列表为 "A01→B05→C09"；无点时返回 "(未设路径)"。
static void CellControl_FormatWaypointList(char* buf, int bufsize,
    const int16_t* wps, int count)
{
    if (bufsize <= 0) return;
    buf[0] = 0;
    if (count <= 0) {
        strncpy(buf, "(未设路径)", bufsize - 1);
        buf[bufsize - 1] = 0;
        return;
    }
    int off = 0;
    for (int i = 0; i < count && off < bufsize - 1; ++i) {
        char one[8] = {};
        CellControl_FormatPosition(one, sizeof(one), wps[i]);
        const char* sep = (i == 0) ? "" : "→";
        off += _snprintf(buf + off, bufsize - off, "%s%s", sep, one);
    }
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
            // 循环移动：小列3上行=「编辑路径(n/6)」按钮；下行=路径文本。
            char btn_buf[24] = {};
            _snprintf(btn_buf, sizeof(btn_buf), "编辑路径 (%d/6)",
                (int)rule.target.moveWaypointCount);
            const bool picking = (ctrl->move_path_pick_request != 0);
            CellControl_DrawButtonBg(scr, CC_COL3_X, CC_TOP_Y, CC_COL_W, CC_ROW_H,
                picking, false);
            CellControl_DrawText(scr, fntS, picking ? "正在选点…右键/再点结束" : btn_buf,
                CC_COL3_X + 4, CC_TOP_Y, CC_COL_W - 8, CC_ROW_H,
                (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);

            // 路径文本：小列3下行（一行显示全部）
            char wp_buf[64] = {};
            CellControl_FormatWaypointList(wp_buf, sizeof(wp_buf),
                rule.target.moveWaypoints, rule.target.moveWaypointCount);
            Fill(scr, CC_COL3_X, CC_BOT_Y, CC_COL_W, CC_ROW_H, 48, 36, 20);
            scr->DrawFrame(CC_COL3_X, CC_BOT_Y, CC_COL_W, CC_ROW_H,
                (BYTE)120, (BYTE)96, (BYTE)48);
            CellControl_DrawText(scr, fntS, wp_buf,
                CC_COL3_X + 4, CC_BOT_Y, CC_COL_W - 8, CC_ROW_H,
                (INT32)eTextColor::LIGHT_GREEN, eTextAlignment::MIDDLE_LEFT);
        } else if (CellControl_ActionUsesTwoHex(rule.action)) {
            // 两格 UI：近战=站立格+攻击格。小列3上下行。
            const bool is_move = false;
            char stand_buf[24] = {};
            char atk_buf[24] = {};
            char pos1[8] = {}, pos2[8] = {};
            int stand_show = rule.target.meleeStandHex;
            if (!CellControl_HexValid(stand_show))
                stand_show = ctrl->data.position; // 默认当前位置
            CellControl_FormatPosition(pos1, sizeof(pos1), stand_show);
            CellControl_FormatPosition(pos2, sizeof(pos2), rule.target.meleeAttackHex);
            _snprintf(stand_buf, sizeof(stand_buf), is_move ? "位置1: %s" : "站立: %s", pos1);
            _snprintf(atk_buf, sizeof(atk_buf), is_move ? "位置2: %s" : "攻击: %s", pos2);

            // 站立格：小列3上行
            CellControl_DrawButtonBg(scr, CC_COL3_X, CC_TOP_Y, CC_COL_W, CC_ROW_H,
                ctrl->side_pressed, false);
            CellControl_DrawText(scr, fntS, stand_buf,
                CC_COL3_X + 4, CC_TOP_Y, CC_COL_W - 20, CC_ROW_H,
                (INT32)eTextColor::LIGHT_GREEN, eTextAlignment::MIDDLE_LEFT);
            CellControl_DrawArrow(scr, CC_COL3_X + CC_COL_W - 14,
                CC_TOP_Y + CC_ROW_H / 2 - 3, ctrl->expanded != CEX_STAND);

            // 攻击格：小列3下行
            CellControl_DrawButtonBg(scr, CC_COL3_X, CC_BOT_Y, CC_COL_W, CC_ROW_H,
                ctrl->selector_pressed, false);
            CellControl_DrawText(scr, fntS, atk_buf,
                CC_COL3_X + 4, CC_BOT_Y, CC_COL_W - 20, CC_ROW_H,
                (INT32)eTextColor::CYAN, eTextAlignment::MIDDLE_LEFT);
            CellControl_DrawArrow(scr, CC_COL3_X + CC_COL_W - 14,
                CC_BOT_Y + CC_ROW_H / 2 - 3, ctrl->expanded != CEX_ATTACK);
        } else {
            // 选择器：小列3上行
            const char* sel_label =
                (rule.target.selector < SEL_COUNT && g_selector_labels[rule.target.selector])
                ? g_selector_labels[rule.target.selector] : "---";
            CellControl_DrawButtonBg(scr, CC_COL3_X, CC_TOP_Y, CC_COL_W, CC_ROW_H,
                ctrl->selector_pressed, false);
            CellControl_DrawText(scr, fntS, sel_label,
                CC_COL3_X + 4, CC_TOP_Y, CC_COL_W - 20, CC_ROW_H,
                (INT32)eTextColor::CYAN, eTextAlignment::MIDDLE_LEFT);
            CellControl_DrawArrow(scr, CC_COL3_X + CC_COL_W - 14,
                CC_TOP_Y + CC_ROW_H / 2 - 3, ctrl->expanded != CEX_SELECTOR);

            // 阵营 / 固定说明：小列3下行
            if (CellControl_ShowsSideRow(rule.action)) {
                const char* side_label =
                    (rule.target.side < ATS_COUNT && g_side_labels[rule.target.side])
                    ? g_side_labels[rule.target.side] : "---";
                CellControl_DrawButtonBg(scr, CC_COL3_X, CC_BOT_Y, CC_COL_W, CC_ROW_H,
                    ctrl->side_pressed, false);
                CellControl_DrawText(scr, fntS, side_label,
                    CC_COL3_X + 4, CC_BOT_Y, CC_COL_W - 20, CC_ROW_H,
                    (INT32)eTextColor::LIGHT_GREEN, eTextAlignment::MIDDLE_LEFT);
                CellControl_DrawArrow(scr, CC_COL3_X + CC_COL_W - 14,
                    CC_BOT_Y + CC_ROW_H / 2 - 3, ctrl->expanded != CEX_SIDE);
            } else {
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
            cb_x + cb_box + 4, cb_y - 1, CC_COL_W - cb_box - 8, CC_CHECKBOX_H,
            (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
    }

    // ---- 快捷施法：位置/数量小列顶部两行 ----
    {
        // 第1行：「先快捷施法」复选框
        const int cb_x = CC_POS_X;
        const int cb_y = CC_SPELL_CHK_Y + 1;
        const int cb_box = 10;
        Fill(scr, cb_x, cb_y, cb_box, cb_box, 40, 28, 12);
        scr->DrawFrame(cb_x, cb_y, cb_box, cb_box, (BYTE)184, (BYTE)139, (BYTE)62);
        if (rule.quickCastFirst) {
            Fill(scr, cb_x + 2, cb_y + 4, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 3, cb_y + 5, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 4, cb_y + 6, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 5, cb_y + 5, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 6, cb_y + 4, 2, 2, 235, 205, 116);
            Fill(scr, cb_x + 7, cb_y + 3, 2, 2, 235, 205, 116);
        }
        CellControl_DrawText(scr, fntS, "先快捷施法",
            cb_x + cb_box + 3, cb_y - 2, CC_POS_W - cb_box - 4, CC_CHECKBOX_H,
            (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);

        // 第2行：「选择魔法」标签 + 槽位下拉
        char slot_buf[8] = {};
        _snprintf(slot_buf, sizeof(slot_buf), "%d", (int)rule.spellSlot);
        const int lbl_w = 44;
        const int dd_x  = CC_POS_X + lbl_w;
        const int dd_w  = CC_POS_W - lbl_w;
        CellControl_DrawText(scr, fntS, "选择魔法",
            CC_POS_X, CC_SPELL_SEL_Y, lbl_w, CC_SPELL_SEL_H,
            (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
        CellControl_DrawButtonBg(scr, dd_x, CC_SPELL_SEL_Y, dd_w, CC_SPELL_SEL_H,
            false, false);
        CellControl_DrawText(scr, fntS, slot_buf,
            dd_x + 3, CC_SPELL_SEL_Y, dd_w - 14, CC_SPELL_SEL_H,
            (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
        CellControl_DrawArrow(scr, dd_x + dd_w - 10,
            CC_SPELL_SEL_Y + CC_SPELL_SEL_H / 2 - 3, ctrl->expanded != CEX_SPELL);
    }

    // ---- 位置/数量：独立小列（CC_POS_X）。数量在上(右对齐)，位置靠格子下边缘(左对齐) ----
    const int pos_y   = CC_CELL_H - CC_LABEL_H - 3;   // 位置：贴格子下边缘（非接触）
    const int count_y = pos_y - CC_LABEL_H - 2;        // 数量：位置上方

    char count_buf[16] = {};
    if (ctrl->data.count_alive > 0)
        _snprintf(count_buf, sizeof(count_buf), "%d", ctrl->data.count_alive);
    else
        strncpy(count_buf, "--", sizeof(count_buf) - 1);
    CellControl_DrawText(scr, fntS, count_buf,
        CC_POS_X, count_y, CC_POS_W, CC_LABEL_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_RIGHT);

    char pos_buf[8] = {};
    CellControl_FormatPosition(pos_buf, sizeof(pos_buf), ctrl->data.position);
    CellControl_DrawText(scr, fntS, pos_buf,
        CC_POS_X, pos_y, CC_POS_W, CC_LABEL_H,
        (INT32)eTextColor::LIGHT_GREEN, eTextAlignment::MIDDLE_LEFT);

    ctrl->dirty = false;
}

// ========================================================================
// 绘制：展开列表
// ========================================================================

static void CellControl_DrawDropdownItem(H3LoadedPcx16* scr, H3Font* fnt,
    const char* label, int item_index,
    int cell_panel_x, int cell_panel_y,
    bool is_selected, bool is_hovered, bool cool_theme,
    int list_top_y, int list_left_x)
{
    int item_x = cell_panel_x + list_left_x;
    int item_y = cell_panel_y + list_top_y + item_index * CC_DROPDOWN_ITEM_H;
    int item_w = (list_left_x == CC_POS_X) ? CC_POS_W : CC_COL_W;  // 魔法下拉用窄宽
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
        ctrl->data.is_ranged, allowed);
    for (int i = 0; i < n; ++i) {
        const AutoActionKind a = allowed[i];
        const char* label = (a < AA_COUNT && g_action_labels[a]) ? g_action_labels[a] : "---";
        CellControl_DrawDropdownItem(scr, fntS, label, i,
            cell_panel_x, cell_panel_y,
            a == ctrl->data.rule.action, i == hover_idx, false,
            CC_TOP_Y + CC_ROW_H, CC_COL2_X);
    }
}

// 展开阵营列表（仅移动）
static void CellControl_DrawSideDropdownTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y, int hover_idx)
{
    if (!ctrl || ctrl->expanded != CEX_SIDE || !scr) return;
    if (!CellControl_ShowsSideRow(ctrl->data.rule.action)) return;
    H3Font* fntS = GetSmallFont();
    if (!fntS) return;

    // 移动：己方 / 敌方（作为靠近锚点）
    const AutoTargetSide sides[2] = { ATS_OWN, ATS_ENEMY };
    for (int i = 0; i < 2; ++i) {
        const AutoTargetSide s = sides[i];
        const char* label = (s < ATS_COUNT && g_side_labels[s]) ? g_side_labels[s] : "---";
        CellControl_DrawDropdownItem(scr, fntS, label, i,
            cell_panel_x, cell_panel_y,
            s == ctrl->data.rule.target.side, i == hover_idx, true,
            CC_BOT_Y + CC_ROW_H, CC_COL3_X);
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
        Fill(scr, item_x, item_y, CC_COMBO_W, CC_DROPDOWN_ITEM_H, bg_r, bg_g, bg_b);
        scr->DrawFrame(item_x, item_y, CC_COMBO_W, CC_DROPDOWN_ITEM_H, fr, fg, fb);
        CellControl_DrawText(scr, fntS, buf,
            item_x + 4, item_y, CC_COMBO_W - 20, CC_DROPDOWN_ITEM_H,
            tc, eTextAlignment::MIDDLE_LEFT);
        // 滚动指示：首/末可见项画箭头
        if (v == 0 && scroll > 0)
            CellControl_DrawArrow(scr, item_x + CC_COMBO_W - 12, item_y + 2, false);
        if (v == vis - 1 && scroll < max_scroll)
            CellControl_DrawArrow(scr, item_x + CC_COMBO_W - 12, item_y + CC_DROPDOWN_ITEM_H - 6, true);
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

// 展开魔法槽位列表（10 项：1-9,0），从位置/数量小列展开
static void CellControl_DrawSpellDropdownTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y, int hover_idx)
{
    if (!ctrl || ctrl->expanded != CEX_SPELL || !scr) return;
    H3Font* fntS = GetSmallFont();
    if (!fntS) return;
    const int cur = CellControl_SpellSlotToIndex(ctrl->data.rule.spellSlot);
    for (int i = 0; i < 10; ++i) {
        CellControl_DrawDropdownItem(scr, fntS, CellControl_SpellSlotLabel(i), i,
            cell_panel_x, cell_panel_y,
            i == cur, i == hover_idx, false,
            CC_SPELL_SEL_Y + CC_SPELL_SEL_H, CC_POS_X);
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
    case CEX_SIDE:
        CellControl_DrawSideDropdownTo(ctrl, scr, cell_panel_x, cell_panel_y, hover_idx);
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
            ctrl->data.is_ranged, allowed);
    }
    case CEX_SIDE:
        return CellControl_ShowsSideRow(ctrl->data.rule.action) ? 2 : 0;
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
    CELL_HIT_SIDE,
    CELL_HIT_SELECTOR,
    CELL_HIT_CHECKBOX,
    CELL_HIT_DROP,       // 当前展开列表
    CELL_HIT_MELEE_STAND,  // 近战站立格
    CELL_HIT_MELEE_ATTACK, // 近战攻击格
    CELL_HIT_MOVE_EDIT,    // 循环移动：编辑路径按钮
    CELL_HIT_MOVE_CLEAR,   // 循环移动：路径文本（点击清空）
    CELL_HIT_SPELL_CHK,    // 快捷施法复选框
    CELL_HIT_SPELL_SLOT,   // 魔法槽位下拉
};

static CellHitArea CellControl_HitTestInCell(CellControl* ctrl, int local_x, int local_y)
{
    if (!ctrl) return CELL_HIT_NONE;
    const bool needs_target = CellControl_ActionNeedsTarget(ctrl->data.rule.action);
    const bool shows_fallback = CellControl_ActionShowsFallback(
        ctrl->data.creature_type, ctrl->data.rule.action);
    const bool melee = CellControl_ActionUsesTwoHex(ctrl->data.rule.action);

    auto in_box = [&](int x, int y) -> bool {
        return local_x >= x && local_x < x + CC_COL_W
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
    if (ctrl->expanded != CEX_ACTION && in_box(CC_COL2_X, CC_TOP_Y))
        return CELL_HIT_ACTION;

    if (needs_target) {
        if (ctrl->data.rule.action == AA_MOVE) {
            // 循环移动：编辑路径按钮（小列3上行），路径文本清空（小列3下行）
            if (in_box(CC_COL3_X, CC_TOP_Y))
                return CELL_HIT_MOVE_EDIT;
            if (in_box(CC_COL3_X, CC_BOT_Y))
                return CELL_HIT_MOVE_CLEAR;
        } else if (melee) {
            // 站立格：小列3上行；攻击格：小列3下行
            if (ctrl->expanded != CEX_STAND && in_box(CC_COL3_X, CC_TOP_Y))
                return CELL_HIT_MELEE_STAND;
            if (ctrl->expanded != CEX_ATTACK && in_box(CC_COL3_X, CC_BOT_Y))
                return CELL_HIT_MELEE_ATTACK;
        } else {
            // 选择器：小列3上行
            if (ctrl->expanded != CEX_SELECTOR && in_box(CC_COL3_X, CC_TOP_Y))
                return CELL_HIT_SELECTOR;
            // 阵营：小列3下行（仅移动）
            if (CellControl_ShowsSideRow(ctrl->data.rule.action)
                && ctrl->expanded != CEX_SIDE && in_box(CC_COL3_X, CC_BOT_Y))
                return CELL_HIT_SIDE;
        }
    }

    // 降级复选框：统一在小列2下行（行动下拉正下方）
    if (shows_fallback) {
        if (local_x >= CC_COL2_X && local_x < CC_COL2_X + CC_COL_W
            && local_y >= CC_BOT_Y && local_y < CC_BOT_Y + CC_ROW_H)
            return CELL_HIT_CHECKBOX;
    }

    // 快捷施法：复选框（位置/数量小列顶部第1行）+ 魔法槽位下拉（第2行）
    {
        const int cb_box = 10;
        if (local_x >= CC_POS_X && local_x < CC_POS_X + CC_POS_W
            && local_y >= CC_SPELL_CHK_Y && local_y < CC_SPELL_CHK_Y + cb_box + 4)
            return CELL_HIT_SPELL_CHK;
        if (ctrl->expanded != CEX_SPELL
            && local_x >= CC_POS_X && local_x < CC_POS_X + CC_POS_W
            && local_y >= CC_SPELL_SEL_Y && local_y < CC_SPELL_SEL_Y + CC_SPELL_SEL_H)
            return CELL_HIT_SPELL_SLOT;
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
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_SIDE) {
            ctrl->expanded = (ctrl->expanded == CEX_SIDE) ? CEX_NONE : CEX_SIDE;
            ctrl->side_pressed = true;
            ctrl->hover_item = -1;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_SELECTOR) {
            ctrl->expanded = (ctrl->expanded == CEX_SELECTOR) ? CEX_NONE : CEX_SELECTOR;
            ctrl->selector_pressed = true;
            ctrl->hover_item = -1;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_CHECKBOX) {
            ctrl->data.rule.allowDefendFallback = !ctrl->data.rule.allowDefendFallback;
            ctrl->expanded = CEX_NONE;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_MELEE_STAND) {
            ctrl->expanded = (ctrl->expanded == CEX_STAND) ? CEX_NONE : CEX_STAND;
            ctrl->dd_scroll = 0;
            ctrl->side_pressed = true;
            ctrl->hover_item = -1;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_MELEE_ATTACK) {
            ctrl->expanded = (ctrl->expanded == CEX_ATTACK) ? CEX_NONE : CEX_ATTACK;
            ctrl->dd_scroll = 0;
            ctrl->selector_pressed = true;
            ctrl->hover_item = -1;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_SPELL_CHK) {
            ctrl->data.rule.quickCastFirst = !ctrl->data.rule.quickCastFirst;
            ctrl->expanded = CEX_NONE;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_SPELL_SLOT) {
            ctrl->expanded = (ctrl->expanded == CEX_SPELL) ? CEX_NONE : CEX_SPELL;
            ctrl->hover_item = -1;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_MOVE_EDIT) {
            // 循环移动：请求进入战场选点模式（由 SettingsDlg 接线激活）。
            ctrl->expanded = CEX_NONE;
            ctrl->move_path_pick_request = 1;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_MOVE_CLEAR) {
            // 点击路径文本：清空全部路径点，重来。
            ctrl->expanded = CEX_NONE;
            for (int k = 0; k < 6; ++k) ctrl->data.rule.target.moveWaypoints[k] = -1;
            ctrl->data.rule.target.moveWaypointCount = 0;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_DROP)
            return true; // 按下只消费，松开再选

        if (ctrl->expanded != CEX_NONE) {
            ctrl->expanded = CEX_NONE;
            ctrl->dirty = true;
        }
    }

    if (msg_type == 8) { // LButtonUp
        if (hit == CELL_HIT_DROP && ctrl->expanded != CEX_NONE) {
            const int idx = CellControl_HitExpandIndex(ctrl, mouse_local_x, mouse_local_y);
            if (idx >= 0) {
                if (ctrl->expanded == CEX_ACTION) {
                    AutoActionKind allowed[AA_COUNT] = {};
                    const int n = CellControl_GetAllowedActions(ctrl->data.creature_type,
                        ctrl->data.is_ranged, allowed);
                    if (idx < n) {
                        ctrl->data.rule.action = allowed[idx];
                        ctrl->data.rule.target =
                            CellControl_DefaultTargetForAction(ctrl->data.rule.action);
                        CellControl_NormalizeRule(&ctrl->data.rule, ctrl->data.creature_type,
                            ctrl->data.is_ranged);
                        CellControl_EnsureMeleeDefaults(ctrl);
                    }
                } else if (ctrl->expanded == CEX_SIDE) {
                    const AutoTargetSide sides[2] = { ATS_OWN, ATS_ENEMY };
                    if (idx < 2) {
                        ctrl->data.rule.target.side = sides[idx];
                        ctrl->data.rule.target.kind = AT_STACK; // 靠近部队
                    }
                } else if (ctrl->expanded == CEX_SELECTOR) {
                    AutoTargetSelector allowed[SEL_COUNT] = {};
                    const int n = CellControl_GetAllowedSelectors(ctrl->data.rule.action,
                        ctrl->data.rule.target.kind, allowed);
                    if (idx < n)
                        ctrl->data.rule.target.selector = allowed[idx];
                } else if (ctrl->expanded == CEX_SPELL) {
                    // 槽位下拉：idx 0-8 → 槽位 1-9，idx 9 → 槽位 0
                    if (idx >= 0 && idx < 10)
                        ctrl->data.rule.spellSlot = (int8_t)((idx < 9) ? (idx + 1) : 0);
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
                ctrl->side_pressed = false;
                ctrl->selector_pressed = false;
                ctrl->dirty = true;
                return true;
            }
        }
        ctrl->action_pressed = false;
        ctrl->side_pressed = false;
        ctrl->selector_pressed = false;
        ctrl->dirty = true;
    }

    return false;
}

#endif // _CELL_CONTROL_INC_CPP
