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
static const int CELL_H     = 83;
static const int CELL_STEP_X = CELL_W - 2;
static const int CELL_STEP_Y = CELL_H - 2;
static const int GRID_X      = 36;
static const int GRID_Y      = 70;
static const int SCROLL_X    = GRID_X + CELL_W + (COLS - 1) * CELL_STEP_X + 18;
static const int MARGIN      = 20;
static const int TITLE_H    = 44;
static const int BTN_W      = 80;
static const int BTN_H      = 24;
static const int BTN_Y      = PANEL_H - 44;
static const int OK_X       = PANEL_W - MARGIN - BTN_W;
static const int CANCEL_X   = OK_X - BTN_W - 10;
static const int CELL_COUNT = COLS * 4;

static const char* g_action_labels[] = {
    "手动", "防御", "近战攻击", "随机射击", "顺序射击", "循环移动",
};
static const char* g_target_labels[] = {
    "无", "指定位置", "远程和高速优先", "数量优先",
};

static const INT32 COL_TITLE_TEXT  = 0x1D;
static const INT32 COL_TEXT        = 0x01;
static const INT32 COL_ACTION_TEXT = 0x1A;
static const INT32 COL_TARGET_TEXT = 0x0D;
static const INT32 COL_OK_TEXT     = 0x0D;
static const INT32 COL_CANCEL_TEXT = 0x1B;

static struct Panel {
    bool active;
    int x, y;
    int action[21];
    int target[21];
    int stack_idx[CELL_COUNT];
    int count;
} s_p = {};

// 与 H3BattleValueInfo 远程对比框相同：先离屏合成，再一次性写入 backbuffer。
static H3LoadedPcx16* s_panel_composite = nullptr;
static H3LoadedPcx16* s_panel_background = nullptr;
static H3LoadedPcx16* s_panel_cell = nullptr;
static bool s_panel_background_load_failed = false;
static bool s_panel_cell_load_failed = false;
static bool s_panel_redraw_in_progress = false;

// 战斗内右键说明使用通用 TDialogBox；自动战斗按钮说明实测为 448x128。
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
static void DrawPanelToBuffer_();

// ========================================================================
// 第二部分：工具函数
// ========================================================================

static H3Font* GetPanelFont() { return H3Font::Load("bigfont.fnt"); }
static H3Font* GetSmallFont() { return H3Font::Load("smalfont.fnt"); }

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
    WriteLog("[Panel] PCX 资源加载成功：%s (%dx%d)。", path, width, height);
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

static bool IsPanelCellCyanKey_(int red, int green, int blue)
{
    // HA_cell.pcx uses cyan as a transparency matte. Its antialiased edge is
    // blended with that matte, including low-saturation green edge pixels.
    // The actual frame is gold/brown and therefore always red-dominant.
    return green > red || blue > red;
}

