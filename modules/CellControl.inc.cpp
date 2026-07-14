// ========== CellControl.inc.cpp ==========
// 格子控件：左侧生物图标（带位置和数量），右侧两行下拉框（行动+目标）
// 下拉展开项绘制到面板缓冲区，格子内容绘制到自己的离屏缓冲区。
// 外部负责在适当时机调用 DrawCollapsed() + DrawExpandedTo()，并负责 Blt 到面板。

#ifndef _CELL_CONTROL_INC_CPP
#define _CELL_CONTROL_INC_CPP

static void WriteLog(const char* fmt, ...);

// Fill 和 GetSmallFont 定义在 SettingsDlg.inc.cpp（include 顺序保证先看见）
void Fill(H3LoadedPcx16* scr, int x, int y, int w, int h, int r, int g, int b);
H3Font* GetSmallFont();
// PCX 加载/透明绘制定义在 SettingsDlg.inc.cpp（同一编译单元，前向声明即可）
static H3LoadedPcx16* LoadPanelPcx24_(const char* asset_name, int expected_width,
    int expected_height, H3LoadedPcx16*& cache, bool& load_failed);
static void DrawTransparentPcx_(H3LoadedPcx16* source,
    H3LoadedPcx16* destination, int dst_x, int dst_y);

// 图标金框（HA_icon_frame.pcx，60x54，包住 58x52 图标区，每边 +1px）
static H3LoadedPcx16* s_cc_icon_frame = nullptr;
static bool s_cc_icon_frame_load_failed = false;

// 写回全局策略数组
// g_action/target_strategies 定义在 ConfigLog.inc.cpp
extern "C" INT g_action_strategies[21];
extern "C" INT g_target_strategies[21];

// 当 CellData::army_slot_ix >= 0 时，将选中的值写回全局策略数组
static void SyncStrategyBack_(INT army_slot_ix, int action_id, int target_id)
{
    if (army_slot_ix < 0 || army_slot_ix >= 21) return;
    g_action_strategies[army_slot_ix] = action_id;
    g_target_strategies[army_slot_ix] = target_id;
}

// ========================================================================
// 公共类型（与外部共享）
// ========================================================================

// 格子控件输入数据来源
struct CellData
{
    // 来自 H3CombatCreature
    INT32   creature_type;       // eCreatureType
    INT32   position;           // 战场坐标 0-186（-1 表示空/测试数据）
    INT32   count_alive;        // 当前存活数量
    H3LoadedDef* creature_def;   // 生物 DEF 精灵（可为 nullptr）

    // 策略
    INT     action_id;          // 当前行动策略序号
    INT     target_id;          // 当前目标策略序号

    // 所属阵营槽位（确定按钮提交时需要写回 g_action/target_strategies）
    INT     army_slot_ix;
};

// ========================================================================
// 控件常量（格子尺寸固定，与 SettingsDlg 保持一致）
// ========================================================================

// 格子整体尺寸
static const int CC_CELL_W    = 193;
static const int CC_CELL_H    = 83;

// 左侧生物图标区域（左上角定位）
static const int CC_ICON_X   = 4;
static const int CC_ICON_Y   = 4;
static const int CC_ICON_W   = 58;
static const int CC_ICON_H   = 64;
// 图标金框（HA_icon_frame.pcx 60x66），画在图标区外扩 1px 处
static const int CC_ICON_FRAME_W = 60;
static const int CC_ICON_FRAME_H = 66;

// 图标下方标注区域（两行）
static const int CC_LABEL_H  = 11;  // 每行高度

