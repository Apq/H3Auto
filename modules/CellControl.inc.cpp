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
    AutoStackRule rule;
};

// ========================================================================
// 控件常量
// ========================================================================

static const int CC_CELL_W    = 193;
static const int CC_CELL_H    = 96;

static const int CC_ICON_X   = 4;
static const int CC_ICON_Y   = 4;
static const int CC_ICON_W   = 58;
static const int CC_ICON_H   = 64;
static const int CC_ICON_FRAME_W = 60;
static const int CC_ICON_FRAME_H = 66;

static const int CC_LABEL_H  = 11;

static const int CC_COMBO_X  = CC_ICON_X + CC_ICON_W + 4;  // 66
static const int CC_COMBO_W  = CC_CELL_W - CC_COMBO_X - 4;  // 123
static const int CC_ROW1_Y   = 4;
static const int CC_ROW_H    = 20;
static const int CC_ROW_GAP  = 2;
static const int CC_ROW2_Y   = CC_ROW1_Y + CC_ROW_H + CC_ROW_GAP;      // 26
static const int CC_ROW3_Y   = CC_ROW2_Y + CC_ROW_H + CC_ROW_GAP;      // 48
static const int CC_CHECKBOX_Y = CC_ROW3_Y + CC_ROW_H + CC_ROW_GAP;    // 70
static const int CC_CHECKBOX_H = 14;

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
        push(AA_WAIT);
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
        t.kind = AT_POSITION;
        t.side = ATS_ENEMY;
        t.selector = SEL_NEAREST;
        break;
    case AA_MELEE_ATTACK:
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
        if (rule->target.kind != AT_STACK && rule->target.kind != AT_POSITION)
            rule->target.kind = AT_POSITION;
        if (rule->target.kind == AT_STACK
            && rule->target.side != ATS_OWN && rule->target.side != ATS_ENEMY)
            rule->target.side = ATS_ENEMY;
        break;
    default:
        break;
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

    if (action == AA_FIRST_AID) {
        push(SEL_MOST_WOUNDED);
        push(SEL_RANDOM);
        push(SEL_SEQUENTIAL);
        push(SEL_COUNT_HIGH);
        push(SEL_COUNT_LOW);
        push(SEL_FIXED);
        return n;
    }
    if (action == AA_CATAPULT) {
        push(SEL_SEQUENTIAL);
        push(SEL_RANDOM);
        push(SEL_FIXED);
        return n;
    }
    if (action == AA_MOVE) {
        if (kind == AT_STACK) {
            push(SEL_NEAREST);
            push(SEL_FARTHEST);
            push(SEL_RANDOM);
            push(SEL_SEQUENTIAL);
            push(SEL_FIXED);
        } else {
            push(SEL_NEAREST);
            push(SEL_RANDOM);
            push(SEL_SEQUENTIAL);
            push(SEL_FIXED);
        }
        return n;
    }
    // 近战/远程
    push(SEL_RANDOM);
    push(SEL_SEQUENTIAL);
    push(SEL_NEAREST);
    push(SEL_FARTHEST);
    push(SEL_RANGED_SPEED);
    push(SEL_COUNT_HIGH);
    push(SEL_COUNT_LOW);
    push(SEL_FIXED);
    return n;
}

// 目标第二行：阵营（仅部分行动需要）
static bool CellControl_ShowsSideRow(AutoActionKind action)
{
    return action == AA_MOVE; // 移动可选靠近己方/敌方；攻击/急救阵营固定
}

