// ========== SettingsDlg.inc.cpp ==========
// 战场自动化 - 设置面板
// 渲染：LoHook 0x600430 画面板到 screenPcx16
// 输入捕获：检测"自动战斗"对话框的关闭事件

#define o_WndMgr (*reinterpret_cast<H3WindowManager**>(0x6992D0))
#define o_DDSurfaceBackBuffer (*reinterpret_cast<LPDIRECTDRAWSURFACE*>(0x6AAD28))

// ========================================================================
// 第一部分：数据声明
// ========================================================================

static const int PANEL_W    = 680;
static const int PANEL_H    = 480;
static const int COLS       = 3;
static const int CELL_W     = 193;
static const int CELL_H     = 96;
static const int CELL_STEP_X = CELL_W - 2;
static const int CELL_STEP_Y = CELL_H - 2;
static const int GRID_X      = 36;
static const int GRID_Y      = 80;
static const int SCROLL_X    = GRID_X + CELL_W + (COLS - 1) * CELL_STEP_X + 18;
static const int SCROLL_Y    = GRID_Y;
static const int SCROLL_W    = 16;
static const int SCROLL_H    = CELL_H + 3 * CELL_STEP_Y;
static const int MARGIN      = 20;
static const int TITLE_H    = 44;
static const int BTN_W      = 64;
static const int BTN_H      = 30;
static const int BTN_FRAME_W = 66;
static const int BTN_FRAME_H = 32;
static const int BTN_GAP    = 24;
static const int BTN_Y      = PANEL_H - 56;
static const int OK_X       = (PANEL_W - BTN_GAP) / 2 - BTN_W;
static const int CANCEL_X   = (PANEL_W + BTN_GAP) / 2;
static const int CELL_COUNT = COLS * 4;
static const int MAX_STACKS = 21;
static const int PROFILE_COUNT = 5;
static const int PROFILE_BTN_W = 104;
static const int PROFILE_BTN_H = 18;
static const int PROFILE_BTN_GAP = 8;
static const int PROFILE_BTN_Y = 47;
static const int PROFILE_BTNS_W = PROFILE_COUNT * PROFILE_BTN_W
    + (PROFILE_COUNT - 1) * PROFILE_BTN_GAP;
static const int PROFILE_BTN_X = (PANEL_W - PROFILE_BTNS_W) / 2;

// 网格金框（HA_grid_frame.pcx 624x342），框住 4 行格子 + 右侧滚动条。
// 整个滚动区域较原布局下移10px，为上方5个方案按钮留出空间。
static const int GRID_FRAME_W = 624;
static const int GRID_FRAME_H = 342;
static const int GRID_FRAME_X = 29;
static const int GRID_FRAME_Y = 72;

// 硬编码默认标签（INI 加载失败时使用）
static const char* DEFAULT_ACTION_LABELS[AA_COUNT] = {
    "手动", "防御", "等待", "移动", "近战攻击", "远程攻击", "急救治疗", "投石车攻击",
};
static const char* DEFAULT_SIDE_LABELS[ATS_COUNT] = {
    "靠近己方", "靠近敌方", "双方",
};
static const char* DEFAULT_SELECTOR_LABELS[SEL_COUNT] = {
    "指定目标", "随机", "顺序", "最近", "最远",
    "远程高速优先", "数量最多", "数量最少", "伤最重",
};

// 运行时标签
const char* g_action_labels[AA_COUNT] = {};
const char* g_side_labels[ATS_COUNT] = {};
const char* g_selector_labels[SEL_COUNT] = {};
static char g_panel_title[64] = {};
static bool g_labels_loaded = false;

static void LoadLabelArray_(const char* section, const char** defaults,
    const char** out_labels, char storage[][64], int count, const char* ini_path)
{
    for (int i = 0; i < count; i++) {
        char key[16] = {};
        char buf[64] = {};
        sprintf(key, "%d", i);
        GetPrivateProfileStringA(section, key, defaults[i],
            buf, sizeof(buf), ini_path);
        strncpy(storage[i], buf, 63);
        storage[i][63] = 0;
        out_labels[i] = storage[i];
    }
}

// 从 H3Auto.ini 加载面板标签
static void LoadLabels_(const char* ini_path)
{
    if (g_labels_loaded) return;
    g_labels_loaded = true;
    static char storage_action[AA_COUNT][64] = {};
    static char storage_side[ATS_COUNT][64] = {};
    static char storage_selector[SEL_COUNT][64] = {};
    LoadLabelArray_("Actions", DEFAULT_ACTION_LABELS, g_action_labels,
        storage_action, AA_COUNT, ini_path);
    LoadLabelArray_("Sides", DEFAULT_SIDE_LABELS, g_side_labels,
        storage_side, ATS_COUNT, ini_path);
    LoadLabelArray_("Selectors", DEFAULT_SELECTOR_LABELS, g_selector_labels,
        storage_selector, SEL_COUNT, ini_path);
    WriteLog("[Panel] 标签已从 %s 加载。", ini_path);
    GetPrivateProfileStringA("Panel", "Title", "部队自动行动设置",
        g_panel_title, sizeof(g_panel_title), ini_path);
}


static const INT32 COL_TITLE_TEXT  = 0x03;
static const INT32 COL_TEXT        = 0x01;
static const INT32 COL_ACTION_TEXT = 0x1A;
static const INT32 COL_TARGET_TEXT = 0x0D;

static struct Panel {
    bool active;
    int x, y;
    AutoStackRule draft_rules[PROFILE_COUNT][MAX_STACKS];
    int selected_profile;
    int pressed_profile;
    int count;
    int scroll_row;
    bool scroll_dragging;
    int scroll_drag_offset;
    int scroll_button_pressed;
    bool cursor_saved;
    int saved_cursor_type;
    int saved_cursor_frame;
    int pressed_button;
    int hover_cell;   // 下拉展开时鼠标悬停的格子索引，-1=无
    int hover_idx;    // 下拉展开时鼠标悬停的项索引，-1=无
    CellControl cells[CELL_COUNT];
} s_p = {};

// 与 H3BattleValueInfo 远程对比框相同：先离屏合成，再一次性写入 backbuffer。
static H3LoadedPcx16* s_panel_composite = nullptr;
static H3LoadedPcx16* s_panel_background = nullptr;
static H3LoadedPcx16* s_panel_cell = nullptr;
static H3LoadedPcx16* s_panel_ok_normal = nullptr;
static H3LoadedPcx16* s_panel_ok_pressed = nullptr;
static H3LoadedPcx16* s_panel_cancel_normal = nullptr;
static H3LoadedPcx16* s_panel_cancel_pressed = nullptr;
static H3LoadedPcx16* s_panel_button_frame = nullptr;
static H3LoadedPcx16* s_panel_grid_frame = nullptr;
static bool s_panel_background_load_failed = false;
static bool s_panel_cell_load_failed = false;
static bool s_panel_grid_frame_load_failed = false;
static bool s_panel_ok_normal_load_failed = false;
static bool s_panel_ok_pressed_load_failed = false;
static bool s_panel_cancel_normal_load_failed = false;
static bool s_panel_cancel_pressed_load_failed = false;
static bool s_panel_button_frame_load_failed = false;
static bool s_panel_redraw_in_progress = false;
static bool s_panel_modal_suspended = false;
static Patch* s_hover_patch_primary = nullptr;
static Patch* s_hover_patch_secondary = nullptr;

// HD_SOD.dll 高亮屏蔽：patch HD 战斗消息钩子 FUN_010d9ce0 (RVA 0xD9CE0) 为 ret 12
static HMODULE s_hd_sod_module = nullptr;
static Patch* s_hd_msgproc_patch = nullptr;

// 面板打开时安装 WH_KEYBOARD 钩子，立即响应 ESC/Enter，不依赖游戏帧率
static void CommitAndCloseSettingsPanel_();
static HHOOK s_kb_hook = nullptr;