// 右侧控件区域（紧贴左侧图标右边）
static const int CC_COMBO_X  = CC_ICON_X + CC_ICON_W + 4;  // 66
static const int CC_COMBO_W  = CC_CELL_W - CC_COMBO_X - 4;  // 123
static const int CC_ROW1_Y   = 4;   // 第一行下拉框 Y（顶部对齐）
static const int CC_ROW_H    = 22;  // 每行高度
static const int CC_ROW_GAP  = 2;   // 两行间距
static const int CC_ROW2_Y   = CC_ROW1_Y + CC_ROW_H + CC_ROW_GAP; // 28
static const int CC_LABEL_Y  = CC_ROW2_Y + CC_ROW_H + 2;  // 52，底部位置标注

// 下拉框展开高度（每项 18px）
static const int CC_DROPDOWN_ITEM_H = 18;

// 标签来源（由 SettingsDlg 注入）
extern const char* g_action_labels[6];
extern const char* g_target_labels[4];
static const int MAX_ACTION_LABELS = 6;
static const int MAX_TARGET_LABELS = 4;

// ========================================================================
// CellControl 结构
// 每个格子独立的状态
// ========================================================================

struct CellControl
{
    // 输入数据
    CellData         data;

    // 渲染缓冲区（格子尺寸，不含展开下拉框）
    H3LoadedPcx16*  buffer;

    // 下拉框状态
    bool            action_expanded;
    bool            target_expanded;
    int             action_hover_item;   // -1 = 无
    int             target_hover_item;

    // 按钮状态
    bool            action_pressed;
    bool            target_pressed;

    // 是否需要重绘（数据变化时置 true）
    bool            dirty;

    // 缓存的 DEF 精灵指针（用于绘制生物图标）
    H3LoadedDef*    def_cache;

    // 是否已设置过数据（用于 DrawToBuffer 入口保护）
    bool            has_data;
};

// ========================================================================
// 公共 API
// ========================================================================

// 构造 / 重置控件
static void CellControl_Init(CellControl* ctrl)
{
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->data.action_id = 0;
    ctrl->data.target_id = 0;
    ctrl->data.position = -1;
    ctrl->data.count_alive = 0;
    ctrl->data.creature_def = nullptr;
    ctrl->data.creature_type = -1;
}

// 销毁控件（释放渲染缓冲区）
static void CellControl_Destroy(CellControl* ctrl)
{
    if (ctrl->buffer) {
        ctrl->buffer->Destroy();
        ctrl->buffer = nullptr;
    }
    ctrl->dirty = true;
}

// 绑定数据（从 H3CombatCreature 或测试数据）
static void CellControl_SetData(CellControl* ctrl, const CellData* data)
{
    if (!ctrl || !data) return;
    ctrl->data = *data;
    ctrl->def_cache = data->creature_def;
    ctrl->has_data = true;
    ctrl->dirty = true;
    // 收起展开的下拉框
    ctrl->action_expanded = false;
    ctrl->target_expanded = false;
    ctrl->action_hover_item = -1;
    ctrl->target_hover_item = -1;
}

// 获取格子绝对区域（格子左上角在面板上的坐标，由 SettingsDlg 传入）
// 这些值在 DrawCollapsed 和 DrawExpandedTo 之间保持不变
static RECT CellControl_GetCellRect(int cell_panel_x, int cell_panel_y)
{
    RECT rc = {};
    rc.left = cell_panel_x;
    rc.top  = cell_panel_y;
    rc.right  = rc.left + CC_CELL_W;
    rc.bottom = rc.top  + CC_CELL_H;
    return rc;
}

// 获取行动下拉框展开项的绝对区域（向下展开）
// 返回 false 表示没有展开
static bool CellControl_GetActionDropdownRect(int cell_panel_x, int cell_panel_y, RECT* out_rc)
{
    if (!out_rc) return false;
    out_rc->left   = cell_panel_x + CC_COMBO_X;
    out_rc->top    = cell_panel_y + CC_ROW1_Y + CC_ROW_H;         // 下拉项起始 Y
    out_rc->right  = out_rc->left + CC_COMBO_W;
    out_rc->bottom = out_rc->top  + MAX_ACTION_LABELS * CC_DROPDOWN_ITEM_H;
    return true; // 无论是否展开都返回 rect，由调用方判断展开状态
}

