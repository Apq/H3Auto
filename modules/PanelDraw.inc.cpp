// PanelDraw.inc.cpp - 设置面板内容绘制（依赖 s_p / 标签 / CellControl）。
#include "PanelLayout.hpp"
// 在 H3Auto.cpp 中排在 SettingsDlg.inc.cpp 之后；调用的图像原语来自
// PanelGfx.inc.cpp（更早），面板状态/标签来自 SettingsDlg.inc.cpp。
// ========================================================================
// 第六部分：绘制
// ========================================================================

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
        _snprintf(text, sizeof(text), T("panel.profile_btn_fmt"), i + 1);
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

static void DrawMeleePickMarker_()
{
    if (!s_panel_hidden_for_pick || s_melee_pick_phase != 2
        || !CellControl_HexValid(s_melee_pick_stand_hex))
        return;

    H3CombatManager* mgr = GetCombatMgr();
    if (!mgr || !mgr->CCellShdPcx) return;
    __try {
        // 可见路径：画到 screenPcx16 + 后缓冲。
        // 绝不能 ShadeSquare/写 drawBuffer——那会污染战场离屏缓冲，
        // 结束拾取后的 Refresh 会把蓝标重新画出来。
        // 只脏 screen 层；结束时完整 Refresh 从干净 drawBuffer 重建即可撤销。
        H3CombatSquare& sq = mgr->squares[s_melee_pick_stand_hex];
        int dlg_x = 0;
        int dlg_y = 0;
        if (mgr->dlg) {
            dlg_x = mgr->dlg->GetX();
            dlg_y = mgr->dlg->GetY();
        } else {
            BYTE* base = reinterpret_cast<BYTE*>(mgr);
            BYTE* raw_dlg = *reinterpret_cast<BYTE**>(base + 0x132FC);
            if (raw_dlg) {
                dlg_x = *reinterpret_cast<INT32*>(raw_dlg + 0x18);
                dlg_y = *reinterpret_cast<INT32*>(raw_dlg + 0x1C);
            }
        }
        const int abs_x = dlg_x + static_cast<int>(sq.left);
        const int abs_y = dlg_y + static_cast<int>(sq.top);

        static H3LoadedPcx16* s_marker_tile = nullptr;
        if (!s_marker_tile)
            s_marker_tile = H3LoadedPcx16::Create(0x2D, 0x34);
        if (!s_marker_tile || !s_marker_tile->buffer) return;

        const bool mode32 = H3BitMode::Get() == 4;
        for (int y = 0; y < s_marker_tile->height; ++y) {
            BYTE* row = s_marker_tile->buffer + y * s_marker_tile->scanlineSize;
            if (mode32) {
                DWORD* px = reinterpret_cast<DWORD*>(row);
                for (int x = 0; x < s_marker_tile->width; ++x)
                    px[x] = 0xFF00FFFFu;
            } else {
                WORD* px = reinterpret_cast<WORD*>(row);
                for (int x = 0; x < s_marker_tile->width; ++x)
                    px[x] = 0x7FDF;
            }
        }
        mgr->CCellShdPcx->DrawToPcx16(0, 0, 0x2D, 0x34,
            s_marker_tile, 0, 0, TRUE);

        // BltComplete 钩在呈现前后缓冲；只写后缓冲会被下一帧战场重画盖掉。
        // 同步写 screenPcx16 才能稳定可见。
        bool screen_ok = false;
        if (o_WndMgr && o_WndMgr->screenPcx16) {
            mgr->CCellShdPcx->DrawToPcx16(0, 0, 0x2D, 0x34,
                o_WndMgr->screenPcx16, abs_x, abs_y, TRUE);
            if (!s_panel_redraw_in_progress) {
                s_panel_redraw_in_progress = true;
                o_WndMgr->H3Redraw(abs_x, abs_y, 0x2D, 0x34);
                s_panel_redraw_in_progress = false;
            }
            screen_ok = true;
        }
        const bool blitted = BlitPcx16ToBackBuffer_(s_marker_tile, abs_x, abs_y);

        static int s_marker_log_hex = -1;
        if (s_marker_log_hex != s_melee_pick_stand_hex) {
            s_marker_log_hex = s_melee_pick_stand_hex;
            LogDebug("[Panel] melee marker draw hex=%d rel=(%d,%d) abs=(%d,%d) dlg=(%d,%d) screen=%d blt=%d",
                s_melee_pick_stand_hex, (int)sq.left, (int)sq.top,
                abs_x, abs_y, dlg_x, dlg_y, screen_ok ? 1 : 0, blitted ? 1 : 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogError("[Panel] melee marker draw exception hex=%d", s_melee_pick_stand_hex);
    }
}

static void GetHelpButtonRect_(int* out_x, int* out_y, int* out_w, int* out_h)
{
    if (out_x) *out_x = HELP_BTN_X;
    if (out_y) *out_y = HELP_BTN_Y;
    if (out_w) *out_w = HELP_BTN_SIZE;
    if (out_h) *out_h = HELP_BTN_SIZE;
}

static void GetHelpModalRect_(int* out_x, int* out_y, int* out_w, int* out_h)
{
    const int w = 480;
    const int h = 412; // 原 336 + 日志级别行 + 打包日志按钮行
    if (out_x) *out_x = (PANEL_W - w) / 2;
    if (out_y) *out_y = (PANEL_H - h) / 2;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static void GetHelpModalCloseRect_(int* out_x, int* out_y, int* out_w, int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;
    GetHelpModalRect_(&x, &y, &w, &h);
    const int bw = 92;
    const int bh = 26;
    if (out_x) *out_x = x + (w - bw) / 2;
    if (out_y) *out_y = y + h - bh - 14;
    if (out_w) *out_w = bw;
    if (out_h) *out_h = bh;
}

// 帮助模态新增行：日志级别下拉（收起态框体）与打包日志按钮。
    // 级别序号 → 选项文案键（0..4 = trace..error）。
static const char* HelpLogLevelOptKey_(int lv)
{
    static const char* const k[] = {
        "help.log_level_opt0", "help.log_level_opt1", "help.log_level_opt2",
        "help.log_level_opt3", "help.log_level_opt4",
    };
    return (lv >= 0 && lv < 5) ? k[lv] : k[2];
}

static const int HELP_DD_LABEL_W = 80;
static const int HELP_DD_W  = 120;
static const int HELP_DD_H  = 22;
static const int HELP_DD_ITEM_H = 20;
static const int HELP_PACK_BTN_W = 180;
static const int HELP_PACK_BTN_H = 26;

// 行 A 顶（原 8 行 help_lines 底之下）。
static void GetHelpLogLevelRowY_(int* out_y)
{
    int x = 0, y = 0, w = 0, h = 0;
    GetHelpModalRect_(&x, &y, &w, &h);
    if (out_y) *out_y = y + 50 + 8 * 28 + 6; // 8 行说明之后
}

static void GetHelpLogLevelDdRect_(int* out_x, int* out_y, int* out_w, int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;
    GetHelpModalRect_(&x, &y, &w, &h);
    int ry = 0;
    GetHelpLogLevelRowY_(&ry);
    if (out_x) *out_x = x + 18 + HELP_DD_LABEL_W + 8;
    if (out_y) *out_y = ry;
    if (out_w) *out_w = HELP_DD_W;
    if (out_h) *out_h = HELP_DD_H;
}

static void GetHelpLogLevelItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;
    GetHelpLogLevelDdRect_(&x, &y, &w, &h);
    if (out_x) *out_x = x;
    if (out_y) *out_y = y + HELP_DD_H + item * HELP_DD_ITEM_H;
    if (out_w) *out_w = w;
    if (out_h) *out_h = HELP_DD_ITEM_H;
}

static void GetHelpPackBtnRect_(int* out_x, int* out_y, int* out_w, int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;
    GetHelpModalRect_(&x, &y, &w, &h);
    int ry = 0;
    GetHelpLogLevelRowY_(&ry);
    if (out_x) *out_x = x + 18;
    if (out_y) *out_y = ry + HELP_DD_H + 10;
    if (out_w) *out_w = HELP_PACK_BTN_W;
    if (out_h) *out_h = HELP_PACK_BTN_H;
}

static void DrawHelpButton_(H3LoadedPcx16* destination)
{
    if (!destination) return;
    int x = 0, y = 0, w = 0, h = 0;
    GetHelpButtonRect_(&x, &y, &w, &h);
    const bool pressed = s_p.pressed_button == 3;
    Fill(destination, x, y, w, h, pressed ? 90 : 62, pressed ? 58 : 40, pressed ? 28 : 18);
    destination->DrawFrame(x, y, w, h, (BYTE)232, (BYTE)196, (BYTE)96);
    destination->DrawFrame(x + 1, y + 1, w - 2, h - 2, (BYTE)112, (BYTE)82, (BYTE)36);
    DrawTxt(destination, GetPanelFont(), "?",
        x, y - 1, w, h,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
}

// pressed_button：4=读档，5=存档。
static void DrawStoreButton_(H3LoadedPcx16* destination, int x, int pressed_id,
    const char* label)
{
    if (!destination) return;
    const bool pressed = s_p.pressed_button == pressed_id;
    Fill(destination, x, HELP_BTN_Y, STORE_BTN_W, HELP_BTN_SIZE,
        pressed ? 90 : 62, pressed ? 58 : 40, pressed ? 28 : 18);
    destination->DrawFrame(x, HELP_BTN_Y, STORE_BTN_W, HELP_BTN_SIZE,
        (BYTE)232, (BYTE)196, (BYTE)96);
    destination->DrawFrame(x + 1, HELP_BTN_Y + 1, STORE_BTN_W - 2,
        HELP_BTN_SIZE - 2, (BYTE)112, (BYTE)82, (BYTE)36);
    DrawTxt(destination, GetSmallFont(), label,
        x, HELP_BTN_Y, STORE_BTN_W, HELP_BTN_SIZE,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
}

static void DrawHelpModal_(H3LoadedPcx16* scr)
{
    if (!scr || !s_help_modal_open) return;
    int x = 0, y = 0, w = 0, h = 0;
    GetHelpModalRect_(&x, &y, &w, &h);

    // 遮暗整张设置面板，形成明确模态层。
    for (int yy = 0; yy < PANEL_H; yy += 2)
        Fill(scr, 0, yy, PANEL_W, 1, 22, 18, 14);

    Fill(scr, x, y, w, h, 48, 32, 18);
    scr->DrawFrame(x, y, w, h, (BYTE)232, (BYTE)196, (BYTE)96);
    scr->DrawFrame(x + 2, y + 2, w - 4, h - 4, (BYTE)112, (BYTE)82, (BYTE)36);

    H3Font* title_font = GetPanelFont();
    H3Font* small_font = GetSmallFont();
    DrawTxt(scr, title_font, T("help.title"),
        x + 16, y + 12, w - 32, 26,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);

    char toggle_name[16];
    char oneshot_name[16];
    char open_name[16];
    char hotkey_line[192];
    snprintf(hotkey_line, sizeof(hotkey_line), T("help.line_hotkey"),
        HotkeyDisplayName_(cfg.toggle_manual_vk, toggle_name, sizeof(toggle_name)),
        HotkeyDisplayName_(cfg.one_shot_manual_vk, oneshot_name, sizeof(oneshot_name)),
        HotkeyDisplayName_(cfg.open_settings_vk, open_name, sizeof(open_name)));

    // 打开方法单独一行：右键「自动战斗」或按配置的打开设置键（键名读配置）。
    char open_key[16];
    char open_line[192];
    snprintf(open_line, sizeof(open_line), T("help.line_open"),
        HotkeyDisplayName_(cfg.open_settings_vk, open_key, sizeof(open_key)));

    const char* help_lines[] = {
        open_line,
        T("help.line1"),
        T("help.line2"),
        T("help.line3"),
        T("help.line4"),
        T("help.line5"),
        hotkey_line,
        T("help.line6"),
    };
    const int line_h = 28;
    int ty = y + 50;
    for (int i = 0; i < (int)(sizeof(help_lines) / sizeof(help_lines[0])); ++i) {
        DrawTxt(scr, small_font, help_lines[i],
            x + 18, ty, w - 36, line_h,
            (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        ty += line_h;
    }

// 行 A：日志级别下拉（选项 = 全部/调试/信息/警告/错误，对应 trace..error）。
    {
        int ry = 0;
        GetHelpLogLevelRowY_(&ry);
        DrawTxt(scr, small_font, T("help.log_level_label"),
            x + 18, ry, HELP_DD_LABEL_W, HELP_DD_H,
            (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        int dx = 0, dy = 0, dw = 0, dh = 0;
        GetHelpLogLevelDdRect_(&dx, &dy, &dw, &dh);
        Fill(scr, dx, dy, dw, dh, s_help_log_dd_open ? 104 : 74,
            s_help_log_dd_open ? 70 : 52, s_help_log_dd_open ? 28 : 24);
        scr->DrawFrame(dx, dy, dw, dh, (BYTE)210, (BYTE)170, (BYTE)72);
        DrawTxt(scr, small_font, T(HelpLogLevelOptKey_(g_log_level)),
            dx + 6, dy, dw - 20, dh,
            (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
        CellControl_DrawArrow(scr, dx + dw - 14, dy + dh / 2 - 2,
            !s_help_log_dd_open);
    }
    // 行 B：打包日志按钮。
    {
        int bx = 0, by = 0, bw = 0, bh = 0;
        GetHelpPackBtnRect_(&bx, &by, &bw, &bh);
        Fill(scr, bx, by, bw, bh, 74, 50, 27);
        scr->DrawFrame(bx, by, bw, bh, (BYTE)196, (BYTE)154, (BYTE)68);
        DrawTxt(scr, small_font, T("help.pack_btn"),
            bx, by, bw, bh, (INT32)eTextColor::WHITE,
            eTextAlignment::MIDDLE_CENTER);
    }
    // 展开的级别列表（最后绘制，盖在按钮上层）。
    if (s_help_log_dd_open) {
        static const char* const kKeys[] = {
            "help.log_level_opt0", "help.log_level_opt1", "help.log_level_opt2",
            "help.log_level_opt3", "help.log_level_opt4",
        };
        for (int i = 0; i < 5; ++i) {
            int ix = 0, iy = 0, iw = 0, ih = 0;
            GetHelpLogLevelItemRect_(i, &ix, &iy, &iw, &ih);
            const bool cur = (i == g_log_level);
            Fill(scr, ix, iy, iw, ih,
                cur ? 136 : 68, cur ? 88 : 42, cur ? 24 : 18);
            scr->DrawFrame(ix, iy, iw, ih,
                (BYTE)(cur ? 232 : 166), (BYTE)(cur ? 184 : 112), (BYTE)(cur ? 76 : 40));
            DrawTxt(scr, small_font, T(kKeys[i]),
                ix + 6, iy, iw - 12, ih,
                (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        }
    }

    int bx = 0, by = 0, bw = 0, bh = 0;
    GetHelpModalCloseRect_(&bx, &by, &bw, &bh);
    Fill(scr, bx, by, bw, bh, 74, 50, 27);
    scr->DrawFrame(bx, by, bw, bh, (BYTE)196, (BYTE)154, (BYTE)68);
    DrawTxt(scr, small_font, T("help.close"),
        bx, by, bw, bh, (INT32)eTextColor::WHITE,
        eTextAlignment::MIDDLE_CENTER);
}

// ===== 方案 A tips：悬停目标 → 状态栏提示文案 =====
// 表格内按可见卡片细分到控件（行动/施法槽/近战/移动/保活勾选等），
// 命不中控件再退回面板级矩形表。
static const char* PanelTipAt_(int px, int py)
{
    const int first_item = s_p.scroll_row * COLS;
    for (int i = 0; i < CELL_COUNT; ++i) {
        if (first_item + i >= s_p.count) break;
        RECT cRc = CellRect(i);
        if (px < cRc.left || px >= cRc.right || py < cRc.top || py >= cRc.bottom)
            continue;
        CellControl* ctrl = &s_p.cells[i];
        if (!ctrl->has_data) continue;
        const CellHitArea hit = CellControl_HitTestInCell(ctrl, px - cRc.left, py - cRc.top);
        if (const char* tip = CellControl_TipForHit(hit, ctrl))
            return tip;
        // 标签「行动前循环快捷施法:」本身不在槽位命中区内，整行都给施法说明。
        if (py >= cRc.top + CC_SPELL_Y && py < cRc.top + CC_SPELL_Y + CC_ROW_H
            && px >= cRc.left + CC_COL2_X && px < cRc.left + CC_COL3_RIGHT)
            return CellControl_TipForHit(
                static_cast<CellHitArea>(CELL_HIT_SPELL_BASE), ctrl);
    }
    struct TipRect { int x, y, w, h; const char* text; };
    static const TipRect kTips[] = {
        { LOAD_BTN_X, HELP_BTN_Y, STORE_BTN_W, HELP_BTN_SIZE, nullptr },
        { SAVE_BTN_X, HELP_BTN_Y, STORE_BTN_W, HELP_BTN_SIZE, nullptr },
        { PROFILE_BTN_X, PROFILE_BTN_Y, PROFILE_BTNS_W, PROFILE_BTN_H, nullptr },
        { 20, PROTECT_DD_Y - 4, STOP_LABEL_X - 24, 30, nullptr },
        { STOP_LABEL_X, PROTECT_DD_Y - 4, STOP_LABEL_W + STOP_BOX_W + 8, 30, nullptr },
        { OK_X, BTN_Y, BTN_W, BTN_H, nullptr },
        { CANCEL_X, BTN_Y, BTN_W, BTN_H, nullptr },
    };
    static const char* const kTipKeys[] = {
        "tips.btn_load", "tips.btn_save", "tips.btn_profile",
        nullptr, "tips.stop_row", "tips.btn_ok", "tips.btn_cancel",
    };
    for (int i = 0; i < (int)(sizeof(kTips) / sizeof(kTips[0])); ++i) {
        const TipRect& t = kTips[i];
        if (px >= t.x && px < t.x + t.w && py >= t.y && py < t.y + t.h) {
            // 保活策略下拉：提示跟随当前选中项（kTipKeys 第 4 项）。
            if (i == 3) {
                const int cur = s_p.draft_protect_strategy[s_p.selected_profile];
                if (cur < 0 || cur >= (int)H3AutoPolicy::PS_COUNT)
                    return T("tips.cell_drop");
                char key[24] = {};
                _snprintf(key, sizeof(key) - 1, "tips.protect_opt%d", cur);
                return T(key);
            }
            return T(kTipKeys[i]);
        }
    }
    // 标题带热键说明：只兜标题文字附近（居中绘制，按文字宽估算），
    // 不再整条 680×44 触发（按钮/空白处不给这个 tip）。
    {
        int hi = 0, lo = 0; // UTF-8：汉字 3 字节各占 16px、ASCII 每字节 8px（近似）
        for (const unsigned char* s = (const unsigned char*)PanelTitle_(); *s; ++s) {
            if (*s >= 0x80) ++hi; else ++lo;
        }
        const int text_w = (hi / 3) * 16 + lo * 8 + 24;
        const int tx0 = (PANEL_W - text_w) / 2;
        if (px >= tx0 && px < tx0 + text_w && py >= 0 && py < TITLE_H) {
        static char title_tip[256];
        char t1[16], t2[16], t3[16];
        snprintf(title_tip, sizeof(title_tip), T("tips.titlebar"),
            HotkeyDisplayName_(cfg.toggle_manual_vk, t1, sizeof(t1)),
            HotkeyDisplayName_(cfg.one_shot_manual_vk, t2, sizeof(t2)),
            HotkeyDisplayName_(cfg.open_settings_vk, t3, sizeof(t3)));
        return title_tip;
        }
    }
    return nullptr;
}

static void GetSpellKeyModalRect_(int* out_x, int* out_y, int* out_w, int* out_h)
{
    const int w = 360;
    const int h = 164;
    if (out_x) *out_x = (PANEL_W - w) / 2;
    if (out_y) *out_y = (PANEL_H - h) / 2;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static void GetProtectDdItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h)
{
    if (out_x) *out_x = PROTECT_DD_X;
    if (out_y) *out_y = PROTECT_DD_Y + PROTECT_DD_H + item * PROTECT_DD_ITEM_H;
    if (out_w) *out_w = PROTECT_DD_W;
    if (out_h) *out_h = PROTECT_DD_ITEM_H;
}

// 收起态：label「保活策略:」+ 当前项 + 下拉箭头。
// 配色与卡片下拉（CellControl_DrawButtonBg / CellControl_DrawDropdownItem）对齐：
// 深棕底避开格子的青色键色（16-bit 0x7FDF），否则合成时会被当透明抠掉。
// 保活策略项文案（i18n；顺序同 ProtectStrategy）。
static const char* ProtectStrategyLabel_(int i)
{
    switch (i) {
    case 0: return T("panel.protect_opt0");
    case 1: return T("panel.protect_opt1");
    case 2: return T("panel.protect_opt2");
    case 3: return T("panel.protect_opt3");
    case 4: return T("panel.protect_opt4");
    default: return "?";
    }
}

static void DrawProtectStrategyRow_(H3LoadedPcx16* scr)
{
    if (!scr) return;
    const int current = s_p.draft_protect_strategy[s_p.selected_profile];
    const char* text = (current >= 0 && current < (int)H3AutoPolicy::PS_COUNT)
        ? ProtectStrategyLabel_(current) : "?";
    H3Font* small_font = GetSmallFont();

    DrawTxt(scr, small_font, T("panel.protect_label"),
        PROTECT_DD_LABEL_X, PROTECT_DD_Y, PROTECT_DD_LABEL_W, PROTECT_DD_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);

    // 框体：普通 74,52,24 / 展开 104,70,28（卡片按钮按下色），金框 210,170,72
    Fill(scr, PROTECT_DD_X, PROTECT_DD_Y, PROTECT_DD_W, PROTECT_DD_H,
        s_protect_dd_open ? 104 : 74, s_protect_dd_open ? 70 : 52,
        s_protect_dd_open ? 28 : 24);
    scr->DrawFrame(PROTECT_DD_X, PROTECT_DD_Y, PROTECT_DD_W, PROTECT_DD_H,
        (BYTE)210, (BYTE)170, (BYTE)72);
    DrawTxt(scr, small_font, text,
        PROTECT_DD_X + 6, PROTECT_DD_Y, PROTECT_DD_W - 20, PROTECT_DD_H,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
    // 金色像素三角箭头，与卡片下拉一致（收起▼/展开▲）
    CellControl_DrawArrow(scr, PROTECT_DD_X + PROTECT_DD_W - 14,
        PROTECT_DD_Y + PROTECT_DD_H / 2 - 2, !s_protect_dd_open);

    DrawTxt(scr, small_font, T("panel.stop_label"),
        STOP_LABEL_X, PROTECT_DD_Y, STOP_LABEL_W, PROTECT_DD_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_RIGHT);
    char num[8] = {};
    if (s_stop_turns_editing)
        _snprintf(num, sizeof(num), "%s", s_stop_turns_text);
    else
        _snprintf(num, sizeof(num), "%d",
            (int)s_p.draft_stop_turns[s_p.selected_profile]);
    Fill(scr, STOP_BOX_X, PROTECT_DD_Y, STOP_BOX_W, PROTECT_DD_H,
        s_stop_turns_editing ? 104 : 74,
        s_stop_turns_editing ? 70 : 52,
        s_stop_turns_editing ? 28 : 24);
    scr->DrawFrame(STOP_BOX_X, PROTECT_DD_Y, STOP_BOX_W, PROTECT_DD_H,
        (BYTE)210, (BYTE)170, (BYTE)72);
    if (s_stop_turns_editing) {
        // 编辑态：左对齐绘制 + 500ms 闪烁光标（BltComplete 每帧重绘面板，
        // 按键/移动重置基准，保证输入后光标立即可见）。
        const int text_x = STOP_BOX_X + 8;
        DrawTxt(scr, small_font, num, text_x, PROTECT_DD_Y,
            STOP_BOX_W - 12, PROTECT_DD_H,
            (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
        const bool caret_on =
            ((GetTickCount() - s_stop_turns_caret_tick) / 500) % 2 == 0;
        if (caret_on) {
            char prefix[8] = {};
            const int caret = s_stop_turns_caret;
            if (caret > 0)
                memcpy(prefix, num, caret < 7 ? caret : 7);
            const INT32 prefix_w =
                small_font ? small_font->GetMaxLineWidth(prefix) : 0;
            Fill(scr, text_x + prefix_w,
                PROTECT_DD_Y + (PROTECT_DD_H - 10) / 2, 2, 10,
                210, 170, 72); // 金色竖线光标
        }
    } else {
        DrawTxt(scr, small_font, num[0] ? num : "0",
            STOP_BOX_X, PROTECT_DD_Y, STOP_BOX_W, PROTECT_DD_H,
            (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
    }
}

// 展开列表：每项单独底色+边框（同卡片下拉暖色主题），
// 普通 68,42,18 / 当前项 136,88,24 / 悬停 184,136,48。
static void DrawProtectDropdownList_(H3LoadedPcx16* scr)
{
    if (!scr || !s_protect_dd_open) return;
    const int current = s_p.draft_protect_strategy[s_p.selected_profile];
    H3Font* small_font = GetSmallFont();

    for (int i = 0; i < (int)H3AutoPolicy::PS_COUNT; ++i) {
        int ix = 0, iy = 0, iw = 0, ih = 0;
        GetProtectDdItemRect_(i, &ix, &iy, &iw, &ih);
        BYTE bg_r, bg_g, bg_b, frame_r, frame_g, frame_b;
        if (i == s_protect_dd_hover) {
            bg_r = 184; bg_g = 136; bg_b = 48;
            frame_r = 246; frame_g = 214; frame_b = 116;
        } else if (i == current) {
            bg_r = 136; bg_g = 88; bg_b = 24;
            frame_r = 232; frame_g = 184; frame_b = 76;
        } else {
            bg_r = 68; bg_g = 42; bg_b = 18;
            frame_r = 166; frame_g = 112; frame_b = 40;
        }
        Fill(scr, ix, iy, iw, ih, bg_r, bg_g, bg_b);
        scr->DrawFrame(ix, iy, iw, ih, frame_r, frame_g, frame_b);
        DrawTxt(scr, small_font, ProtectStrategyLabel_(i),
            ix + 6, iy, iw - 12, ih,
            (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
    }
}

static void GetSpellKeyModalCancelRect_(int* out_x, int* out_y, int* out_w, int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;
    GetSpellKeyModalRect_(&x, &y, &w, &h);
    const int bw = 84;
    const int bh = 26;
    if (out_x) *out_x = x + (w - bw) / 2;
    if (out_y) *out_y = y + h - bh - 16;
    if (out_w) *out_w = bw;
    if (out_h) *out_h = bh;
}

static void DrawSpellKeyModal_(H3LoadedPcx16* scr)
{
    // 循环施法录入（s_spell_pick_cell）模态框。
    if (!scr || s_spell_pick_cell < 0) return;
    int x = 0, y = 0, w = 0, h = 0;
    GetSpellKeyModalRect_(&x, &y, &w, &h);

    // 先遮暗整个设置面板，形成明确模态层。
    for (int yy = 0; yy < PANEL_H; yy += 2)
        Fill(scr, 0, yy, PANEL_W, 1, 22, 18, 14);

    // 模态框本体：深色底 + 双层金框。
    Fill(scr, x, y, w, h, 52, 35, 20);
    scr->DrawFrame(x, y, w, h, (BYTE)232, (BYTE)196, (BYTE)96);
    scr->DrawFrame(x + 2, y + 2, w - 4, h - 4, (BYTE)112, (BYTE)82, (BYTE)36);

    H3Font* title_font = GetPanelFont();
    H3Font* small_font = GetSmallFont();
    char slot_text[64] = {};
    DrawTxt(scr, title_font, T("cell.spell_modal_title"),
        x + 16, y + 14, w - 32, 28,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
    _snprintf(slot_text, sizeof(slot_text), T("cell.spell_modal_slot"),
        s_spell_pick_slot + 1);
    DrawTxt(scr, small_font, slot_text,
        x + 16, y + 48, w - 32, 20,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_CENTER);
    DrawTxt(scr, small_font, T("cell.spell_modal_hint1"),
        x + 16, y + 72, w - 32, 20,
        (INT32)eTextColor::LIGHT_GREEN, eTextAlignment::MIDDLE_CENTER);
    DrawTxt(scr, small_font, T("cell.spell_modal_hint2"),
        x + 16, y + 94, w - 32, 18,
        (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_CENTER);

    int bx = 0, by = 0, bw = 0, bh = 0;
    GetSpellKeyModalCancelRect_(&bx, &by, &bw, &bh);
    Fill(scr, bx, by, bw, bh, 74, 50, 27);
    scr->DrawFrame(bx, by, bw, bh, (BYTE)196, (BYTE)154, (BYTE)68);
    DrawTxt(scr, small_font, T("cell.spell_modal_cancel"),
        bx, by, bw, bh, (INT32)eTextColor::WHITE,
        eTextAlignment::MIDDLE_CENTER);
}

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
    DrawTxt(scr, GetPanelFont(), PanelTitle_(),
        px + 20, py + 14, PANEL_W - 40, 36,
        COL_TITLE_TEXT, eTextAlignment::MIDDLE_CENTER);
    DrawHelpButton_(scr);
    DrawStoreButton_(scr, LOAD_BTN_X, 4, T("panel.load"));
    DrawStoreButton_(scr, SAVE_BTN_X, 5, T("panel.save"));
    DrawProtectStrategyRow_(scr);
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

    // 网格金框：框住 3 行格子 + 右侧滚动条（仅金色边框，内部青色键透明）。
    // 必须在展开下拉之前绘制，否则下拉区域会被金框边线盖住。
    {
        H3LoadedPcx16* gridFrame = LoadPanelGridFrame_();
        if (gridFrame)
            DrawTransparentPcx_(gridFrame, scr, GRID_FRAME_X, GRID_FRAME_Y);
    }

    // 最后一趟：展开的下拉项，覆盖在格子/滚动条/金框之上（层级最高）。
    for (int i = 0; i < CELL_COUNT; ++i) {
        const int item_index = first_item + i;
        if (item_index >= s_p.count) break;
        CellControl* ctrl = &s_p.cells[i];
        if (!ctrl->buffer || !ctrl->buffer->buffer) continue;
        if (ctrl->expanded == CEX_NONE) continue;
        RECT cRc = CellRect(i);
        const int h_idx = (s_p.hover_cell == i) ? s_p.hover_idx : -1;
        CellControl_DrawExpandedTo(ctrl, scr, cRc.left, cRc.top, h_idx);
        RECT dropRc = {};
        if (CellControl_GetExpandRectForCtrl(ctrl, cRc.left, cRc.top, &dropRc)
            && dropRc.bottom > max_redraw_bottom)
            max_redraw_bottom = dropRc.bottom;
    }

    DrawPanelButtons_(scr);

    // 状态栏：tips 与结果文字共用 SetStatusText_（富文本 + 统一延时消失）。
    // 语义：任何新调用立即覆盖旧文本并重新计时。tips 只在光标位置变化时
    // 调用（点存/读档后光标不动 → 结果文字稳定显示到到期，不再被按钮 tip
    // 每帧刷新盖掉）；光标静止悬停在同一目标上时仅对 tip 续期不覆盖。
    // 帮助模态打开期间面板 tips 整体停用（模态盖住的控件不该再穿透提示）。
    {
        static int s_tip_last_cx = -1, s_tip_last_cy = -1;
        const H3POINT cursor = H3POINT::GetCursorPosition();
        if (!s_help_modal_open
            && (cursor.x != s_tip_last_cx || cursor.y != s_tip_last_cy)) {
            s_tip_last_cx = cursor.x;
            s_tip_last_cy = cursor.y;
            if (const char* tip = PanelTipAt_(cursor.x - s_p.x, cursor.y - s_p.y)) {
                char rich[512];
                snprintf(rich, sizeof(rich), T("tips.color_wrap"), tip);
                SetStatusText_(rich, 3000);
                s_status_is_tip = true;
            }
        } else if (!s_help_modal_open && s_status_is_tip && s_status_text[0]
            && GetTickCount() >= s_status_until) {
            // 静止悬停：tip 到期前续期（不消失），结果消息不续期自然到期。
            s_status_until = GetTickCount() + 3000;
        }
    }

    if (s_status_text[0]) {
        if (GetTickCount() >= s_status_until)
            s_status_text[0] = 0;
        else
            DrawRichTxt(scr, GetSmallFont(), s_status_text,
                20, BTN_Y + BTN_H + 14, PANEL_W - 40, 20,
                (INT32)eTextColor::WHITE);
    }

    // 保活策略展开列表：盖住金框上缘/第一行格子，画在格子之后。
    DrawProtectDropdownList_(scr);

    // 模态层最后绘制，盖住整张设置面板。
    if (s_spell_pick_cell >= 0)
        DrawSpellKeyModal_(scr);
    if (s_help_modal_open)
        DrawHelpModal_(scr);

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