static void DrawPanelCell_(H3LoadedPcx16* destination, int dst_x, int dst_y)
{
    H3LoadedPcx16* cell = LoadPanelCell_();
    if (!cell || !cell->buffer || !destination || !destination->buffer)
        return;

    const bool mode_32_bit = H3BitMode::Get() == 4;
    for (int y = 0; y < CELL_H; ++y) {
        BYTE* dst_row = destination->buffer + (dst_y + y) * destination->scanlineSize;
        const BYTE* src_row = cell->buffer + y * cell->scanlineSize;
        if (mode_32_bit) {
            DWORD* dst = reinterpret_cast<DWORD*>(dst_row) + dst_x;
            const DWORD* src = reinterpret_cast<const DWORD*>(src_row);
            for (int x = 0; x < CELL_W; ++x) {
                const DWORD color = src[x];
                const int red = (color >> 16) & 0xFF;
                const int green = (color >> 8) & 0xFF;
                const int blue = color & 0xFF;
                if (!IsPanelCellCyanKey_(red, green, blue))
                    dst[x] = src[x];
            }
        } else {
            WORD* dst = reinterpret_cast<WORD*>(dst_row) + dst_x;
            const WORD* src = reinterpret_cast<const WORD*>(src_row);
            for (int x = 0; x < CELL_W; ++x) {
                const WORD color = src[x];
                const int red = ((color >> 11) & 0x1F) << 3;
                const int green = ((color >> 5) & 0x3F) << 2;
                const int blue = (color & 0x1F) << 3;
                if (!IsPanelCellCyanKey_(red, green, blue))
                    dst[x] = src[x];
            }
        }
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
    s_panel_background_load_failed = false;
    s_panel_cell_load_failed = false;
}

// ========================================================================
// 第三部分：战斗状态判断
// ========================================================================

static H3CombatManager* GetCombatMgr()
{
    return H3CombatManager::Get();
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

    // 只在窗口链实际变化时记录，避免每帧刷日志。
    static UINT s_logged_first_vtable = 0;
    static UINT s_logged_last_vtable = 0;
    if (first_vtable != s_logged_first_vtable || last_vtable != s_logged_last_vtable) {
        WriteLog("[Dlg] 切换 first=0x%08X(vt=0x%08X) last=0x%08X(vt=0x%08X,w=%d,h=%d) cursorItem=0x%X rbutton=%d",
            (UINT)(INT_PTR)first, first_vtable,
            (UINT)(INT_PTR)last, last_vtable, last_w, last_h,
            cursor_item_id,
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
        s_logged_first_vtable = first_vtable;
        s_logged_last_vtable = last_vtable;
    }

    const bool battle_ui_exists = combat_dlg != nullptr;
    const bool right_button_down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
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
        const H3POINT cursor = H3POINT::GetCursorPosition();
        WriteLog("[AutoFight] 说明框出现但尚未命中目标按钮 cursor=(%d,%d) cursorItem=0x%X rbutton=%d。",
            cursor.x, cursor.y, cursor_item_id, right_button_down);
        s_logged_unarmed_explanation = last;
    } else if (last_vtable != s_right_click_dialog_vtable) {
        s_logged_unarmed_explanation = nullptr;
    }

    if (battle_ui_exists) {
        s_battle_ui_missing_frames = 0;
    } else if (++s_battle_ui_missing_frames >= 3) {
        if (s_saw_explanation_dlg_in_battle || s_panel_popup_done)
            WriteLog("[State] BattleUI 已离开，重置说明框状态。");
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
    // 每30帧输出一次心跳日志
    if (s_frame % 30 == 0) {
        H3BaseDlg* top = o_WndMgr ? o_WndMgr->lastDlg : nullptr;
        UINT vtable = top ? *(UINT*)top : 0;
        WriteLog("[Blt] frame=%d last_vtable=0x%08X explanation=%d panel=%d",
            s_frame, vtable, s_saw_explanation_dlg_in_battle, s_p.active);
    }
    CheckAutoFightDialogClosed();
    if (s_p.active) DrawPanelToBuffer_();
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
    DrawTxt(scr, GetPanelFont(), "部队自动行动设置",
        px + 20, py + 8, PANEL_W - 40, 34, COL_TITLE_TEXT, eTextAlignment::MIDDLE_CENTER);

    H3Font* fntS = GetSmallFont();
    for (int i = 0; i < s_p.count && i < CELL_COUNT; ++i) {
        int si = s_p.stack_idx[i];
        if (si < 0) continue;
        RECT cRc = CellRect(i);
        RECT aRc = ActionBtnRect(i);
        RECT tRc = TargetBtnRect(i);

        DrawPanelCell_(scr, cRc.left, cRc.top);

        char coord[16] = "--";
        H3CombatManager* mgr = GetCombatMgr();
        if (mgr && si >= 0 && si < 21) {
            int hx = mgr->stacks[0][si].position;
            if (hx >= 0 && hx < 40)
                _snprintf(coord, sizeof(coord), "%c%02d", 'A' + (hx % 8), hx / 8 + 1);
        }
        DrawTxt(scr, fntS, coord, cRc.left + 4, cRc.top + 2, CELL_W - 8, 16,
            COL_TEXT, eTextAlignment::TOP_LEFT);

        DrawTxt(scr, fntS, g_action_labels[s_p.action[si]],
            aRc.left, aRc.top, aRc.right - aRc.left, aRc.bottom - aRc.top,
            COL_ACTION_TEXT, eTextAlignment::MIDDLE_CENTER);

        DrawTxt(scr, fntS, g_target_labels[s_p.target[si]],
            tRc.left, tRc.top, tRc.right - tRc.left, tRc.bottom - tRc.top,
            COL_TARGET_TEXT, eTextAlignment::MIDDLE_CENTER);
    }

    Fill(scr, px + OK_X, py + BTN_Y, BTN_W, BTN_H, 0, 100, 0);
    DrawTxt(scr, GetSmallFont(), "确定", px + OK_X, py + BTN_Y, BTN_W, BTN_H,
        COL_OK_TEXT, eTextAlignment::MIDDLE_CENTER);
    Fill(scr, px + CANCEL_X, py + BTN_Y, BTN_W, BTN_H, 100, 0, 0);
    DrawTxt(scr, GetSmallFont(), "取消", px + CANCEL_X, py + BTN_Y, BTN_W, BTN_H,
        COL_CANCEL_TEXT, eTextAlignment::MIDDLE_CENTER);

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
        o_WndMgr->H3Redraw(s_p.x, s_p.y, PANEL_W, PANEL_H);
        s_panel_redraw_in_progress = false;
    }
}

// ========================================================================
// 第七部分：面板控制
// ========================================================================

void OpenSettingsPanel_()
{
    s_p.active = true;
    s_p.count  = 0;
    memset(s_p.action, 0, sizeof(s_p.action));
    memset(s_p.target, 0, sizeof(s_p.target));

    if (o_WndMgr && o_WndMgr->screenPcx16) {
        s_p.x = (o_WndMgr->screenPcx16->width  - PANEL_W) / 2;
        s_p.y = (o_WndMgr->screenPcx16->height - PANEL_H) / 2;
    } else {
        s_p.x = (800 - PANEL_W) / 2; s_p.y = (600 - PANEL_H) / 2;
    }
    if (s_p.x < 0) s_p.x = 0; if (s_p.y < 0) s_p.y = 0;

    H3CombatManager* mgr = GetCombatMgr();
    if (mgr) {
        for (int i = 0; i < 21 && s_p.count < CELL_COUNT; ++i) {
            if (mgr->stacks[0][i].numberAlive > 0) {
                s_p.stack_idx[s_p.count] = i;
                s_p.action[i] = g_action_strategies[i];
                s_p.target[i] = g_target_strategies[i];
                ++s_p.count;
            }
        }
    }
    DrawPanelToBuffer_();
    WriteLog("[Panel] 打开设置面板 count=%d at (%d,%d)", s_p.count, s_p.x, s_p.y);
}

void RefreshSettingsPanel() { if (s_p.active) DrawPanelToBuffer_(); }

void CloseSettingsPanel()
{
    if (!s_p.active) return;
    s_p.active = false;
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
    int px = sx - s_p.x, py = sy - s_p.y;

    if (px >= OK_X && px < OK_X + BTN_W && py >= BTN_Y && py < BTN_Y + BTN_H) {
        for (int i = 0; i < s_p.count && i < CELL_COUNT; ++i) {
            int si = s_p.stack_idx[i];
            if (si >= 0 && si < 21) {
                g_action_strategies[si] = s_p.action[si];
                g_target_strategies[si] = s_p.target[si];
            }
        }
        extern void CommitStrategies(int side, int* actions, int* targets);
        CommitStrategies(0, g_action_strategies, g_target_strategies);
        WriteLog("[Panel] OK");
        CloseSettingsPanel();
        return true;
    }
    if (px >= CANCEL_X && px < CANCEL_X + BTN_W && py >= BTN_Y && py < BTN_Y + BTN_H) {
        WriteLog("[Panel] Cancel");
        CloseSettingsPanel();
        return true;
    }
    for (int i = 0; i < s_p.count && i < CELL_COUNT; ++i) {
        int si = s_p.stack_idx[i];
        if (si < 0) continue;
        RECT aRc = ActionBtnRect(i);
        RECT tRc = TargetBtnRect(i);
        if (px >= aRc.left && px < aRc.right && py >= aRc.top && py < aRc.bottom) {
            WriteLog("[Panel] 点击行动按钮 stack=%d", si); return true;
        }
        if (px >= tRc.left && px < tRc.right && py >= tRc.top && py < tRc.bottom) {
            WriteLog("[Panel] 点击目标按钮 stack=%d", si); return true;
        }
    }
    return false;
}

// ========================================================================
// 第九部分：输入处理后续扩展