// 获取目标下拉框展开项的绝对区域（可能向下也可能向上展开）
// 获取目标下拉框展开项的绝对区域
// expand_up: true=向上展开, false=向下展开
static bool CellControl_GetTargetDropdownRect(int cell_panel_x, int cell_panel_y,
    RECT* out_rc, bool expand_up)
{
    if (!out_rc) return false;
    out_rc->left   = cell_panel_x + CC_COMBO_X;
    out_rc->right  = out_rc->left + CC_COMBO_W;
    if (expand_up) {
        out_rc->bottom = cell_panel_y + CC_ROW2_Y;
        out_rc->top    = out_rc->bottom - MAX_TARGET_LABELS * CC_DROPDOWN_ITEM_H;
    } else {
        out_rc->top    = cell_panel_y + CC_ROW2_Y + CC_ROW_H;
        out_rc->bottom = out_rc->top + MAX_TARGET_LABELS * CC_DROPDOWN_ITEM_H;
    }
    return true;
}

// ========================================================================
// 内部工具函数
// ========================================================================

// 获取或创建渲染缓冲区
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

// 将堆叠坐标 0-186 转为棋盘格标签 A01-H15
static void CellControl_FormatPosition(char* buf, int bufsize, int pos)
{
    if (pos < 0 || pos > 186) {
        strncpy(buf, "--", bufsize - 1);
        buf[bufsize - 1] = 0;
        return;
    }
    int col = pos % 8;
    int row = pos / 8 + 1;
    _snprintf(buf, bufsize, "%c%02d", 'A' + col, row);
}

// 绘制一行文字（自动处理 GBK 转换）
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

// 绘制带边框的按钮区域
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

// 绘制下拉箭头（展开/收起指示器）
static void CellControl_DrawArrow(H3LoadedPcx16* scr, int x, int y, bool down)
{
    if (down) {
        // 向下箭头 ▼：顶部最宽，逐行收窄到底部尖
        Fill(scr, x,     y,     7, 1, 235, 205, 116);
        Fill(scr, x + 1, y + 1, 5, 1, 235, 205, 116);
        Fill(scr, x + 2, y + 2, 3, 1, 235, 205, 116);
        Fill(scr, x + 3, y + 3, 1, 1, 235, 205, 116);
    } else {
        // 向上箭头 ▲：底部最宽，逐行收窄到顶部尖
        Fill(scr, x + 3, y,     1, 1, 235, 205, 116);
        Fill(scr, x + 2, y + 1, 3, 1, 235, 205, 116);
        Fill(scr, x + 1, y + 2, 5, 1, 235, 205, 116);
        Fill(scr, x,     y + 3, 7, 1, 235, 205, 116);
    }
}

// 用透明色（青色）填充整个缓冲区，透明叠加时会跳过这些像素。
// 16-bit cyan: R=0, G=63, B=31 → 0x7FDF;  32-bit cyan: 0xFF00FFFF
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

// ========================================================================
// 绘制：生物图标
// ========================================================================

// 城镇生物头像 DEF（TwCrPort.def 58x64），frame = 生物类型 ID + 2。
// 反编译确认：行动顺序条也用这套素材、不透明绘制。全局缓存一次。
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