static LRESULT CALLBACK PanelKbHook_(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && s_p.active && !s_panel_modal_suspended
        && !(lParam & 0x80000000))  // keydown only
    {
        if (wParam == VK_ESCAPE) {
            CloseSettingsPanel();
            return 1;  // cancel and swallow the key
        }
        if (wParam == VK_RETURN) {
            CommitAndCloseSettingsPanel_();
            return 1;  // commit and swallow the key
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// 面板打开时安装 WH_MOUSE 钩子，鼠标移动立即刷新下拉悬停高亮，不依赖游戏帧率
static HHOOK s_mouse_hook = nullptr;
static bool UpdateDropdownHover_(int px, int py);  // 前向声明（钩子先用到）

static LRESULT CALLBACK PanelMouseHook_(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && s_p.active && !s_panel_modal_suspended
        && wParam == WM_MOUSEMOVE)
    {
        // 有下拉展开时才需要即时刷新高亮
        bool any_expanded = false;
        for (int i = 0; i < CELL_COUNT; ++i) {
            if (s_p.cells[i].expanded != CEX_NONE) {
                any_expanded = true;
                break;
            }
        }
        if (any_expanded) {
            const MOUSEHOOKSTRUCT* ms = reinterpret_cast<const MOUSEHOOKSTRUCT*>(lParam);
            if (ms) {
                HWND game_wnd = *reinterpret_cast<HWND*>(0x699650);
                POINT pt = ms->pt;
                RECT client = {};
                if (game_wnd && ScreenToClient(game_wnd, &pt)
                    && GetClientRect(game_wnd, &client)
                    && o_WndMgr && o_WndMgr->screenPcx16
                    && client.right > client.left && client.bottom > client.top)
                {
                    // WH_MOUSE 给出窗口客户区像素坐标；HD 模式下客户区会缩放，
                    // 而 CellRect/s_p 使用 screenPcx16 的游戏逻辑坐标。
                    const int game_x = MulDiv(pt.x - client.left,
                        o_WndMgr->screenPcx16->width, client.right - client.left);
                    const int game_y = MulDiv(pt.y - client.top,
                        o_WndMgr->screenPcx16->height, client.bottom - client.top);
                    const int hpx = game_x - s_p.x;
                    const int hpy = game_y - s_p.y;
                    UpdateDropdownHover_(hpx, hpy);
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
static const UINT s_right_click_dialog_vtable = 0x0063DB40;
// 真正的战斗窗口。0x0063A5E4 是战斗底下仍保留的冒险地图窗口。
static const UINT s_combat_dialog_vtable = 0x0063D528;
// BattleUI 底栏 ICM004.def：点击分支切换 H3CombatManager::autoCombat。
static const INT32 s_autofight_button_id = 0x7D4;
// 战斗中弹出过设置面板
static bool s_panel_popup_done = false;
// 是否检测到 BattleUI 上层的自动战斗右键说明框
static bool s_saw_explanation_dlg_in_battle = false;
// 右键按下时鼠标确实位于自动战斗按钮上。
static bool s_autofight_right_press_armed = false;
// BattleUI 连续缺失帧数，避免对话框切换的瞬时空帧误判为战斗结束
static int s_battle_ui_missing_frames = 0;

// 前向声明
static void OpenSettingsPanel_();
static void CommitAndCloseSettingsPanel_();
void CloseSettingsPanel();
static void DrawPanelToBuffer_();
static void HandlePanelInput_();
static void HandlePanelMouseMessage_(int raw_command, int screen_x, int screen_y);
static bool UpdateDropdownHover_(int px, int py);
static bool BlockBattleHover_();
static void RestoreBattleHover_();
static void UpdatePanelModalSuspension_();
static void SetPanelScrollRow_(int row);
static bool PointInRect_(int x, int y, int left, int top, int width, int height);
static bool IsGameWindowForeground_();
static bool IsGameMouseInputActive_();
static void CancelPanelTransientInput_();

struct BattleInputBlocker
{
    H3BaseDlg* battle_ui;
    H3DlgTransparentItem* item;
    void** original_vtable;
    void* local_vtable[14];
};

static BattleInputBlocker s_input_blocker = {};

static INT __fastcall BlockBattleItemMessage_(H3DlgItem*, int, H3Msg& msg)
{
    const bool panel_was_active = s_p.active && !s_panel_modal_suspended;
    const int raw_command = static_cast<int>(msg.command);
    const bool mouse_command = raw_command == 4 || raw_command == 8
        || raw_command == 16
        || raw_command == static_cast<int>(eMsgCommand::MOUSE_WHEEL);
    if (panel_was_active && mouse_command && !IsGameMouseInputActive_()) {
        // Losing focus can leave a stale in-game cursor coordinate in the
        // translated packet. Never let an outside click complete a button or drag.
        CancelPanelTransientInput_();
        return msg.StopProcessing();
    }
    if (panel_was_active && raw_command == static_cast<int>(eMsgCommand::MOUSE_WHEEL)) {
        const int old_row = s_p.scroll_row;
        const int wheel_delta = static_cast<int>(msg.subtype);
        if (wheel_delta < 0)
            SetPanelScrollRow_(s_p.scroll_row + 1);
        else if (wheel_delta > 0)
            SetPanelScrollRow_(s_p.scroll_row - 1);
        if (s_p.scroll_row != old_row)
            DrawPanelToBuffer_();
    }
    if (panel_was_active && (raw_command == 4 || raw_command == 8 || raw_command == 16)) {
        // Item vProcessMsg receives the pre-translation mouse packet:
        // command is WM-derived and coordinates are stored at +4/+8.
        HandlePanelMouseMessage_(raw_command,
            static_cast<int>(msg.subtype), msg.itemId);
    }
    return panel_was_active ? msg.StopProcessing() : 0;
}

static void ForcePanelDefaultCursor_()
{
    if (!s_p.active) return;
    H3MouseManager* mouse = H3MouseManager::Get();
    if (!mouse) return;
    if (mouse->GetType() != 0 || mouse->GetFrame() != 0)
        mouse->DefaultCursor();
}

// ========================================================================
// 第二部分：工具函数
// ========================================================================

static H3Font* GetPanelFont() { return H3Font::Load("bigfont.fnt"); }
static H3Font* GetSmallFont() { return H3Font::Load("smalfont.fnt"); }

static bool IsGameWindowForeground_()
{
    HWND game_window = *reinterpret_cast<HWND*>(0x699650);
    if (!game_window || IsIconic(game_window)) return false;

    HWND foreground = GetForegroundWindow();
    return foreground
        && GetAncestor(foreground, GA_ROOT) == GetAncestor(game_window, GA_ROOT);
}

static bool IsGameMouseInputActive_()
{
    if (!IsGameWindowForeground_()) return false;
    HWND game_window = *reinterpret_cast<HWND*>(0x699650);

    POINT cursor = {};
    RECT client = {};
    if (!GetCursorPos(&cursor) || !ScreenToClient(game_window, &cursor)
        || !GetClientRect(game_window, &client))
    {
        return false;
    }
    return PtInRect(&client, cursor) != FALSE;
}

static void CancelPanelTransientInput_()
{
    s_p.pressed_button = 0;
    s_p.pressed_profile = -1;
    s_p.scroll_button_pressed = 0;
    s_p.scroll_dragging = false;
}

static RECT ProfileButtonRect_(int profile)
{
    RECT rc = {};
    if (profile < 0 || profile >= PROFILE_COUNT) return rc;
    rc.left = PROFILE_BTN_X + profile * (PROFILE_BTN_W + PROFILE_BTN_GAP);
    rc.top = PROFILE_BTN_Y;
    rc.right = rc.left + PROFILE_BTN_W;
    rc.bottom = rc.top + PROFILE_BTN_H;
    return rc;
}

static RECT CellRect(int idx)
{
    RECT rc = {};
    if (idx < 0 || idx >= CELL_COUNT) return rc;
    rc.left   = GRID_X + (idx % COLS) * CELL_STEP_X;
    rc.top    = GRID_Y + (idx / COLS) * CELL_STEP_Y;
    rc.right  = rc.left + CELL_W;
    rc.bottom = rc.top + CELL_H;
    return rc;
}

static RECT ActionBtnRect(int idx)
{
    RECT rc = CellRect(idx);
    rc.left += 4; rc.right -= 4;
    rc.top += 23;
    rc.bottom = rc.top + 22;
    return rc;
}

static RECT TargetBtnRect(int idx)
{
    RECT rc = CellRect(idx);
    rc.left += 4; rc.right -= 4;
    rc.top += 52;
    rc.bottom = rc.top + 22;
    return rc;
}

static int PanelMaxScrollRow_()
{
    const int total_rows = (s_p.count + COLS - 1) / COLS;
    const int visible_rows = CELL_COUNT / COLS;
    return total_rows > visible_rows ? total_rows - visible_rows : 0;
}

static int PanelScrollButtonSize_()
{
    return 16;
}

static int PanelScrollThumbY_()
{
    const int button_size = PanelScrollButtonSize_();
    const int free_size = SCROLL_H - 3 * button_size;
    const int max_row = PanelMaxScrollRow_();
    return SCROLL_Y + button_size
        + (max_row > 0 ? free_size * s_p.scroll_row / max_row : 0);
}

static void SetPanelScrollRow_(int row)
{
    const int max_row = PanelMaxScrollRow_();
    if (row < 0) row = 0;
    if (row > max_row) row = max_row;
    s_p.scroll_row = row;
}

static void Fill(H3LoadedPcx16* scr, int x, int y, int w, int h, int r, int g, int b)
{
    if (w <= 0 || h <= 0) return;
    scr->FillRectangle(x, y, w, h, (BYTE)r, (BYTE)g, (BYTE)b);
}

static void DrawTxt(H3LoadedPcx16* scr, H3Font* fnt, const char* text,
    int x, int y, int w, int h, INT32 color,
    eTextAlignment align = eTextAlignment::MIDDLE_CENTER)
{
    if (!fnt || !text || w <= 0 || h <= 0) return;

    // The project uses UTF-8 source files, while the Chinese game font expects
    // GBK byte sequences. ASCII can be passed through unchanged.
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

static WORD PanelRGB888To565_(int r, int g, int b)
{
    return (WORD)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

static DWORD PanelRGB565To8888_(WORD color)
{
    const int r = ((color >> 11) & 0x1F) << 3;
    const int g = ((color >> 5) & 0x3F) << 2;
    const int b = (color & 0x1F) << 3;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static WORD PanelRGB8888To565_(DWORD color)
{
    return PanelRGB888To565_((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

static H3LoadedPcx16* LoadPanelPcx24_(const char* asset_name, int expected_width,
    int expected_height, H3LoadedPcx16*& cache, bool& load_failed)
{
    if (cache || load_failed)
        return cache;

    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_hModule, path, _countof(path));
    char* slash = strrchr(path, '\\');
    if (!slash) {
        load_failed = true;
        return nullptr;
    }
    const size_t remaining = _countof(path) - static_cast<size_t>(slash + 1 - path);
    strcpy_s(slash + 1, remaining, "img\\");
    strcat_s(path, asset_name);

    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) {
        WriteLog("[Panel] PCX 资源加载失败：%s", path);
        load_failed = true;
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size < 128) {
        fclose(file);
        load_failed = true;
        return nullptr;
    }

    BYTE* encoded = static_cast<BYTE*>(malloc(file_size));
    if (!encoded || fread(encoded, 1, file_size, file) != static_cast<size_t>(file_size)) {
        if (encoded) free(encoded);
        fclose(file);
        load_failed = true;
        return nullptr;
    }
    fclose(file);

    const int bits_per_plane = encoded[3];
    const int xmin = *reinterpret_cast<WORD*>(encoded + 4);
    const int ymin = *reinterpret_cast<WORD*>(encoded + 6);
    const int xmax = *reinterpret_cast<WORD*>(encoded + 8);
    const int ymax = *reinterpret_cast<WORD*>(encoded + 10);
    const int plane_count = encoded[65];
    const int bytes_per_line = *reinterpret_cast<WORD*>(encoded + 66);
    const int width = xmax - xmin + 1;
    const int height = ymax - ymin + 1;

    if (encoded[0] != 0x0A || encoded[2] != 1 || bits_per_plane != 8
        || plane_count != 3 || width != expected_width || height != expected_height
        || bytes_per_line < width)
    {
        WriteLog("[Panel] %s 格式不符 w=%d h=%d bpp=%d planes=%d bpl=%d。",
            asset_name, width, height, bits_per_plane, plane_count, bytes_per_line);
        free(encoded);
        load_failed = true;
        return nullptr;
    }

    const size_t raw_size = static_cast<size_t>(bytes_per_line) * plane_count * height;
    BYTE* raw = static_cast<BYTE*>(malloc(raw_size));
    if (!raw) {
        free(encoded);
        load_failed = true;
        return nullptr;
    }

    size_t source_pos = 128;
    size_t output_pos = 0;
    while (output_pos < raw_size && source_pos < static_cast<size_t>(file_size)) {
        const BYTE marker = encoded[source_pos++];
        if ((marker & 0xC0) == 0xC0) {
            const int count = marker & 0x3F;
            if (source_pos >= static_cast<size_t>(file_size)) break;
            const BYTE value = encoded[source_pos++];
            for (int i = 0; i < count && output_pos < raw_size; ++i)
                raw[output_pos++] = value;
        } else {
            raw[output_pos++] = marker;
        }
    }
    free(encoded);

    if (output_pos != raw_size) {
        WriteLog("[Panel] %s 解码不完整 decoded=%u expected=%u。",
            asset_name, static_cast<unsigned>(output_pos), static_cast<unsigned>(raw_size));
        free(raw);
        load_failed = true;
        return nullptr;
    }

    cache = H3LoadedPcx16::Create(width, height);
    if (!cache || !cache->buffer) {
        if (cache) cache->Destroy();
        cache = nullptr;
        free(raw);
        load_failed = true;
        return nullptr;
    }

    const bool output_32_bit = H3BitMode::Get() == 4;
    for (int y = 0; y < height; ++y) {
        const BYTE* planes = raw + static_cast<size_t>(y) * bytes_per_line * plane_count;
        const BYTE* red = planes;
        const BYTE* green = planes + bytes_per_line;
        const BYTE* blue = planes + bytes_per_line * 2;
        BYTE* row = cache->buffer + y * cache->scanlineSize;
        if (output_32_bit) {
            DWORD* pixels = reinterpret_cast<DWORD*>(row);
            for (int x = 0; x < width; ++x)
                pixels[x] = 0xFF000000u | (red[x] << 16) | (green[x] << 8) | blue[x];
        } else {
            WORD* pixels = reinterpret_cast<WORD*>(row);
            for (int x = 0; x < width; ++x)
                pixels[x] = PanelRGB888To565_(red[x], green[x], blue[x]);
        }
    }
    free(raw);
    return cache;
}

static H3LoadedPcx16* LoadPanelBackground_()
{
    return LoadPanelPcx24_("HA_bg.pcx", PANEL_W, PANEL_H,
        s_panel_background, s_panel_background_load_failed);
}

static H3LoadedPcx16* LoadPanelCell_()
{
    return LoadPanelPcx24_("HA_cell.pcx", CELL_W, CELL_H,
        s_panel_cell, s_panel_cell_load_failed);
}

static H3LoadedPcx16* LoadPanelGridFrame_()
{
    return LoadPanelPcx24_("HA_grid_frame.pcx", GRID_FRAME_W, GRID_FRAME_H,
        s_panel_grid_frame, s_panel_grid_frame_load_failed);
}

static bool CopyPanelBackground_(H3LoadedPcx16* destination)
{
    H3LoadedPcx16* background = LoadPanelBackground_();
    if (!background || !destination || !background->buffer || !destination->buffer)
        return false;

    const int row_bytes = background->scanlineSize < destination->scanlineSize
        ? background->scanlineSize : destination->scanlineSize;
    for (int y = 0; y < PANEL_H; ++y) {
        memcpy(destination->buffer + y * destination->scanlineSize,
            background->buffer + y * background->scanlineSize, row_bytes);
    }
    return true;
}

static void DrawPanelCell_(H3LoadedPcx16* destination, int dst_x, int dst_y)
{
    H3LoadedPcx16* cell = LoadPanelCell_();
    if (!cell || !cell->buffer || !destination || !destination->buffer)
        return;

    // HA_cell.pcx 现在是统一金色 2px 边框 + 精确青色键内部，
    // 与其它边框资源一致，直接用精确青色键判定即可
    // （32-bit 0x0000FFFF / 16-bit 0x7FDF），不再需要宽松的 red-dominant 判定。
    const bool mode_32_bit = H3BitMode::Get() == 4;
    for (int y = 0; y < CELL_H; ++y) {
        BYTE* dst_row = destination->buffer + (dst_y + y) * destination->scanlineSize;
        const BYTE* src_row = cell->buffer + y * cell->scanlineSize;
        if (mode_32_bit) {
            DWORD* dst = reinterpret_cast<DWORD*>(dst_row) + dst_x;
            const DWORD* src = reinterpret_cast<const DWORD*>(src_row);
            for (int x = 0; x < CELL_W; ++x) {
                const DWORD color = src[x];
                if ((color & 0x00FFFFFFu) == 0x0000FFFFu) continue; // 精确青色键
                dst[x] = color;
            }
        } else {
            WORD* dst = reinterpret_cast<WORD*>(dst_row) + dst_x;
            const WORD* src = reinterpret_cast<const WORD*>(src_row);
            for (int x = 0; x < CELL_W; ++x) {
                const WORD color = src[x];
                if (color == 0x7FDF) continue; // 精确青色键
                dst[x] = color;
            }
        }
    }
}

static void DrawPanelTriangle_(H3LoadedPcx16* destination, int center_x, int top,
    bool points_down, int red, int green, int blue)
{
    for (int row = 0; row < 5; ++row) {
        const int half_width = points_down ? 4 - row : row;
        const int y = top + row;
        Fill(destination, center_x - half_width, y,
            half_width * 2 + 1, 1, red, green, blue);
    }
}

static void DrawPanelScrollbar_(H3LoadedPcx16* destination)
{
    const int max_row = PanelMaxScrollRow_();
    if (!destination) return;

    const int button_size = PanelScrollButtonSize_();
    const int thumb_y = PanelScrollThumbY_();

    // Matches the supplied mockup: black recessed track, gold outline,
    // gold arrow buttons and a brown/gold thumb. Drawing directly to pcx16
    // avoids the 8-bit palette corruption seen with sliderV.pcx in HD 32-bit.
    Fill(destination, SCROLL_X, SCROLL_Y, SCROLL_W, SCROLL_H, 8, 6, 4);
    destination->DrawFrame(SCROLL_X, SCROLL_Y, SCROLL_W, SCROLL_H,
        (BYTE)112, (BYTE)78, (BYTE)30);
    destination->DrawFrame(SCROLL_X + 1, SCROLL_Y + 1, SCROLL_W - 2, SCROLL_H - 2,
        (BYTE)35, (BYTE)25, (BYTE)14);

    const bool up_pressed = s_p.scroll_button_pressed == 1;
    const bool down_pressed = s_p.scroll_button_pressed == 2;
    Fill(destination, SCROLL_X + 2, SCROLL_Y + 2, SCROLL_W - 4, button_size - 3,
        up_pressed ? 104 : 54, up_pressed ? 70 : 38, up_pressed ? 28 : 20);
    Fill(destination, SCROLL_X + 2, SCROLL_Y + SCROLL_H - button_size + 1,
        SCROLL_W - 4, button_size - 3,
        down_pressed ? 104 : 54, down_pressed ? 70 : 38, down_pressed ? 28 : 20);
    destination->DrawFrame(SCROLL_X + 1, SCROLL_Y + 1, SCROLL_W - 2, button_size - 1,
        (BYTE)184, (BYTE)139, (BYTE)62);
    destination->DrawFrame(SCROLL_X + 1, SCROLL_Y + SCROLL_H - button_size,
        SCROLL_W - 2, button_size - 1, (BYTE)184, (BYTE)139, (BYTE)62);
    DrawPanelTriangle_(destination, SCROLL_X + SCROLL_W / 2,
        SCROLL_Y + 5 + (up_pressed ? 1 : 0), false, 235, 205, 116);
    DrawPanelTriangle_(destination, SCROLL_X + SCROLL_W / 2,
        SCROLL_Y + SCROLL_H - button_size + 5 + (down_pressed ? 1 : 0),
        true, 235, 205, 116);

    Fill(destination, SCROLL_X + 2, thumb_y, SCROLL_W - 4, button_size,
        max_row > 0 ? 126 : 74, max_row > 0 ? 86 : 52, max_row > 0 ? 36 : 24);
    destination->DrawFrame(SCROLL_X + 1, thumb_y, SCROLL_W - 2, button_size,
        (BYTE)(max_row > 0 ? 218 : 118),
        (BYTE)(max_row > 0 ? 174 : 83),
        (BYTE)(max_row > 0 ? 82 : 38));
    Fill(destination, SCROLL_X + 4, thumb_y + button_size / 2 - 1,
        SCROLL_W - 8, 1, 235, 205, 116);
}

static void DrawTransparentPcx_(H3LoadedPcx16* source,
    H3LoadedPcx16* destination, int dst_x, int dst_y)
{
    if (!source || !source->buffer || !destination || !destination->buffer) return;
    const bool mode_32_bit = H3BitMode::Get() == 4;
    // 格子缓冲区是我们自己用精确清屏色（16-bit 0x7FDF / 32-bit 0xFF00FFFF）
    // 清空的，所以合成时只跳过这个精确值，不能用金框那套宽松的
    // green>red||blue>red 判定，否则图标/文字里的冷色像素会被误抠。
    for (int y = 0; y < source->height; ++y) {
        const BYTE* src_row = source->buffer + y * source->scanlineSize;
        BYTE* dst_row = destination->buffer + (dst_y + y) * destination->scanlineSize;
        for (int x = 0; x < source->width; ++x) {
            if (mode_32_bit) {
                const DWORD color = reinterpret_cast<const DWORD*>(src_row)[x];
                if ((color & 0x00FFFFFFu) == 0x0000FFFFu) continue; // 精确青色键
                reinterpret_cast<DWORD*>(dst_row)[dst_x + x] = color;
            } else {
                const WORD color = reinterpret_cast<const WORD*>(src_row)[x];
                if (color == 0x7FDF) continue; // 精确青色键
                reinterpret_cast<WORD*>(dst_row)[dst_x + x] = color;
            }
        }
    }
}

// 不透明区块拷贝：把 source 的一个矩形原样拷到 destination（不做任何键色跳过）。
// 用于图标区——TwCrPort 头像自带实心背景，冷色像素若走 DrawTransparentPcx_
// 的 green>red||blue>red 判定会被误当透明抠掉，透出面板底图。这里直接不透明贴。
static void BlitOpaqueRegion_(H3LoadedPcx16* source, H3LoadedPcx16* destination,
    int src_x, int src_y, int w, int h, int dst_x, int dst_y)
{
    if (!source || !source->buffer || !destination || !destination->buffer) return;
    const bool mode_32_bit = H3BitMode::Get() == 4;
    for (int y = 0; y < h; ++y) {
        const int sy = src_y + y;
        const int dy = dst_y + y;
        if (sy < 0 || sy >= source->height) continue;
        if (dy < 0 || dy >= destination->height) continue;
        const BYTE* src_row = source->buffer + sy * source->scanlineSize;
        BYTE* dst_row = destination->buffer + dy * destination->scanlineSize;
        for (int x = 0; x < w; ++x) {
            const int sx = src_x + x;
            const int dx = dst_x + x;
            if (sx < 0 || sx >= source->width) continue;
            if (dx < 0 || dx >= destination->width) continue;
            if (mode_32_bit) {
                reinterpret_cast<DWORD*>(dst_row)[dx] =
                    reinterpret_cast<const DWORD*>(src_row)[sx];
            } else {
                reinterpret_cast<WORD*>(dst_row)[dx] =
                    reinterpret_cast<const WORD*>(src_row)[sx];
            }
        }
    }
}

static void EnsurePanelButtonPcxResources_()
{
    LoadPanelPcx24_("HA_button_frame.pcx", BTN_FRAME_W, BTN_FRAME_H,
        s_panel_button_frame, s_panel_button_frame_load_failed);
    LoadPanelPcx24_("HA_ok_normal.pcx", BTN_W, BTN_H,
        s_panel_ok_normal, s_panel_ok_normal_load_failed);
    LoadPanelPcx24_("HA_ok_pressed.pcx", BTN_W, BTN_H,
        s_panel_ok_pressed, s_panel_ok_pressed_load_failed);
    LoadPanelPcx24_("HA_cancel_normal.pcx", BTN_W, BTN_H,
        s_panel_cancel_normal, s_panel_cancel_normal_load_failed);
    LoadPanelPcx24_("HA_cancel_pressed.pcx", BTN_W, BTN_H,
        s_panel_cancel_pressed, s_panel_cancel_pressed_load_failed);
}

static void DrawProfileButtons_(H3LoadedPcx16* destination)
{
    H3Font* font = GetSmallFont();
    for (int i = 0; i < PROFILE_COUNT; ++i) {
        const RECT rc = ProfileButtonRect_(i);
        const bool selected = i == s_p.selected_profile;
        const bool pressed = i == s_p.pressed_profile;
        if (selected) {
            Fill(destination, rc.left, rc.top, PROFILE_BTN_W, PROFILE_BTN_H,
                pressed ? 122 : 154, pressed ? 86 : 112, pressed ? 30 : 38);
            destination->DrawFrame(rc.left - 1, rc.top - 1,
                PROFILE_BTN_W + 2, PROFILE_BTN_H + 2,
                (BYTE)255, (BYTE)218, (BYTE)108);
            destination->DrawFrame(rc.left, rc.top,
                PROFILE_BTN_W, PROFILE_BTN_H,
                (BYTE)224, (BYTE)176, (BYTE)62);
        } else {
            Fill(destination, rc.left, rc.top, PROFILE_BTN_W, PROFILE_BTN_H,
                pressed ? 68 : 48, pressed ? 48 : 36, pressed ? 22 : 18);
            destination->DrawFrame(rc.left, rc.top,
                PROFILE_BTN_W, PROFILE_BTN_H,
                (BYTE)142, (BYTE)108, (BYTE)54);
        }
        char text[16];
        _snprintf(text, sizeof(text), "方案 %d", i + 1);
        DrawTxt(destination, font, text,
            rc.left, rc.top, PROFILE_BTN_W, PROFILE_BTN_H,
            selected ? (INT32)eTextColor::WHITE : (INT32)eTextColor::GOLD,
            eTextAlignment::MIDDLE_CENTER);
    }
}

static void DrawPanelButtons_(H3LoadedPcx16* destination)
{
    const H3POINT cursor = H3POINT::GetCursorPosition();
    const int px = cursor.x - s_p.x;
    const int py = cursor.y - s_p.y;
    const bool ok_pressed = s_p.pressed_button == 1
        && PointInRect_(px, py, OK_X, BTN_Y, BTN_W, BTN_H);
    const bool cancel_pressed = s_p.pressed_button == 2
        && PointInRect_(px, py, CANCEL_X, BTN_Y, BTN_W, BTN_H);

    H3LoadedPcx16* ok = ok_pressed ? s_panel_ok_pressed : s_panel_ok_normal;
    H3LoadedPcx16* cancel = cancel_pressed
        ? s_panel_cancel_pressed : s_panel_cancel_normal;

    if (s_panel_button_frame) {
        DrawTransparentPcx_(s_panel_button_frame, destination, OK_X - 1, BTN_Y - 1);
        DrawTransparentPcx_(s_panel_button_frame, destination, CANCEL_X - 1, BTN_Y - 1);
    } else {
        destination->DrawFrame(OK_X - 1, BTN_Y - 1, BTN_FRAME_W, BTN_FRAME_H,
            (BYTE)168, (BYTE)141, (BYTE)68);
        destination->DrawFrame(CANCEL_X - 1, BTN_Y - 1, BTN_FRAME_W, BTN_FRAME_H,
            (BYTE)168, (BYTE)141, (BYTE)68);
    }

    if (ok) DrawTransparentPcx_(ok, destination, OK_X, BTN_Y);
    else {
        Fill(destination, OK_X, BTN_Y, BTN_W, BTN_H, 74, 52, 24);
        destination->DrawFrame(OK_X, BTN_Y, BTN_W, BTN_H,
            (BYTE)210, (BYTE)170, (BYTE)72);
    }
    if (cancel) DrawTransparentPcx_(cancel, destination, CANCEL_X, BTN_Y);
    else {
        Fill(destination, CANCEL_X, BTN_Y, BTN_W, BTN_H, 74, 52, 24);
        destination->DrawFrame(CANCEL_X, BTN_Y, BTN_W, BTN_H,
            (BYTE)210, (BYTE)170, (BYTE)72);
    }
}

static int GetPanelBackBufferBpp_()
{
    if (!o_DDSurfaceBackBuffer)
        return H3BitMode::Get() == 4 ? 32 : 16;

    DDPIXELFORMAT format = {};
    format.dwSize = sizeof(format);
    if (SUCCEEDED(o_DDSurfaceBackBuffer->GetPixelFormat(&format))
        && (format.dwRGBBitCount == 16 || format.dwRGBBitCount == 32))
    {
        return static_cast<int>(format.dwRGBBitCount);
    }
    return H3BitMode::Get() == 4 ? 32 : 16;
}

static bool DrawPanelCompositeToBackBuffer_(H3LoadedPcx16* source, int dst_x, int dst_y)
{
    if (!source || !source->buffer || !o_DDSurfaceBackBuffer)
        return false;

    __try {
        DDSURFACEDESC desc = {};
        desc.dwSize = sizeof(desc);
        const HRESULT lock_result = o_DDSurfaceBackBuffer->Lock(
            nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr);
        if (FAILED(lock_result) || !desc.lpSurface)
            return false;

        const int dst_bpp = GetPanelBackBufferBpp_();
        int dst_w = static_cast<int>(desc.dwWidth);
        int dst_h = static_cast<int>(desc.dwHeight);
        if (dst_w <= 0 && o_WndMgr && o_WndMgr->screenPcx16)
            dst_w = o_WndMgr->screenPcx16->width;
        if (dst_h <= 0 && o_WndMgr && o_WndMgr->screenPcx16)
            dst_h = o_WndMgr->screenPcx16->height;

        int src_x = 0;
        int src_y = 0;
        int copy_w = source->width;
        int copy_h = source->height;
        if (dst_x < 0) { src_x = -dst_x; copy_w += dst_x; dst_x = 0; }
        if (dst_y < 0) { src_y = -dst_y; copy_h += dst_y; dst_y = 0; }
        if (dst_x + copy_w > dst_w) copy_w = dst_w - dst_x;
        if (dst_y + copy_h > dst_h) copy_h = dst_h - dst_y;

        const bool source_is_32_bit = H3BitMode::Get() == 4;
        if (copy_w > 0 && copy_h > 0) {
            for (int y = 0; y < copy_h; ++y) {
                BYTE* src_row = source->buffer + (src_y + y) * source->scanlineSize;
                BYTE* dst_row = static_cast<BYTE*>(desc.lpSurface)
                    + (dst_y + y) * desc.lPitch;

                if (dst_bpp == 32) {
                    BYTE* dst = dst_row + dst_x * 4;
                    if (source_is_32_bit) {
                        const DWORD* src = reinterpret_cast<const DWORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            const DWORD color = src[x];
                            dst[x * 4 + 0] = static_cast<BYTE>(color);
                            dst[x * 4 + 1] = static_cast<BYTE>(color >> 8);
                            dst[x * 4 + 2] = static_cast<BYTE>(color >> 16);
                        }
                    } else {
                        const WORD* src = reinterpret_cast<const WORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            const DWORD color = PanelRGB565To8888_(src[x]);
                            dst[x * 4 + 0] = static_cast<BYTE>(color);
                            dst[x * 4 + 1] = static_cast<BYTE>(color >> 8);
                            dst[x * 4 + 2] = static_cast<BYTE>(color >> 16);
                        }
                    }
                } else {
                    WORD* dst = reinterpret_cast<WORD*>(dst_row) + dst_x;
                    if (source_is_32_bit) {
                        const DWORD* src = reinterpret_cast<const DWORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x)
                            dst[x] = PanelRGB8888To565_(src[x]);
                    } else {
                        const WORD* src = reinterpret_cast<const WORD*>(src_row) + src_x;
                        memcpy(dst, src, copy_w * sizeof(WORD));
                    }
                }
            }
        }

        o_DDSurfaceBackBuffer->Unlock(nullptr);
        return copy_w > 0 && copy_h > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static H3LoadedPcx16* EnsurePanelComposite_()
{
    if (s_panel_composite
        && s_panel_composite->width == PANEL_W
        && s_panel_composite->height == PANEL_H)
    {
        return s_panel_composite;
    }

    if (s_panel_composite)
        s_panel_composite->Destroy();
    s_panel_composite = H3LoadedPcx16::Create(PANEL_W, PANEL_H);
    return s_panel_composite;
}

static void ReleasePanelComposite_()
{
    if (s_panel_composite) {
        s_panel_composite->Destroy();
        s_panel_composite = nullptr;
    }
    if (s_panel_background) {
        s_panel_background->Destroy();
        s_panel_background = nullptr;
    }
    if (s_panel_cell) {
        s_panel_cell->Destroy();
        s_panel_cell = nullptr;
    }
    if (s_panel_grid_frame) {
        s_panel_grid_frame->Destroy();
        s_panel_grid_frame = nullptr;
    }
    H3LoadedPcx16** button_resources[] = {
        &s_panel_ok_normal, &s_panel_ok_pressed,
        &s_panel_cancel_normal, &s_panel_cancel_pressed,
        &s_panel_button_frame
    };
    for (int i = 0; i < 5; ++i) {
        if (*button_resources[i]) {
            (*button_resources[i])->Destroy();
            *button_resources[i] = nullptr;
        }
    }
    s_panel_background_load_failed = false;
    s_panel_cell_load_failed = false;
    s_panel_grid_frame_load_failed = false;
    s_panel_ok_normal_load_failed = false;
    s_panel_ok_pressed_load_failed = false;
    s_panel_cancel_normal_load_failed = false;
    s_panel_cancel_pressed_load_failed = false;
    s_panel_button_frame_load_failed = false;
}

// ========================================================================
// 第三部分：战斗状态判断
// ========================================================================

static H3CombatManager* GetCombatMgr()
{
    return H3CombatManager::Get();
}

// 定位 HD_SOD.dll 模块基址，只调一次
static void InitHdHover_()
{
    if (s_hd_sod_module) return;
    s_hd_sod_module = GetModuleHandleA("HD_SOD.dll");
    if (s_hd_sod_module) {
        WriteLog("[Panel] HD_SOD.dll=%p", (void*)s_hd_sod_module);
    }
}

// 面板打开时调：patch HD 消息钩子为 ret 12，屏蔽 hover 高亮更新
static void BlockHdHover_()
{
    InitHdHover_();
    if (!s_hd_sod_module) return;

    // 直接 patch HD 的战斗消息钩子 FUN_010d9ce0 (RVA 0xD9CE0) 为 ret 12
    // 这是 HD 挂在原版战斗消息处理上的钩子，负责 hover 高亮更新。
    // ret 12 (C2 0C 00) 直接跳过整个钩子，高亮不会更新。
    // 面板关闭时 undo，恢复正常功能。
    if (_PI) {
        if (!s_hd_msgproc_patch) {
            BYTE* target = (BYTE*)s_hd_sod_module + 0xD9CE0;
            char ret12[] = "C2 0C 00";
            s_hd_msgproc_patch = _PI->CreateHexPatch(
                reinterpret_cast<UINT_PTR>(target), ret12);
        }
        if (s_hd_msgproc_patch && !s_hd_msgproc_patch->IsApplied())
            s_hd_msgproc_patch->Apply();
    }
}

// 面板关闭时调：还原
static void RestoreHdHover_()
{
    if (s_hd_msgproc_patch && s_hd_msgproc_patch->IsApplied())
        s_hd_msgproc_patch->Undo();
}

// 面板打开时每帧强制清掉战场悬停状态，让 SP 行动顺序条的高亮跟随失效。
// SP 插件的队列高亮每帧读 creatureAtMousePos(0x132D0) 和 mouseCoord(0x132D4)
// 重画。游戏鼠标离场时也是把这两个值设成 -1，所以这里直接置 -1，
// SP 下一帧重算就认为鼠标不在任何单位上，自动清高亮。
static void ClearBattleHoverState_()
{
    H3CombatManager* mgr = GetCombatMgr();
    if (!mgr) return;
    BYTE* base = reinterpret_cast<BYTE*>(mgr);
    __try {
        *reinterpret_cast<INT32*>(base + 0x132D0) = -1; // creatureAtMousePos
        *reinterpret_cast<INT32*>(base + 0x132D4) = -1; // mouseCoord
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static bool BlockBattleHover_()
{
    if (!_PI) return false;
    if (!s_hover_patch_primary)
        s_hover_patch_primary = _PI->CreateHexPatch(0x473E32,
            const_cast<char*>("83 C4 04 33 C0"));
    if (!s_hover_patch_secondary)
        s_hover_patch_secondary = _PI->CreateHexPatch(0x473F55,
            const_cast<char*>("83 C4 04 33 C0"));
    if (!s_hover_patch_primary || !s_hover_patch_secondary) {
        WriteLog("[Panel] 创建战场悬停屏蔽补丁失败。");
        return false;
    }

    const int first = s_hover_patch_primary->IsApplied()
        ? 0 : s_hover_patch_primary->Apply();
    const int second = s_hover_patch_secondary->IsApplied()
        ? 0 : s_hover_patch_secondary->Apply();
    if (first < 0 || second < 0) {
        if (s_hover_patch_primary->IsApplied()) s_hover_patch_primary->Undo();
        if (s_hover_patch_secondary->IsApplied()) s_hover_patch_secondary->Undo();
        WriteLog("[Panel] 应用战场悬停屏蔽补丁失败 first=%d second=%d。",
            first, second);
        return false;
    }
    BlockHdHover_();
    WriteLog("[Panel] 战场悬停处理已屏蔽。");
    return true;
}

static void RestoreBattleHover_()
{
    RestoreHdHover_();
    if (s_hover_patch_secondary && s_hover_patch_secondary->IsApplied())
        s_hover_patch_secondary->Undo();
    if (s_hover_patch_primary && s_hover_patch_primary->IsApplied())
        s_hover_patch_primary->Undo();
    WriteLog("[Panel] 战场悬停处理已恢复。");
}

static H3BaseDlg* FindDialogByVtable_(UINT target_vtable)
{
    if (!o_WndMgr) return nullptr;

    H3BaseDlg* dlg = o_WndMgr->firstDlg;
    for (int i = 0; dlg && i < 16; ++i) {
        if (*(UINT*)dlg == target_vtable)
            return dlg;

        // H3BaseDlg::nextDialog is protected and is stored at +0x08.
        dlg = *reinterpret_cast<H3BaseDlg**>(reinterpret_cast<BYTE*>(dlg) + 0x08);
    }
    return nullptr;
}

static bool InstallBattleInputBlocker_()
{
    H3BaseDlg* battle_ui = FindDialogByVtable_(s_combat_dialog_vtable);
    if (!battle_ui) {
        WriteLog("[Panel] 未找到 BattleUI，无法安装输入屏障。");
        return false;
    }

    if (s_input_blocker.item && s_input_blocker.battle_ui == battle_ui) {
        *reinterpret_cast<void***>(s_input_blocker.item) = s_input_blocker.local_vtable;
        s_input_blocker.item->ShowActivate();
        WriteLog("[Panel] 已重新激活 BattleUI 输入屏障 item=%p。", s_input_blocker.item);
        return true;
    }

    s_input_blocker = {};
    H3DlgTransparentItem* item = H3DlgTransparentItem::Create(
        0, 0, H3GameWidth::Get(), H3GameHeight::Get(), 0x7FFE);
    if (!item) {
        WriteLog("[Panel] 创建 BattleUI 输入屏障失败。");
        return false;
    }

    void** original_vtable = *reinterpret_cast<void***>(item);
    memcpy(s_input_blocker.local_vtable, original_vtable,
        sizeof(s_input_blocker.local_vtable));
    s_input_blocker.local_vtable[2] = reinterpret_cast<void*>(&BlockBattleItemMessage_);
    *reinterpret_cast<void***>(item) = s_input_blocker.local_vtable;

    if (!battle_ui->AddItem(item, TRUE)) {
        *reinterpret_cast<void***>(item) = original_vtable;
        typedef H3DlgItem* (__thiscall *DestroyItemProc)(H3DlgItem*, BOOL8);
        reinterpret_cast<DestroyItemProc>(original_vtable[0])(item, TRUE);
        WriteLog("[Panel] BattleUI 拒绝加入输入屏障控件。");
        return false;
    }

    s_input_blocker.battle_ui = battle_ui;
    s_input_blocker.item = item;
    s_input_blocker.original_vtable = original_vtable;
    WriteLog("[Panel] BattleUI 输入屏障已安装。 battle=%p item=%p prev=%p next=%p。",
        battle_ui, item, item->GetPreviousItem(), item->GetNextItem());
    return true;
}

static void RemoveBattleInputBlocker_()
{
    if (!s_input_blocker.item) return;
    *reinterpret_cast<void***>(s_input_blocker.item) = s_input_blocker.original_vtable;
    s_input_blocker.item->HideDeactivate();
    WriteLog("[Panel] BattleUI 输入屏障已停用。 item=%p。", s_input_blocker.item);
}

// 实验：面板打开时把 H3 模态深度计数器（0x69FEA4）顶成 1，看 HD.dll 的
// 行动顺序条会不会因此停止响应鼠标 hover。真模态对话框会把计数器顶到 2 以上，
// 所以挂起判定用 >= 2 区分。面板关闭时归 0。
static void ForcePanelModalDepth_(bool on)
{
    __try {
        INT32* depth = reinterpret_cast<INT32*>(0x69FEA4);
        if (on) {
            if (*depth < 1) *depth = 1;
        } else {
            if (*depth > 0) *depth = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void UpdatePanelModalSuspension_()
{
    if (!s_p.active) return;
    const INT32 modal_depth = *reinterpret_cast<INT32*>(0x69FEA4);
    // The same counter also rises while the game is inactive. Only an in-game
    // modal dialog should hide the panel; clicking outside must leave it intact.
    // 我们自己每帧把计数器顶到 1，所以真模态对话框的阈值是 >= 2。
    const bool system_modal_active = modal_depth >= 2 && IsGameWindowForeground_();
    if (system_modal_active == s_panel_modal_suspended) return;

    s_panel_modal_suspended = system_modal_active;
    if (system_modal_active) {
        RemoveBattleInputBlocker_();
        RestoreBattleHover_();
        WriteLog("[Panel] 检测到系统模态对话框，暂停面板绘制和输入。");
    } else {
        if (!BlockBattleHover_() || !InstallBattleInputBlocker_()) {
            WriteLog("[Panel] 系统模态对话框关闭后恢复面板失败，关闭设置面板。");
            CloseSettingsPanel();
            return;
        }
        ForcePanelDefaultCursor_();
        DrawPanelToBuffer_();
        WriteLog("[Panel] 系统模态对话框已关闭，恢复设置面板。");
    }
}

static INT32 GetBattleItemUnderCursor_(H3BaseDlg* battle_ui)
{
    if (!battle_ui) return -1;

    const H3POINT cursor = H3POINT::GetCursorPosition();
    H3CombatDlg* combat_dlg = reinterpret_cast<H3CombatDlg*>(battle_ui);

    // Bottom-panel items overlap: the 0x7D0 background covers the buttons and
    // appears earlier in the vector. Find the known auto-fight button first.
    if (combat_dlg->bottomPanel) {
        H3Vector<H3DlgItem*>& items = combat_dlg->bottomPanel->GetItems();
        for (H3DlgItem** it = items.begin(); it != items.end(); ++it) {
            H3DlgItem* item = *it;
            if (!item || item->GetID() != s_autofight_button_id) continue;

            const INT32 x = item->GetAbsoluteX();
            const INT32 y = item->GetAbsoluteY();
            if (cursor.x >= x && cursor.x < x + item->GetWidth()
                && cursor.y >= y && cursor.y < y + item->GetHeight())
            {
                return s_autofight_button_id;
            }
        }

        // Diagnostic fallback for other overlapping controls under the cursor.
        INT32 fallback_id = -1;
        for (H3DlgItem** it = items.begin(); it != items.end(); ++it) {
            H3DlgItem* item = *it;
            if (!item || !item->IsVisible()) continue;

            const INT32 x = item->GetAbsoluteX();
            const INT32 y = item->GetAbsoluteY();
            if (cursor.x >= x && cursor.x < x + item->GetWidth()
                && cursor.y >= y && cursor.y < y + item->GetHeight())
            {
                fallback_id = item->GetID();
            }
        }
        if (fallback_id != -1)
            return fallback_id;
    }

    H3Msg msg = {};
    msg.position = cursor;
    H3DlgItem* item = battle_ui->ItemAtPosition(msg);
    return item ? item->GetID() : -1;
}

// ========================================================================
// 第四部分：检测"自动战斗"对话框关闭
// ========================================================================
// 状态机：firstDlg 是底层战斗界面，lastDlg 才是当前最上层对话框。
// 流程：BattleUI 存在 + lastDlg=自动战斗说明框 → lastDlg 回到 BattleUI → 弹窗。
//
static void CheckAutoFightDialogClosed()
{
    if (!o_WndMgr) return;

    H3BaseDlg* first = o_WndMgr->firstDlg;
    H3BaseDlg* last = o_WndMgr->lastDlg;
    UINT first_vtable = first ? *(UINT*)first : 0;
    UINT last_vtable = last ? *(UINT*)last : 0;
    INT32 last_w = last ? last->GetWidth() : 0;
    INT32 last_h = last ? last->GetHeight() : 0;
    H3BaseDlg* combat_dlg = FindDialogByVtable_(s_combat_dialog_vtable);
    INT32 cursor_item_id = GetBattleItemUnderCursor_(combat_dlg);

    const bool battle_ui_exists = combat_dlg != nullptr;
    const bool right_button_down = IsGameMouseInputActive_()
        && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (battle_ui_exists && right_button_down
        && cursor_item_id == s_autofight_button_id)
    {
        if (!s_autofight_right_press_armed)
            WriteLog("[AutoFight] 右键按下命中自动战斗按钮 id=0x%X。", s_autofight_button_id);
        s_autofight_right_press_armed = true;
    }

    const bool explanation_is_top = battle_ui_exists
        && s_autofight_right_press_armed
        && last_vtable == s_right_click_dialog_vtable
        && last_w == 448 && last_h == 128;

    static H3BaseDlg* s_logged_unarmed_explanation = nullptr;
    if (battle_ui_exists && last_vtable == s_right_click_dialog_vtable
        && last_w == 448 && last_h == 128
        && !s_autofight_right_press_armed
        && last != s_logged_unarmed_explanation)
    {
        s_logged_unarmed_explanation = last;
    } else if (last_vtable != s_right_click_dialog_vtable) {
        s_logged_unarmed_explanation = nullptr;
    }

    if (battle_ui_exists) {
    } else if (++s_battle_ui_missing_frames >= 3) {
        s_saw_explanation_dlg_in_battle = false;
        s_autofight_right_press_armed = false;
        s_panel_popup_done = false;
    }

    if (explanation_is_top && !s_panel_popup_done) {
        if (!s_saw_explanation_dlg_in_battle) {
            s_saw_explanation_dlg_in_battle = true;
            WriteLog("[AutoFight] 检测到右键按住时的自动战斗说明框，w=%d h=%d。", last_w, last_h);
        }
        return;
    }

    if (s_saw_explanation_dlg_in_battle && battle_ui_exists
        && last_vtable != s_right_click_dialog_vtable && !s_panel_popup_done)
    {
        s_saw_explanation_dlg_in_battle = false;
        s_autofight_right_press_armed = false;
        WriteLog("[AutoFight] 说明框已关闭且 BattleUI 仍在，打开设置面板。");
        OpenSettingsPanel_();
        s_panel_popup_done = true;
    } else if (!right_button_down && !s_saw_explanation_dlg_in_battle) {
        // 在目标按钮上按下但没有出现对应说明框时，不把状态带到下一次右键。
        s_autofight_right_press_armed = false;
    }
}

// ========================================================================
// 第五部分：LoHook
// ========================================================================

INT __stdcall Hook_BltComplete(LoHook* h, HookContext* c)
{
    (void)h; (void)c;
    static int s_frame = 0;
    s_frame++;
    CheckAutoFightDialogClosed();
    if (s_p.active) {
        ForcePanelModalDepth_(true);
        UpdatePanelModalSuspension_();
        if (s_p.active && !s_panel_modal_suspended) {
            HandlePanelInput_();
            DrawPanelToBuffer_();
            ForcePanelDefaultCursor_();
        }
    }
    return EXEC_DEFAULT;
}

// 战斗消息处理入口（FUN_004746b0 @ 0x4746B0）。this=H3CombatManager 在 ECX，
// 消息指针 msg 在栈上 [esp+4]。msg[0]=类型（4=鼠标移动），msg[4]=x，msg[5]=y。
// 1) 面板打开时：把鼠标移动坐标改成离屏，清掉 hover 高亮。
// 2) 面板关闭时：若当前活动单位应由 H3Auto 主动执行，则在此提交 action 字段，
//    让原版主循环自然进入 FUN_004786b0 执行动画/伤害/回合推进。
extern bool TryAutoExecuteActiveStack();
INT __stdcall Hook_BattleMsgProc(LoHook* h, HookContext* c)
{
    (void)h;
    if (s_p.active && !s_panel_modal_suspended) {
        __try {
            int* msg = *reinterpret_cast<int**>(c->esp + 4);
            if (msg && msg[0] == 4) {   // 鼠标移动
                msg[4] = -1000;         // x 离屏
                msg[5] = -1000;         // y 离屏
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return EXEC_DEFAULT;
    }

    // 面板未打开：尝试主动提交当前单位动作（防御/远程等）。
    // 只写 battle->action，不跳过原函数；原函数看到 action!=0 会走执行路径。
    __try {
        TryAutoExecuteActiveStack();
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return EXEC_DEFAULT;
}

// ========================================================================
// 第六部分：绘制
// ========================================================================

static void DrawPanelToBuffer_()
{
    if (!s_p.active) return;
    H3LoadedPcx16* scr = EnsurePanelComposite_();
    if (!scr || !scr->buffer) return;

    const int px = 0;
    const int py = 0;

    if (!CopyPanelBackground_(scr)) {
        Fill(scr, px, py, PANEL_W, PANEL_H, 70, 42, 22);
        scr->DrawFrame(px, py, PANEL_W, PANEL_H, (BYTE)232, (BYTE)212, (BYTE)120);
    }
    DrawTxt(scr, GetPanelFont(), g_panel_title[0] ? g_panel_title : "部队自动行动设置",
        px + 20, py + 14, PANEL_W - 40, 36,
        COL_TITLE_TEXT, eTextAlignment::MIDDLE_CENTER);
    DrawProfileButtons_(scr);

    // 下拉悬停高亮由 WH_MOUSE 钩子即时更新到 s_p.hover_cell/hover_idx，
    // 绘制时直接使用，不再依赖低帧率的游戏坐标。
    H3Font* fntS = GetSmallFont();
    const int first_item = s_p.scroll_row * COLS;
    int max_redraw_bottom = 0; // 记录最下方的重绘边界

    // 第一趟：画所有格子本体
    for (int i = 0; i < CELL_COUNT; ++i) {
        const int item_index = first_item + i;
        if (item_index >= s_p.count) break;
        CellControl* ctrl = &s_p.cells[i];
        if (ctrl->dirty || !ctrl->buffer)
            CellControl_DrawCollapsed(ctrl);
        if (ctrl->buffer && ctrl->buffer->buffer) {
            RECT cRc = CellRect(i);
            DrawPanelCell_(scr, cRc.left, cRc.top);
            DrawTransparentPcx_(ctrl->buffer, scr, cRc.left, cRc.top);
        }
    }

    DrawPanelScrollbar_(scr);

    // 第二趟：单独画展开的下拉项，确保覆盖在所有格子和滚动条之上（层级最高）
    for (int i = 0; i < CELL_COUNT; ++i) {
        const int item_index = first_item + i;
        if (item_index >= s_p.count) break;
        CellControl* ctrl = &s_p.cells[i];
        if (!ctrl->buffer || !ctrl->buffer->buffer) continue;
        if (ctrl->expanded == CEX_NONE) continue;
        RECT cRc = CellRect(i);
        const int h_idx = (s_p.hover_cell == i) ? s_p.hover_idx : -1;
        CellControl_DrawExpandedTo(ctrl, scr, cRc.left, cRc.top, h_idx);
        const int n = CellControl_GetExpandedItemCount(ctrl);
        int bottom = cRc.top + CC_ROW1_Y + CC_ROW_H + n * CC_DROPDOWN_ITEM_H;
        if (bottom > max_redraw_bottom) max_redraw_bottom = bottom;
    }

    // 网格金框：框住 4 行格子 + 右侧滚动条，画在格子/滚动条之上（仅金色边框，
    // 内部青色键透明，不遮挡内容）。与其它边框资源同款算法。
    {
        H3LoadedPcx16* gridFrame = LoadPanelGridFrame_();
        if (gridFrame)
            DrawTransparentPcx_(gridFrame, scr, GRID_FRAME_X, GRID_FRAME_Y);
    }

    DrawPanelButtons_(scr);

    // Match H3BattleValueInfo's ranged panel: one composite copy to the real
    // DirectDraw backbuffer, then invalidate only the panel region.
    bool drawn = DrawPanelCompositeToBackBuffer_(scr, s_p.x, s_p.y);
    if (!drawn && o_WndMgr && o_WndMgr->screenPcx16) {
        scr->DrawToPcx16(s_p.x, s_p.y, FALSE, o_WndMgr->screenPcx16);
        drawn = true;
    }
    if (drawn && o_WndMgr && !s_panel_redraw_in_progress)
    {
        s_panel_redraw_in_progress = true;
        int redraw_h = PANEL_H;
        if (max_redraw_bottom > redraw_h) redraw_h = max_redraw_bottom;
        o_WndMgr->H3Redraw(s_p.x, s_p.y, PANEL_W, redraw_h);
        s_panel_redraw_in_progress = false;
    }
}

// ========================================================================
// 第七部分：面板控制
// ========================================================================

static bool IsConfigurablePanelStack_(const H3CombatCreature& stack, const H3Hero* hero)
{
    if (stack.numberAlive <= 0)
        return false;

    switch (stack.type) {
    case eCreature::AMMO_CART:
        // 弹药车永远排除。
        return false;
    case eCreature::CATAPULT:
        // 投石车仅在英雄掌握弹道术时显示。
        return hero && hero->secSkill[eSecondary::BALLISTICS] > 0;
    case eCreature::FIRST_AID_TENT:
        // 急救帐篷仅在英雄掌握急救术时显示。
        return hero && hero->secSkill[eSecondary::FIRST_AID] > 0;
    default:
        // 弩车、箭塔、普通部队全部保留。
        return true;
    }
}

static bool StackIsRanged_(const H3CombatCreature& stack)
{
    if (stack.type == eCreature::BALLISTA
        || stack.type == eCreature::ARROW_TOWER)
        return true;
    return stack.info.shooter != 0;
}

static void SaveCurrentCellsToDraft_()
{
    const int profile = s_p.selected_profile;
    if (profile < 0 || profile >= PROFILE_COUNT) return;
    for (int i = 0; i < CELL_COUNT; ++i) {
        CellControl* ctrl = &s_p.cells[i];
        if (!ctrl->has_data) continue;
        const int slot = ctrl->data.army_slot_ix;
        if (slot < 0 || slot >= MAX_STACKS) continue;
        s_p.draft_rules[profile][slot] = ctrl->data.rule;
    }
}

static void LoadSelectedProfileIntoCells_()
{
    const int profile = s_p.selected_profile;
    if (profile < 0 || profile >= PROFILE_COUNT) return;
    for (int i = 0; i < CELL_COUNT; ++i) {
        CellControl* ctrl = &s_p.cells[i];
        if (!ctrl->has_data) continue;
        const int slot = ctrl->data.army_slot_ix;
        if (slot < 0 || slot >= MAX_STACKS) continue;
        ctrl->data.rule = s_p.draft_rules[profile][slot];
        CellControl_NormalizeRule(&ctrl->data.rule, ctrl->data.creature_type,
            CellControl_IsRangedType(ctrl->data.creature_type)
            || (ctrl->data.creature_def && false)); // 下方用更准的 is_ranged 覆盖
        // 用 creature_type 的 ranged 判断已在 Normalize 内；再按类型补一次
        const bool is_ranged = CellControl_IsRangedType(ctrl->data.creature_type)
            || ctrl->data.creature_type == eCreature::BALLISTA
            || ctrl->data.creature_type == eCreature::ARROW_TOWER;
        CellControl_NormalizeRule(&ctrl->data.rule, ctrl->data.creature_type, is_ranged);
        ctrl->expanded = CEX_NONE;
        ctrl->action_pressed = false;
        ctrl->side_pressed = false;
        ctrl->selector_pressed = false;
        ctrl->dirty = true;
    }
    s_p.hover_cell = -1;
    s_p.hover_idx = -1;
}

static void SelectProfile_(int profile)
{
    if (profile < 0 || profile >= PROFILE_COUNT
        || profile == s_p.selected_profile)
        return;
    SaveCurrentCellsToDraft_();
    s_p.selected_profile = profile;
    LoadSelectedProfileIntoCells_();
    DrawPanelToBuffer_();
}

void OpenSettingsPanel_()
{
    H3MouseManager* mouse = H3MouseManager::Get();
    s_p.cursor_saved = mouse != nullptr;
    s_p.saved_cursor_type = mouse ? mouse->GetType() : 0;
    s_p.saved_cursor_frame = mouse ? mouse->GetFrame() : 0;
    s_panel_modal_suspended = false;
    if (!BlockBattleHover_()) {
        s_p.cursor_saved = false;
        WriteLog("[Panel] 无法屏蔽战场悬停，取消打开设置面板。");
        return;
    }
    s_p.active = true;
    s_p.count  = 0;
    s_p.scroll_row = 0;
    s_p.scroll_dragging = false;
    s_p.scroll_drag_offset = 0;
    s_p.scroll_button_pressed = 0;
    s_p.hover_cell = -1;
    s_p.hover_idx = -1;
    s_p.pressed_button = 0;
    s_p.pressed_profile = -1;
    s_p.selected_profile = g_active_profile;
    if (s_p.selected_profile < 0 || s_p.selected_profile >= PROFILE_COUNT)
        s_p.selected_profile = 0;
    memcpy(s_p.draft_rules, g_profiles, sizeof(s_p.draft_rules));
    for (int i = 0; i < CELL_COUNT; ++i)
        CellControl_Init(&s_p.cells[i]);

    if (o_WndMgr && o_WndMgr->screenPcx16) {
        s_p.x = (o_WndMgr->screenPcx16->width  - PANEL_W) / 2;
        s_p.y = (o_WndMgr->screenPcx16->height - PANEL_H) / 2;
    } else {
        s_p.x = (800 - PANEL_W) / 2; s_p.y = (600 - PANEL_H) / 2;
    }
    if (s_p.x < 0) s_p.x = 0; if (s_p.y < 0) s_p.y = 0;

    H3CombatManager* mgr = GetCombatMgr();
    if (mgr) {
        // 当前人类玩家侧：优先 currentActiveSide，否则 0。
        int side = 0;
        if (mgr->isHuman[0]) side = 0;
        else if (mgr->isHuman[1]) side = 1;
        H3Hero* hero = mgr->hero[side];
        for (int i = 0; i < MAX_STACKS && s_p.count < MAX_STACKS; ++i) {
            H3CombatCreature& stack = mgr->stacks[side][i];
            if (IsConfigurablePanelStack_(stack, hero)) {
                CellData cd = {};
                cd.creature_type = stack.type;
                cd.position      = stack.position;
                cd.count_alive   = stack.numberAlive;
                cd.creature_def  = stack.def;
                cd.army_slot_ix  = i;
                cd.rule = s_p.draft_rules[s_p.selected_profile][i];
                const bool is_ranged = StackIsRanged_(stack);
                CellControl_NormalizeRule(&cd.rule, cd.creature_type, is_ranged);
                if (s_p.count < CELL_COUNT)
                    CellControl_SetData(&s_p.cells[s_p.count], &cd);
                ++s_p.count;
            }
        }
    }
    InstallBattleInputBlocker_();
    EnsurePanelButtonPcxResources_();
    ForcePanelDefaultCursor_();
    DrawPanelToBuffer_();
    // 安装键盘钩子，立即响应 ESC/Enter，不受游戏帧率影响
    const DWORD panel_thread = GetWindowThreadProcessId(
        *reinterpret_cast<HWND*>(0x699650), nullptr);
    if (!s_kb_hook)
        s_kb_hook = SetWindowsHookExA(WH_KEYBOARD, PanelKbHook_, g_hModule,
            panel_thread);
    // 安装鼠标钩子，下拉展开时立即刷新悬停高亮，不受游戏帧率影响
    if (!s_mouse_hook)
        s_mouse_hook = SetWindowsHookExA(WH_MOUSE, PanelMouseHook_, g_hModule,
            panel_thread);
    WriteLog("[Panel] 打开设置面板 count=%d at (%d,%d)", s_p.count, s_p.x, s_p.y);
}

void RefreshSettingsPanel() { if (s_p.active) DrawPanelToBuffer_(); }

static void CommitAndCloseSettingsPanel_()
{
    if (!s_p.active) return;

    // 保存当前表格到当前方案副本，再一次性提交全部5套。
    SaveCurrentCellsToDraft_();
    CommitProfiles(s_p.selected_profile, s_p.draft_rules);
    CloseSettingsPanel();
}

void CloseSettingsPanel()
{
    if (!s_p.active) return;
    s_p.active = false;
    s_panel_modal_suspended = false;
    ForcePanelModalDepth_(false);
    RestoreBattleHover_();
    // Allow the same battle to open the panel again, but require a fresh
    // right-click -> explanation shown -> explanation closed sequence.
    s_panel_popup_done = false;
    s_saw_explanation_dlg_in_battle = false;
    s_autofight_right_press_armed = false;
    s_p.pressed_button = 0;
    s_p.pressed_profile = -1;
    s_p.scroll_button_pressed = 0;
    s_p.scroll_dragging = false;
    RemoveBattleInputBlocker_();
    if (s_kb_hook) {
        UnhookWindowsHookEx(s_kb_hook);
        s_kb_hook = nullptr;
    }
    if (s_mouse_hook) {
        UnhookWindowsHookEx(s_mouse_hook);
        s_mouse_hook = nullptr;
    }
    if (s_p.cursor_saved) {
        if (H3MouseManager* mouse = H3MouseManager::Get())
            mouse->SetCursor(s_p.saved_cursor_frame, s_p.saved_cursor_type);
        s_p.cursor_saved = false;
    }
    ReleasePanelComposite_();
    if (H3CombatManager* mgr = GetCombatMgr())
        THISCALL_7(void, 0x493FC0, mgr, FALSE, TRUE, FALSE, 0, TRUE, FALSE);
    WriteLog("[Panel] 设置面板已关闭。");
}

bool IsPanelActive() { return s_p.active; }

// ========================================================================
// 第八部分：点击处理
// ========================================================================

bool HandlePanelClick(int sx, int sy)
{
    if (!s_p.active) return false;
    const int px = sx - s_p.x, py = sy - s_p.y;

    const int first_item = s_p.scroll_row * COLS;
    for (int i = 0; i < CELL_COUNT; ++i) {
        const int item_index = first_item + i;
        if (item_index >= s_p.count) break;
        RECT cRc = CellRect(i);
        if (!PointInRect_(px, py, cRc.left, cRc.top, CELL_W, CELL_H))
            continue;
        // 转发到 CellControl（坐标转相对格子的本地坐标）
        if (CellControl_OnMouse(&s_p.cells[i], 4, px - cRc.left, py - cRc.top, false)) {
            DrawPanelToBuffer_();
            return true;
        }
    }
    return false;
}

// ========================================================================
// 第九部分：滚动条输入
// ========================================================================

static bool PointInRect_(int x, int y, int left, int top, int width, int height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

// 优先处理已展开下拉的点击：下拉展开区可能覆盖到下方格子，必须在普通
// 格子循环之前用展开区 rect 判定并消费，防止点击穿透到下面的格子。
// cell_msg: 4=按下（展开区外点击则收起），8=松开（选中下拉项）。
// 返回 true 表示已消费该点击。
static bool HandleExpandedDropdownClick_(int px, int py, int cell_msg)
{
    const int first_item = s_p.scroll_row * COLS;
    for (int i = 0; i < CELL_COUNT; ++i) {
        const int item_index = first_item + i;
        if (item_index >= s_p.count) break;
        CellControl* ctrl = &s_p.cells[i];
        if (ctrl->expanded == CEX_NONE) continue;
        RECT cRc = CellRect(i);

        RECT dropRc = {};
        const int n = CellControl_GetExpandedItemCount(ctrl);
        CellControl_GetExpandRect(cRc.left, cRc.top, n, &dropRc);

        const bool in_drop = PointInRect_(px, py, dropRc.left, dropRc.top,
            dropRc.right - dropRc.left, dropRc.bottom - dropRc.top);
        // 下拉按钮本身（展开时点它是收起）也算在拥有者格子内。
        const bool in_cell = PointInRect_(px, py, cRc.left, cRc.top, CELL_W, CELL_H);

        if (in_drop || in_cell) {
            CellControl_OnMouse(ctrl, cell_msg,
                px - cRc.left, py - cRc.top, false);
            DrawPanelToBuffer_();
            return true;  // 无论是否改变状态都消费，阻止穿透
        }

        // 点在展开区和拥有者格子之外：按下时收起下拉，但不消费该点击，
        // 让它继续走正常处理（例如点到另一个格子的下拉按钮时应能展开）。
        if (cell_msg == 4) {
            ctrl->expanded = CEX_NONE;
            ctrl->dirty = true;
            DrawPanelToBuffer_();
        }
    }
    return false;
}

// 根据鼠标面板坐标更新展开下拉的悬停项，只在悬停项变化时重绘（无延迟）。
// 返回 true 表示重绘了。
static bool UpdateDropdownHover_(int px, int py)
{
    const int first_item = s_p.scroll_row * COLS;
    int new_hover_cell = -1, new_hover_idx = -1;
    for (int i = 0; i < CELL_COUNT; ++i) {
        const int item_index = first_item + i;
        if (item_index >= s_p.count) break;
        CellControl* ctrl = &s_p.cells[i];
        if (ctrl->expanded == CEX_NONE) continue;
        RECT cRc = CellRect(i);
        const int lx = px - cRc.left;
        const int ly = py - cRc.top;
        if (lx < CC_COMBO_X || lx >= CC_COMBO_X + CC_COMBO_W) {
            new_hover_cell = -1;
            new_hover_idx = -1;
            break;
        }
        const int n = CellControl_GetExpandedItemCount(ctrl);
        const int rel_y = ly - (CC_ROW1_Y + CC_ROW_H);
        int idx = rel_y >= 0 ? rel_y / CC_DROPDOWN_ITEM_H : -1;
        if (idx < 0 || idx >= n) idx = -1;
        new_hover_cell = i;
        new_hover_idx = idx;
        break;
    }
    if (new_hover_cell != s_p.hover_cell || new_hover_idx != s_p.hover_idx) {
        s_p.hover_cell = new_hover_cell;
        s_p.hover_idx = new_hover_idx;
        DrawPanelToBuffer_();
        return true;
    }
    return false;
}

static void HandlePanelMouseMessage_(int raw_command, int screen_x, int screen_y)
{
    if (!s_p.active) return;
    const int px = screen_x - s_p.x;
    const int py = screen_y - s_p.y;
    const int max_row = PanelMaxScrollRow_();
    const int button_size = PanelScrollButtonSize_();

    if (raw_command == 4) {
        if (s_p.scroll_dragging && max_row > 0) {
            const int free_size = SCROLL_H - 3 * button_size;
            int offset = py - s_p.scroll_drag_offset - (SCROLL_Y + button_size);
            if (offset < 0) offset = 0;
            if (offset > free_size) offset = free_size;
            const int row = free_size > 0
                ? (max_row * offset + free_size / 2) / free_size : 0;
            if (row != s_p.scroll_row) {
                SetPanelScrollRow_(row);
                DrawPanelToBuffer_();
            }
            return;
        }
        // 下拉展开时，鼠标移动立即刷新悬停高亮（无延迟）。
        UpdateDropdownHover_(px, py);
        return;
    }

    if (raw_command == 8) {
        for (int i = 0; i < PROFILE_COUNT; ++i) {
            const RECT rc = ProfileButtonRect_(i);
            if (PointInRect_(px, py, rc.left, rc.top,
                    rc.right - rc.left, rc.bottom - rc.top)) {
                s_p.pressed_profile = i;
                DrawPanelToBuffer_();
                return;
            }
        }

        int button = 0;
        if (PointInRect_(px, py, OK_X, BTN_Y, BTN_W, BTN_H)) button = 1;
        else if (PointInRect_(px, py, CANCEL_X, BTN_Y, BTN_W, BTN_H)) button = 2;
        if (button != 0) {
            s_p.pressed_button = button;
            DrawPanelToBuffer_();
            return;
        }

        if (max_row > 0
            && PointInRect_(px, py, SCROLL_X, SCROLL_Y, SCROLL_W, SCROLL_H))
        {
            const int thumb_y = PanelScrollThumbY_();
            if (py < SCROLL_Y + button_size) {
                s_p.scroll_button_pressed = 1;
                SetPanelScrollRow_(s_p.scroll_row - 1);
            } else if (py >= SCROLL_Y + SCROLL_H - button_size) {
                s_p.scroll_button_pressed = 2;
                SetPanelScrollRow_(s_p.scroll_row + 1);
            } else if (py >= thumb_y && py < thumb_y + button_size) {
                s_p.scroll_dragging = true;
                s_p.scroll_drag_offset = py - thumb_y;
            } else if (py < thumb_y) {
                SetPanelScrollRow_(s_p.scroll_row - CELL_COUNT / COLS);
            } else {
                SetPanelScrollRow_(s_p.scroll_row + CELL_COUNT / COLS);
            }
            DrawPanelToBuffer_();
            return;
        }

        // ---- 优先处理展开的下拉：其展开区可能覆盖到下方格子，须先消费防穿透 ----
        if (HandleExpandedDropdownClick_(px, py, 4))
            return;

        // ---- 格子单元格点击 ----
        {
            const int first_item = s_p.scroll_row * COLS;
            for (int i = 0; i < CELL_COUNT; ++i) {
                const int item_index = first_item + i;
                if (item_index >= s_p.count) break;
                RECT cRc = CellRect(i);
                if (PointInRect_(px, py, cRc.left, cRc.top, CELL_W, CELL_H)) {
                    CellControl* ctrl = &s_p.cells[i];
                    // 面板按下(raw 8) → 格子按下(4)，触发下拉展开/收起
                    if (CellControl_OnMouse(ctrl, 4,
                            px - cRc.left, py - cRc.top, false)) {
                        DrawPanelToBuffer_();
                        return;
                    }
                }
            }
        }
        return;
    }

    if (raw_command == 16) {
        const int pressed_profile = s_p.pressed_profile;
        s_p.pressed_profile = -1;
        if (pressed_profile >= 0 && pressed_profile < PROFILE_COUNT) {
            const RECT rc = ProfileButtonRect_(pressed_profile);
            if (PointInRect_(px, py, rc.left, rc.top,
                    rc.right - rc.left, rc.bottom - rc.top))
                SelectProfile_(pressed_profile);
            else
                DrawPanelToBuffer_();
            return;
        }

        const int pressed = s_p.pressed_button;
        const bool activate = pressed == 1
            ? PointInRect_(px, py, OK_X, BTN_Y, BTN_W, BTN_H)
            : pressed == 2
                ? PointInRect_(px, py, CANCEL_X, BTN_Y, BTN_W, BTN_H)
                : false;
        const bool redraw = pressed != 0 || s_p.scroll_button_pressed != 0
            || s_p.scroll_dragging;
        s_p.pressed_button = 0;
        s_p.scroll_button_pressed = 0;
        s_p.scroll_dragging = false;
        if (redraw) DrawPanelToBuffer_();
        if (activate) {
            if (pressed == 1)
                CommitAndCloseSettingsPanel_();
            else
                CloseSettingsPanel();
            return;
        }

        // 优先处理展开的下拉：防止选中点击穿透到下方格子
        if (HandleExpandedDropdownClick_(px, py, 8))
            return;

        // 面板松开(raw 16) → 格子松开(8)，选中展开的下拉项
        {
            const int first_item = s_p.scroll_row * COLS;
            for (int i = 0; i < CELL_COUNT; ++i) {
                const int item_index = first_item + i;
                if (item_index >= s_p.count) break;
                RECT cRc = CellRect(i);
                if (PointInRect_(px, py, cRc.left, cRc.top, CELL_W, CELL_H)) {
                    CellControl* ctrl = &s_p.cells[i];
                    if (CellControl_OnMouse(ctrl, 8,
                            px - cRc.left, py - cRc.top, false)) {
                        DrawPanelToBuffer_();
                        return;
                    }
                }
            }
        }
    }
}

static void HandlePanelInput_()
{
    static bool previous_up_down = false;
    static bool previous_down_down = false;
    static bool previous_page_up_down = false;
    static bool previous_page_down_down = false;

    const bool up_down = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    const bool down_down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    const bool page_up_down = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
    const bool page_down_down = (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
    if (!IsGameWindowForeground_()) {
        CancelPanelTransientInput_();
        previous_up_down = up_down;
        previous_down_down = down_down;
        previous_page_up_down = page_up_down;
        previous_page_down_down = page_down_down;
        return;
    }
    if (up_down && !previous_up_down) SetPanelScrollRow_(s_p.scroll_row - 1);
    if (down_down && !previous_down_down) SetPanelScrollRow_(s_p.scroll_row + 1);
    if (page_up_down && !previous_page_up_down)
        SetPanelScrollRow_(s_p.scroll_row - CELL_COUNT / COLS);
    if (page_down_down && !previous_page_down_down)
        SetPanelScrollRow_(s_p.scroll_row + CELL_COUNT / COLS);

    previous_up_down = up_down;
    previous_down_down = down_down;
    previous_page_up_down = page_up_down;
    previous_page_down_down = page_down_down;
}