// 目标第二行标签（阵营固定时显示说明）
static const char* CellControl_FixedSideHint(AutoActionKind action)
{
    switch (action) {
    case AA_MELEE_ATTACK:
    case AA_RANGED_ATTACK:
        return "敌方部队";
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
}

static void CellControl_Destroy(CellControl* ctrl)
{
    if (ctrl->buffer) {
        ctrl->buffer->Destroy();
        ctrl->buffer = nullptr;
    }
    ctrl->dirty = true;
}

static void CellControl_SetData(CellControl* ctrl, const CellData* data)
{
    if (!ctrl || !data) return;
    ctrl->data = *data;
    ctrl->def_cache = data->creature_def;
    ctrl->has_data = true;
    // is_ranged 在 SetData 时用 creature_type 粗判；SettingsDlg 可先规范化 rule
    CellControl_NormalizeRule(&ctrl->data.rule, ctrl->data.creature_type,
        CellControl_IsRangedType(ctrl->data.creature_type));
    ctrl->expanded = CEX_NONE;
    ctrl->hover_item = -1;
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

// 展开列表矩形（统一在行动行下方展开；若空间不足由面板层处理裁剪）
static bool CellControl_GetExpandRect(int cell_panel_x, int cell_panel_y,
    int item_count, RECT* out_rc)
{
    if (!out_rc || item_count <= 0) return false;
    out_rc->left   = cell_panel_x + CC_COMBO_X;
    out_rc->top    = cell_panel_y + CC_ROW1_Y + CC_ROW_H;
    out_rc->right  = out_rc->left + CC_COMBO_W;
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

    if (needs_target) {
        // ---- 第二行：阵营 / 固定说明 ----
        if (CellControl_ShowsSideRow(rule.action)) {
            const char* side_label =
                (rule.target.side < ATS_COUNT && g_side_labels[rule.target.side])
                ? g_side_labels[rule.target.side] : "---";
            CellControl_DrawButtonBg(scr, CC_COMBO_X, CC_ROW2_Y, CC_COMBO_W, CC_ROW_H,
                ctrl->side_pressed, false);
            CellControl_DrawText(scr, fntS, side_label,
                CC_COMBO_X + 4, CC_ROW2_Y, CC_COMBO_W - 20, CC_ROW_H,
                (INT32)eTextColor::LIGHT_GREEN, eTextAlignment::MIDDLE_LEFT);
            CellControl_DrawArrow(scr, CC_COMBO_X + CC_COMBO_W - 14,
                CC_ROW2_Y + CC_ROW_H / 2 - 3, ctrl->expanded != CEX_SIDE);
        } else {
            const char* hint = CellControl_FixedSideHint(rule.action);
            if (hint) {
                Fill(scr, CC_COMBO_X, CC_ROW2_Y, CC_COMBO_W, CC_ROW_H, 48, 36, 20);
                scr->DrawFrame(CC_COMBO_X, CC_ROW2_Y, CC_COMBO_W, CC_ROW_H,
                    (BYTE)120, (BYTE)96, (BYTE)48);
                CellControl_DrawText(scr, fntS, hint,
                    CC_COMBO_X + 4, CC_ROW2_Y, CC_COMBO_W - 8, CC_ROW_H,
                    (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
            }
        }

        // ---- 第三行：选择器 ----
        const char* sel_label =
            (rule.target.selector < SEL_COUNT && g_selector_labels[rule.target.selector])
            ? g_selector_labels[rule.target.selector] : "---";
        CellControl_DrawButtonBg(scr, CC_COMBO_X, CC_ROW3_Y, CC_COMBO_W, CC_ROW_H,
            ctrl->selector_pressed, false);
        CellControl_DrawText(scr, fntS, sel_label,
            CC_COMBO_X + 4, CC_ROW3_Y, CC_COMBO_W - 20, CC_ROW_H,
            (INT32)eTextColor::CYAN, eTextAlignment::MIDDLE_LEFT);
        CellControl_DrawArrow(scr, CC_COMBO_X + CC_COMBO_W - 14,
            CC_ROW3_Y + CC_ROW_H / 2 - 3, ctrl->expanded != CEX_SELECTOR);
    }

    // ---- 降级复选框 ----
    if (shows_fallback) {
        const int cb_x = CC_COMBO_X + 2;
        const int cb_y = CC_CHECKBOX_Y;
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
            cb_x + cb_box + 4, cb_y - 1, CC_COMBO_W - cb_box - 8, CC_CHECKBOX_H,
            (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
    }

    // ---- 图标下方数量/位置 ----
    const int label_row1_y = CC_ICON_Y + 52 + 1 - 3;
    const int label_row2_y = CC_ICON_Y + 52 + 1 + CC_LABEL_H - 1;

    char count_buf[16] = {};
    if (ctrl->data.count_alive > 0)
        _snprintf(count_buf, sizeof(count_buf), "%d", ctrl->data.count_alive);
    else
        strncpy(count_buf, "--", sizeof(count_buf) - 1);
    CellControl_DrawText(scr, fntS, count_buf,
        CC_ICON_X, label_row1_y, CC_ICON_W, CC_LABEL_H,
        0x01, eTextAlignment::TOP_RIGHT);

    char pos_buf[8] = {};
    CellControl_FormatPosition(pos_buf, sizeof(pos_buf), ctrl->data.position);
    CellControl_DrawText(scr, fntS, pos_buf,
        CC_ICON_X, label_row2_y, CC_ICON_W, CC_LABEL_H,
        0x01, eTextAlignment::TOP_LEFT);

    ctrl->dirty = false;
}

// ========================================================================
// 绘制：展开列表
// ========================================================================

static void CellControl_DrawDropdownItem(H3LoadedPcx16* scr, H3Font* fnt,
    const char* label, int item_index,
    int cell_panel_x, int cell_panel_y,
    bool is_selected, bool is_hovered, bool cool_theme)
{
    int item_x = cell_panel_x + CC_COMBO_X;
    int item_y = cell_panel_y + CC_ROW1_Y + CC_ROW_H + item_index * CC_DROPDOWN_ITEM_H;
    int item_w = CC_COMBO_W;
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
        CellControl_IsRangedType(ctrl->data.creature_type), allowed);
    for (int i = 0; i < n; ++i) {
        const AutoActionKind a = allowed[i];
        const char* label = (a < AA_COUNT && g_action_labels[a]) ? g_action_labels[a] : "---";
        CellControl_DrawDropdownItem(scr, fntS, label, i,
            cell_panel_x, cell_panel_y,
            a == ctrl->data.rule.action, i == hover_idx, false);
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
            s == ctrl->data.rule.target.side, i == hover_idx, true);
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
            s == ctrl->data.rule.target.selector, i == hover_idx, true);
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
    case CEX_SIDE:
        CellControl_DrawSideDropdownTo(ctrl, scr, cell_panel_x, cell_panel_y, hover_idx);
        break;
    case CEX_SELECTOR:
        CellControl_DrawSelectorDropdownTo(ctrl, scr, cell_panel_x, cell_panel_y, hover_idx);
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
            CellControl_IsRangedType(ctrl->data.creature_type), allowed);
    }
    case CEX_SIDE:
        return CellControl_ShowsSideRow(ctrl->data.rule.action) ? 2 : 0;
    case CEX_SELECTOR: {
        AutoTargetSelector allowed[SEL_COUNT] = {};
        return CellControl_GetAllowedSelectors(ctrl->data.rule.action,
            ctrl->data.rule.target.kind, allowed);
    }
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
};

static CellHitArea CellControl_HitTestInCell(CellControl* ctrl, int local_x, int local_y)
{
    if (!ctrl) return CELL_HIT_NONE;
    const bool needs_target = CellControl_ActionNeedsTarget(ctrl->data.rule.action);
    const bool shows_fallback = CellControl_ActionShowsFallback(
        ctrl->data.creature_type, ctrl->data.rule.action);

    // 展开列表优先
    if (ctrl->expanded != CEX_NONE
        && local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W)
    {
        const int n = CellControl_GetExpandedItemCount(ctrl);
        const int list_top = CC_ROW1_Y + CC_ROW_H;
        const int rel_y = local_y - list_top;
        if (rel_y >= 0 && n > 0) {
            const int idx = rel_y / CC_DROPDOWN_ITEM_H;
            if (idx >= 0 && idx < n)
                return CELL_HIT_DROP;
        }
    }

    if (ctrl->expanded != CEX_ACTION
        && local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W
        && local_y >= CC_ROW1_Y && local_y < CC_ROW1_Y + CC_ROW_H)
        return CELL_HIT_ACTION;

    if (needs_target) {
        if (CellControl_ShowsSideRow(ctrl->data.rule.action)
            && ctrl->expanded != CEX_SIDE
            && local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W
            && local_y >= CC_ROW2_Y && local_y < CC_ROW2_Y + CC_ROW_H)
            return CELL_HIT_SIDE;

        if (ctrl->expanded != CEX_SELECTOR
            && local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W
            && local_y >= CC_ROW3_Y && local_y < CC_ROW3_Y + CC_ROW_H)
            return CELL_HIT_SELECTOR;
    }

    if (shows_fallback
        && local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W
        && local_y >= CC_CHECKBOX_Y && local_y < CC_CHECKBOX_Y + CC_CHECKBOX_H)
        return CELL_HIT_CHECKBOX;

    if (local_x >= CC_ICON_X && local_x < CC_ICON_X + CC_ICON_W
        && local_y >= CC_ICON_Y && local_y < CC_ICON_Y + CC_ICON_H + CC_LABEL_H)
        return CELL_HIT_ICON;

    return CELL_HIT_NONE;
}

// 展开列表项索引（-1 无效）
static int CellControl_HitExpandIndex(CellControl* ctrl, int local_x, int local_y)
{
    if (!ctrl || ctrl->expanded == CEX_NONE) return -1;
    if (local_x < CC_COMBO_X || local_x >= CC_COMBO_X + CC_COMBO_W) return -1;
    const int n = CellControl_GetExpandedItemCount(ctrl);
    if (n <= 0) return -1;
    const int rel_y = local_y - (CC_ROW1_Y + CC_ROW_H);
    if (rel_y < 0) return -1;
    const int idx = rel_y / CC_DROPDOWN_ITEM_H;
    if (idx < 0 || idx >= n) return -1;
    return idx;
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
                        CellControl_IsRangedType(ctrl->data.creature_type), allowed);
                    if (idx < n) {
                        ctrl->data.rule.action = allowed[idx];
                        ctrl->data.rule.target =
                            CellControl_DefaultTargetForAction(ctrl->data.rule.action);
                        CellControl_NormalizeRule(&ctrl->data.rule, ctrl->data.creature_type,
                            CellControl_IsRangedType(ctrl->data.creature_type));
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
                }
                ctrl->expanded = CEX_NONE;
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

// 兼容旧调用名：目标展开绘制（新 UI 统一走 DrawExpandedTo）
static void CellControl_DrawTargetDropdownTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y, int hover_idx, bool /*expand_up*/)
{
    // 新 UI 不再单独用 target 展开；若误调用则画当前展开列表
    CellControl_DrawExpandedTo(ctrl, scr, cell_panel_x, cell_panel_y, hover_idx);
}

// 兼容旧接口：目标下拉区域（用于面板 hover 命中）
static bool CellControl_GetTargetDropdownRect(int cell_panel_x, int cell_panel_y,
    RECT* out_rc, bool /*expand_up*/)
{
    return CellControl_GetExpandRect(cell_panel_x, cell_panel_y, 8, out_rc);
}

static bool CellControl_GetActionDropdownRect(int cell_panel_x, int cell_panel_y, RECT* out_rc)
{
    return CellControl_GetExpandRect(cell_panel_x, cell_panel_y, AA_COUNT, out_rc);
}

#endif // _CELL_CONTROL_INC_CPP