// 绘制生物图标到格子缓冲区（图标区左上角 CC_ICON_X/Y）
// 官方权威（H3GarrisonInterface::DrawCreature 注释）：
//   TwCrPort.def 的 frame index = creature id + 2（前两帧是占位）。
// 这是游戏城镇/车库画生物头像用的标准方法。
static void CellControl_DrawCreatureIcon(CellControl* ctrl, H3LoadedPcx16* scr)
{
    if (ctrl->data.creature_type < 0) return;
    H3LoadedDef* def = CellControl_GetPortraitDef();
    if (!def) return;
    const int group = 0;
    const int frame = ctrl->data.creature_type + 2;
    // 深度校验：groups/groupsCount/activeGroups/frames 都必须有效，
    // 否则 DrawToPcx16 内部会解引用空指针崩溃（官方 H3API 也这样检查）。
    if (!def->groups || def->groupsCount <= group) return;
    if (!def->activeGroups || !def->activeGroups[group]) return;
    H3LoadedDef::DefGroup* grp = def->groups[group];
    if (!grp || grp->count <= frame || !grp->frames) return;
    if (!grp->frames[frame]) return;
    __try {
        // TwCrPort.def 头像四周有透明像素，直接画会透出格子青色底图。
        // 行动顺序条的做法是先有纯黑底板再画头像，所以这里先把图标区涂黑，
        // 再用 5 参版默认不透明绘制头像，透明区就落在黑底上，跟顺序条一致。
        Fill(scr, CC_ICON_X, CC_ICON_Y, CC_ICON_W, CC_ICON_H, 0, 0, 0);
        def->DrawToPcx16(group, frame, scr, CC_ICON_X, CC_ICON_Y);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    // 画金框，包住图标区（外扩 1px，60x54 画在 (CC_ICON_X-1, CC_ICON_Y-1)）
    H3LoadedPcx16* frameImg = LoadPanelPcx24_("HA_icon_frame.pcx",
        CC_ICON_FRAME_W, CC_ICON_FRAME_H, s_cc_icon_frame, s_cc_icon_frame_load_failed);
    if (frameImg) {
        DrawTransparentPcx_(frameImg, scr, CC_ICON_X - 1, CC_ICON_Y - 1);
    }
}

// ========================================================================
// 绘制：格子内容（收起状态）
// 绘制到格子自己的离屏缓冲区
// ========================================================================

// 绘制控件内容到缓冲区（格子尺寸，不含展开下拉框）
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

    // 清空缓冲区（青色透明）
    CellControl_ClearBuffer(scr);

    H3Font* fntS = GetSmallFont();
    if (!fntS) return;

    // ---- 绘制生物图标 ----
    // 用 TwCrPort.def（城镇生物头像 58x64），frame = 生物类型
    CellControl_DrawCreatureIcon(ctrl, scr);

    // ---- 行动下拉框（收起状态）----
    const char* action_label = ctrl->data.action_id >= 0
        && ctrl->data.action_id < MAX_ACTION_LABELS
        ? g_action_labels[ctrl->data.action_id] : "---";
    CellControl_DrawButtonBg(scr, CC_COMBO_X, CC_ROW1_Y, CC_COMBO_W, CC_ROW_H,
        ctrl->action_pressed, false);
    CellControl_DrawText(scr, fntS, action_label,
        CC_COMBO_X + 4, CC_ROW1_Y, CC_COMBO_W - 20, CC_ROW_H,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
    // 展开时箭头向上，收起时向下
    CellControl_DrawArrow(scr, CC_COMBO_X + CC_COMBO_W - 14,
        CC_ROW1_Y + CC_ROW_H / 2 - 3, !ctrl->action_expanded);

    // ---- 目标下拉框（收起状态）----
    const char* target_label = ctrl->data.target_id >= 0
        && ctrl->data.target_id < MAX_TARGET_LABELS
        ? g_target_labels[ctrl->data.target_id] : "---";
    CellControl_DrawButtonBg(scr, CC_COMBO_X, CC_ROW2_Y, CC_COMBO_W, CC_ROW_H,
        ctrl->target_pressed, false);
    CellControl_DrawText(scr, fntS, target_label,
        CC_COMBO_X + 4, CC_ROW2_Y, CC_COMBO_W - 20, CC_ROW_H,
        (INT32)eTextColor::LIGHT_GREEN, eTextAlignment::MIDDLE_LEFT);
    // 展开时箭头向上，收起时向下
    CellControl_DrawArrow(scr, CC_COMBO_X + CC_COMBO_W - 14,
        CC_ROW2_Y + CC_ROW_H / 2 - 3, !ctrl->target_expanded);

    // ---- 第一行：数量标注（右对齐，上移3px）----
    // 文字位置锚定原始图标高度 52，不随金框放大而下移（保持用户设定）
    const int label_row1_y = CC_ICON_Y + 52 + 1 - 3;               // 54
    const int label_row2_y = CC_ICON_Y + 52 + 1 + CC_LABEL_H - 1;  // 67

    char count_buf[16] = {};
    if (ctrl->data.count_alive > 0) {
        _snprintf(count_buf, sizeof(count_buf), "%d", ctrl->data.count_alive);
    } else {
        strncpy(count_buf, "--", sizeof(count_buf) - 1);
    }
    CellControl_DrawText(scr, fntS, count_buf,
        CC_ICON_X, label_row1_y, CC_ICON_W, CC_LABEL_H,
        0x01, eTextAlignment::TOP_RIGHT);

    // ---- 第二行：位置标注（左对齐，上移1px）----
    char pos_buf[8] = {};
    CellControl_FormatPosition(pos_buf, sizeof(pos_buf), ctrl->data.position);
    CellControl_DrawText(scr, fntS, pos_buf,
        CC_ICON_X, label_row2_y, CC_ICON_W, CC_LABEL_H,
        0x01, eTextAlignment::TOP_LEFT);

    ctrl->dirty = false;
}

// ========================================================================
// 绘制：展开的下拉项列表
// 直接绘制到面板复合缓冲区 scr（面板绝对坐标）
// cell_panel_x/y 是格子左上角在面板上的绝对坐标
// ========================================================================

// 绘制一行下拉项到面板（面板绝对坐标）
static void CellControl_DrawDropdownItem(H3LoadedPcx16* scr, H3Font* fnt,
    const char* label, int item_index,
    int panel_x, int panel_y,  // 格子左上角在面板上的绝对坐标
    bool is_selected, bool is_hovered)
{
    int item_x = panel_x + CC_COMBO_X;
    int item_y = panel_y + CC_ROW1_Y + CC_ROW_H + item_index * CC_DROPDOWN_ITEM_H;
    int item_w = CC_COMBO_W;
    int item_h = CC_DROPDOWN_ITEM_H;

    // 行动列表使用暖色主题：深棕底、琥珀选中、亮金悬停。
    BYTE bg_r, bg_g, bg_b;
    BYTE frame_r, frame_g, frame_b;
    INT32 text_color;
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
    Fill(scr, item_x, item_y, item_w, item_h, bg_r, bg_g, bg_b);
    scr->DrawFrame(item_x, item_y, item_w, item_h,
        frame_r, frame_g, frame_b);

    CellControl_DrawText(scr, fnt, label,
        item_x + 4, item_y, item_w - 20, item_h,
        text_color,
        eTextAlignment::MIDDLE_LEFT);
}

// 绘制展开的行动下拉列表到面板
static void CellControl_DrawActionDropdownTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y, int hover_idx)
{
    if (!ctrl || !ctrl->action_expanded || !scr) return;

    H3Font* fntS = GetSmallFont();
    if (!fntS) return;

    for (int i = 0; i < MAX_ACTION_LABELS; ++i) {
        bool is_selected = (i == ctrl->data.action_id);
        bool is_hovered  = (i == hover_idx);
        CellControl_DrawDropdownItem(scr, fntS,
            g_action_labels[i], i,
            cell_panel_x, cell_panel_y,
            is_selected, is_hovered);
    }

    // 展开时按钮上的箭头变为向上
    // 注意：按钮本身在格子里由 CellControl_DrawCollapsed 绘制
    // 这里只需要把箭头区域覆盖重画（展开状态指示）
}

