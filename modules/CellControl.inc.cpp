// ========== CellControl.inc.cpp ==========
// 格子控件：左侧生物图标（带位置和数量），右侧两行下拉框（行动+目标）
// 不直接渲染到面板，而是画到自己的离屏缓冲区，由外部调用 Draw() 传入目标 surface。

#ifndef _CELL_CONTROL_INC_CPP
#define _CELL_CONTROL_INC_CPP

static void WriteLog(const char* fmt, ...);

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
static const int CC_ICON_H   = 58;

// 图标下方标注区域
static const int CC_LABEL_H  = 12;

// 右侧控件区域（紧贴左侧图标右边）
static const int CC_COMBO_X  = CC_ICON_X + CC_ICON_W + 4;  // 66
static const int CC_COMBO_W  = CC_CELL_W - CC_COMBO_X - 4;  // 123
static const int CC_ROW1_Y   = 4;   // 第一行下拉框 Y（顶部对齐）
static const int CC_ROW_H    = 22;  // 每行高度
static const int CC_ROW_GAP  = 2;   // 两行间距
static const int CC_ROW2_Y   = CC_ROW1_Y + CC_ROW_H + CC_ROW_GAP; // 28
static const int CC_LABEL_Y  = CC_ROW2_Y + CC_ROW_H + 2;  // 52，底部位置标注

// 下拉框展开高度
static const int CC_DROPDOWN_H = 80;

// 控件总高度（格子高度不变，下拉框展开时超出格子高度）
static const int CC_TOTAL_H = CC_CELL_H;

// ========================================================================
// 内部状态
// ========================================================================

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
    ctrl->data = *data;
    ctrl->def_cache = data->creature_def;
    ctrl->dirty = true;
    // 收起展开的下拉框
    ctrl->action_expanded = false;
    ctrl->target_expanded = false;
    ctrl->action_hover_item = -1;
    ctrl->target_hover_item = -1;
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