// 绘制展开的目标下拉列表到面板
// mouse_local_x/y：鼠标相对于格子左上角
// expand_up：如果 true，下拉项向上展开（超出格子底部时使用）
static void CellControl_DrawTargetDropdownTo(CellControl* ctrl, H3LoadedPcx16* scr,
    int cell_panel_x, int cell_panel_y,
    int hover_idx,
    bool expand_up)
{
    if (!ctrl || !ctrl->target_expanded || !scr) return;

    H3Font* fntS = GetSmallFont();
    if (!fntS) return;

    // 下拉项起始 Y（相对于格子左上角）
    const int list_top = expand_up
        ? CC_ROW2_Y - MAX_TARGET_LABELS * CC_DROPDOWN_ITEM_H
        : CC_ROW2_Y + CC_ROW_H;

    for (int i = 0; i < MAX_TARGET_LABELS; ++i) {
        bool is_selected = (i == ctrl->data.target_id);
        bool is_hovered  = (i == hover_idx);
        int item_x = cell_panel_x + CC_COMBO_X;
        int item_y = cell_panel_y + list_top + i * CC_DROPDOWN_ITEM_H;
        int item_w = CC_COMBO_W;
        int item_h = CC_DROPDOWN_ITEM_H;

        // 目标列表使用冷色主题：深蓝灰底、青蓝选中、亮青悬停。
        BYTE bg_r, bg_g, bg_b;
        BYTE frame_r, frame_g, frame_b;
        INT32 text_color;
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
        Fill(scr, item_x, item_y, item_w, item_h, bg_r, bg_g, bg_b);
        scr->DrawFrame(item_x, item_y, item_w, item_h,
            frame_r, frame_g, frame_b);

        CellControl_DrawText(scr, fntS, g_target_labels[i],
            item_x + 4, item_y, item_w - 20, item_h,
            text_color,
            eTextAlignment::MIDDLE_LEFT);
    }
}

// ========================================================================
// 命中测试
// ========================================================================

enum CellHitArea
{
    CELL_HIT_NONE    = 0,
    CELL_HIT_ICON    = 1,   // 左侧图标区
    CELL_HIT_ACTION  = 2,   // 行动下拉框
    CELL_HIT_TARGET  = 3,   // 目标下拉框
    CELL_HIT_DROP_ACTION = 4, // 行动下拉展开项
    CELL_HIT_DROP_TARGET = 5, // 目标下拉展开项
};

static CellHitArea CellControl_HitTestInCell(CellControl* ctrl, int local_x, int local_y,
    bool expand_up_for_target)
{
    // 行动下拉框区域（收起时）
    if (!ctrl->action_expanded
        && local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W
        && local_y >= CC_ROW1_Y && local_y < CC_ROW1_Y + CC_ROW_H)
    {
        return CELL_HIT_ACTION;
    }
    // 目标下拉框区域（收起时）
    if (!ctrl->target_expanded
        && local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W
        && local_y >= CC_ROW2_Y && local_y < CC_ROW2_Y + CC_ROW_H)
    {
        return CELL_HIT_TARGET;
    }
    // 行动下拉展开项
    if (ctrl->action_expanded
        && local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W)
    {
        int rel_y = local_y - (CC_ROW1_Y + CC_ROW_H);
        if (rel_y >= 0) {
            int idx = rel_y / CC_DROPDOWN_ITEM_H;
            if (idx >= 0 && idx < MAX_ACTION_LABELS)
                return CELL_HIT_DROP_ACTION;
        }
    }
    // 目标下拉展开项（根据空间向上或向下展开）
    if (ctrl->target_expanded
        && local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W)
    {
        const int list_top = expand_up_for_target
            ? CC_ROW2_Y - MAX_TARGET_LABELS * CC_DROPDOWN_ITEM_H
            : CC_ROW2_Y + CC_ROW_H;
        int rel_y = local_y - list_top;
        if (rel_y >= 0) {
            int idx = rel_y / CC_DROPDOWN_ITEM_H;
            if (idx >= 0 && idx < MAX_TARGET_LABELS)
                return CELL_HIT_DROP_TARGET;
        }
    }
    // 图标区域（左侧，矩形稍大）
    if (local_x >= CC_ICON_X && local_x < CC_ICON_X + CC_ICON_W
        && local_y >= CC_ICON_Y && local_y < CC_ICON_Y + CC_ICON_H + CC_LABEL_H)
    {
        return CELL_HIT_ICON;
    }
    return CELL_HIT_NONE;
}