// 判断是否为远程生物
static bool CellControl_IsRanged(INT32 creature_type)
{
    if (creature_type < 0) return false;
    // H3CreatureInformation::isShooter() 位字段
    // 从 creature info 表中查（简化版本：hardcode 常见远程）
    // 这里直接返回 false，由调用方通过 creature_def/info 判断
    // 暂时留空
    return false;
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

// 绘制下拉框（普通状态 + 展开状态）
static void CellControl_DrawCombobox(H3LoadedPcx16* scr, H3Font* fnt,
    const char* selected_label, bool expanded, int dropdown_count,
    int x, int y, int w, int h,
    INT32 text_color, bool is_hovered, bool is_pressed)
{
    CellControl_DrawButtonBg(scr, x, y, w, h, is_pressed, is_hovered);
    CellControl_DrawText(scr, fnt, selected_label, x + 4, y, w - 20, h,
        text_color, eTextAlignment::MIDDLE_LEFT);

    // 展开箭头
    int arrow_x = x + w - 14;
    int arrow_y = y + h / 2 - 3;
    Fill(scr, arrow_x + 3, arrow_y,     1, 1, 235, 205, 116);
    Fill(scr, arrow_x + 2, arrow_y + 1, 3, 1, 235, 205, 116);
    Fill(scr, arrow_x + 1, arrow_y + 2, 5, 1, 235, 205, 116);
    Fill(scr, arrow_x,     arrow_y + 3, 7, 1, 235, 205, 116);
    Fill(scr, arrow_x + 1, arrow_y + 4, 5, 1, 235, 205, 116);
    Fill(scr, arrow_x + 2, arrow_y + 5, 3, 1, 235, 205, 116);
    Fill(scr, arrow_x + 3, arrow_y + 6, 1, 1, 235, 205, 116);

    // 展开时绘制下拉列表（只绘制可见部分，超出格子高度的部分由调用方裁剪）
    if (expanded && dropdown_count > 0) {
        const int item_h = 18;
        const int visible_items = h; // 超出格子部分会被面板裁掉
        for (int i = 0; i < dropdown_count; i++) {
            int iy = y + h + i * item_h;
            bool item_hovered = false; // TODO: 鼠标悬停检测
            CellControl_DrawButtonBg(scr, x, iy, w, item_h, false, item_hovered);
            // 标签由调用方传入，这里只画背景
        }
    }
}

// 绘制生物图标（用 H3LoadedDef 的 frame 0）
static void CellControl_DrawCreatureIcon(CellControl* ctrl, H3LoadedPcx16* scr)
{
    H3LoadedDef* def = ctrl->def_cache;
    if (!def || !def->groups) return;

    // 从 def 的 frame 0 绘制到目标区域
    // 使用 CreatureLarge (58x64) 的画法：找 group=0 frame=0
    // 很多 def 的第一帧就是站立姿态
    def->DrawToPcx16(0, 0, scr, CC_ICON_X, CC_ICON_Y);
}

// ========================================================================
// 主绘制函数
// ========================================================================

// 绘制控件内容到缓冲区（格子尺寸，不含展开下拉框）
// 外部负责在适当时机调用，并负责将缓冲区 Blt 到面板
static void CellControl_DrawToBuffer(CellControl* ctrl)
{
    H3LoadedPcx16* scr = CellControl_EnsureBuffer(ctrl);
    if (!scr || !scr->buffer) return;

    // 清空缓冲区（透明）
    memset(scr->buffer, 0, static_cast<size_t>(scr->scanlineSize) * CC_CELL_H);

    H3Font* fntS = GetSmallFont();
    if (!fntS) return;

    // ---- 绘制生物图标 ----
    CellControl_DrawCreatureIcon(ctrl, scr);

    // ---- 行动下拉框 ----
    const char* action_label = ctrl->data.action_id >= 0
        && ctrl->data.action_id < MAX_ACTION_LABELS
        ? g_action_labels[ctrl->data.action_id] : "---";
    CellControl_DrawCombobox(scr, fntS, action_label,
        ctrl->action_expanded, MAX_ACTION_LABELS,
        CC_COMBO_X, CC_ROW1_Y, CC_COMBO_W, CC_ROW_H,
        0x1A, false, ctrl->action_pressed);

    // ---- 目标下拉框 ----
    const char* target_label = ctrl->data.target_id >= 0
        && ctrl->data.target_id < MAX_TARGET_LABELS
        ? g_target_labels[ctrl->data.target_id] : "---";
    CellControl_DrawCombobox(scr, fntS, target_label,
        ctrl->target_expanded, MAX_TARGET_LABELS,
        CC_COMBO_X, CC_ROW2_Y, CC_COMBO_W, CC_ROW_H,
        0x0D, false, ctrl->target_pressed);

    // ---- 位置标注（左下角）----
    char pos_buf[8] = {};
    CellControl_FormatPosition(pos_buf, sizeof(pos_buf), ctrl->data.position);
    CellControl_DrawText(scr, fntS, pos_buf,
        CC_ICON_X, CC_ICON_Y + CC_ICON_H + 1, CC_ICON_W, CC_LABEL_H,
        0x01, eTextAlignment::BOTTOM_LEFT);

    // ---- 数量标注（右下角，图标右下）----
    char count_buf[16] = {};
    if (ctrl->data.count_alive > 0) {
        _snprintf(count_buf, sizeof(count_buf), "%d", ctrl->data.count_alive);
    } else {
        strncpy(count_buf, "--", sizeof(count_buf) - 1);
    }
    // 数量标注位置：图标区域右下角
    int count_text_w = CC_ICON_W;
    CellControl_DrawText(scr, fntS, count_buf,
        CC_ICON_X, CC_ICON_Y + CC_ICON_H - CC_LABEL_H - 1, count_text_w, CC_LABEL_H,
        0x01, eTextAlignment::BOTTOM_RIGHT);

    ctrl->dirty = false;
}

// ========================================================================
// 鼠标输入处理
// ========================================================================

// 命中测试：返回命中的是哪个区域
enum CellHitArea
{
    CELL_HIT_NONE       = 0,
    CELL_HIT_ICON       = 1,    // 左侧图标区
    CELL_HIT_ACTION     = 2,    // 行动下拉框
    CELL_HIT_TARGET     = 3,    // 目标下拉框
};

static CellHitArea CellControl_HitTest(CellControl* ctrl, int local_x, int local_y)
{
    // 行动下拉框
    if (local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W
        && local_y >= CC_ROW1_Y && local_y < CC_ROW1_Y + CC_ROW_H)
    {
        return CELL_HIT_ACTION;
    }
    // 目标下拉框
    if (local_x >= CC_COMBO_X && local_x < CC_COMBO_X + CC_COMBO_W
        && local_y >= CC_ROW2_Y && local_y < CC_ROW2_Y + CC_ROW_H)
    {
        return CELL_HIT_TARGET;
    }
    // 图标区域（左侧）
    if (local_x >= CC_ICON_X && local_x < CC_ICON_X + CC_ICON_W
        && local_y >= CC_ICON_Y && local_y < CC_ICON_Y + CC_ICON_H + CC_LABEL_H)
    {
        return CELL_HIT_ICON;
    }
    return CELL_HIT_NONE;
}

// 点击下拉框内部的展开项，返回是否命中并修改了选择
static bool CellControl_HitDropdownItem(CellControl* ctrl, CellHitArea area,
    int local_x, int local_y)
{
    if (area == CELL_HIT_ACTION) {
        if (local_y >= CC_ROW1_Y + CC_ROW_H) {
            int idx = (local_y - (CC_ROW1_Y + CC_ROW_H)) / 18;
            if (idx >= 0 && idx < MAX_ACTION_LABELS) {
                ctrl->data.action_id = idx;
                ctrl->action_expanded = false;
                ctrl->dirty = true;
                return true;
            }
        }
    } else if (area == CELL_HIT_TARGET) {
        if (local_y >= CC_ROW2_Y + CC_ROW_H) {
            int idx = (local_y - (CC_ROW2_Y + CC_ROW_H)) / 18;
            if (idx >= 0 && idx < MAX_TARGET_LABELS) {
                ctrl->data.target_id = idx;
                ctrl->target_expanded = false;
                ctrl->dirty = true;
                return true;
            }
        }
    }
    return false;
}

// 处理鼠标消息，返回是否消费了该消息
// local_x/y 是相对于格子左上角的坐标
static bool CellControl_OnMouse(CellControl* ctrl, int msg_type,
    int local_x, int local_y)
{
    // msg_type: 4=WM_LBUTTONDOWN, 8=WM_LBUTTONUP, 16=WM_RBUTTONDOWN
    CellHitArea hit = CellControl_HitTest(ctrl, local_x, local_y);

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
        // 点击空白区域：收起下拉框
        if (ctrl->action_expanded || ctrl->target_expanded) {
            ctrl->action_expanded = false;
            ctrl->target_expanded = false;
            ctrl->dirty = true;
        }
    }

    if (msg_type == 8) { // LButtonUp
        if (ctrl->action_pressed && hit == CELL_HIT_ACTION) {
            // 命中下拉框项
            if (CellControl_HitDropdownItem(ctrl, hit, local_x, local_y)) {
                ctrl->action_pressed = false;
                return true;
            }
        }
        if (ctrl->target_pressed && hit == CELL_HIT_TARGET) {
            if (CellControl_HitDropdownItem(ctrl, hit, local_x, local_y)) {
                ctrl->target_pressed = false;
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