// ========================================================================
// 鼠标输入处理
// 返回值：是否消费了该消息
// mouse_local_x/y：相对于格子左上角的坐标
// expand_up_for_target：目标下拉是否向上展开
// ========================================================================

static bool CellControl_OnMouse(CellControl* ctrl, int msg_type,
    int mouse_local_x, int mouse_local_y, bool expand_up_for_target)
{
    // msg_type: 4=WM_LBUTTONDOWN, 8=WM_LBUTTONUP
    CellHitArea hit = CellControl_HitTestInCell(ctrl,
        mouse_local_x, mouse_local_y, expand_up_for_target);

    if (msg_type == 4) { // LButtonDown
        if (hit == CELL_HIT_ACTION) {
            ctrl->action_expanded = !ctrl->action_expanded;
            ctrl->target_expanded = false;
            ctrl->action_pressed = true;
            ctrl->dirty = true;
            return true;
        }
        if (hit == CELL_HIT_TARGET) {
            ctrl->target_expanded = !ctrl->target_expanded;
            ctrl->action_expanded = false;
            ctrl->target_pressed = true;
            ctrl->dirty = true;
            return true;
        }
        // 展开列表项在按下阶段只消费，不收起；松开阶段再执行选择。
        // 否则按下会先关闭列表，导致松开无法选择且继续穿透到下方格子。
        if (hit == CELL_HIT_DROP_ACTION || hit == CELL_HIT_DROP_TARGET)
            return true;
        // 点击空白区域：收起下拉框
        if (ctrl->action_expanded || ctrl->target_expanded) {
            ctrl->action_expanded = false;
            ctrl->target_expanded = false;
            ctrl->dirty = true;
        }
    }

    if (msg_type == 8) { // LButtonUp
        if (hit == CELL_HIT_DROP_ACTION && ctrl->action_expanded) {
            int rel_y = mouse_local_y - (CC_ROW1_Y + CC_ROW_H);
            int idx = rel_y / CC_DROPDOWN_ITEM_H;
            if (idx >= 0 && idx < MAX_ACTION_LABELS) {
                ctrl->data.action_id = idx;
                ctrl->action_expanded = false;
                ctrl->action_pressed = false;
                ctrl->dirty = true;
                SyncStrategyBack_(ctrl->data.army_slot_ix, ctrl->data.action_id, ctrl->data.target_id);
                return true;
            }
        }
        if (hit == CELL_HIT_DROP_TARGET && ctrl->target_expanded) {
            // 如果是向上展开，需要重新计算索引
            const int list_top = expand_up_for_target
                ? CC_ROW2_Y - MAX_TARGET_LABELS * CC_DROPDOWN_ITEM_H
                : CC_ROW2_Y + CC_ROW_H;
            int rel_y = mouse_local_y - list_top;
            int idx = rel_y / CC_DROPDOWN_ITEM_H;
            if (idx >= 0 && idx < MAX_TARGET_LABELS) {
                ctrl->data.target_id = idx;
                ctrl->target_expanded = false;
                ctrl->target_pressed = false;
                ctrl->dirty = true;
                SyncStrategyBack_(ctrl->data.army_slot_ix, ctrl->data.action_id, ctrl->data.target_id);
                return true;
            }
        }
        ctrl->action_pressed = false;
        ctrl->target_pressed = false;
        ctrl->dirty = true;
    }

    return false;
}

#endif // _CELL_CONTROL_INC_CPP
