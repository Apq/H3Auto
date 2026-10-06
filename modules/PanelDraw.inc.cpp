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
    if (!s_panel_hidden_for_pick || s_forcefield_pick) return;
    H3CombatManager* mgr = GetCombatMgr();
    if (!mgr || !mgr->CCellShdPcx) return;
    int marker_hex = s_melee_pick_phase == 2 ? s_melee_pick_stand_hex : -1;
    if (!CellControl_HexValid(marker_hex)) return;
    __try {
        // 可见路径：画到 screenPcx16 + 后缓冲。
        // 绝不能 ShadeSquare/写 drawBuffer——那会污染战场离屏缓冲，
        // 结束拾取后的 Refresh 会把蓝标重新画出来。
        // 只脏 screen 层；结束时完整 Refresh 从干净 drawBuffer 重建即可撤销。
        H3CombatSquare& sq = mgr->squares[marker_hex];
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
        if (!s_forcefield_pick && s_marker_log_hex != marker_hex) {
            s_marker_log_hex = marker_hex;
            LogDebug("[Panel] melee marker draw hex=%d rel=(%d,%d) abs=(%d,%d) dlg=(%d,%d) screen=%d blt=%d",
                s_melee_pick_stand_hex, (int)sq.left, (int)sq.top,
                abs_x, abs_y, dlg_x, dlg_y, screen_ok ? 1 : 0, blitted ? 1 : 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogError("[Panel] pick marker draw exception hex=%d", marker_hex);
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
    const int h = 440; // 原 336 + 日志级别行 + 打包日志按钮行 + 召唤说明行
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
    if (out_y) *out_y = y + 50 + 9 * 28 + 6; // 9 行说明之后
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

// 开源地址行（纯文本样式，点击复制）：打包日志按钮下方、关闭按钮上方。
static void GetHelpLinkRect_(int* out_x, int* out_y, int* out_w, int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;
    GetHelpModalRect_(&x, &y, &w, &h);
    int ry = 0;
    GetHelpLogLevelRowY_(&ry);
    if (out_x) *out_x = x + 18;
    if (out_y) *out_y = ry + HELP_DD_H + 10 + HELP_PACK_BTN_H + 4;
    if (out_w) *out_w = w - 36;
    if (out_h) *out_h = 18;
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

// 本场存档下拉按钮（原「读档」「存档」合并区）：与保活/召唤下拉同款
// 下拉样式（底色 74,52,24 / 展开 104,70,28，金框 210,170,72，右侧像素
// 三角箭头收起▼/展开▲，文字金色左对齐）。标签 = 选中档时间 /
// 「本场存档」/「无存档」。pressed_button：4。
static void DrawBattleDropdownButton_(H3LoadedPcx16* destination)
{
    if (!destination) return;
    const bool pressed = s_p.pressed_button == 4;
    Fill(destination, BATTLE_DD_X, BATTLE_DD_Y, BATTLE_DD_W, BATTLE_DD_H,
        (s_battle_dd_open || pressed) ? 104 : 74,
        (s_battle_dd_open || pressed) ? 70 : 52,
        (s_battle_dd_open || pressed) ? 28 : 24);
    destination->DrawFrame(BATTLE_DD_X, BATTLE_DD_Y, BATTLE_DD_W, BATTLE_DD_H,
        (BYTE)210, (BYTE)170, (BYTE)72);
    char label[48] = {};
    if (s_battle_record_count <= 0) {
        strncpy(label, T("panel.battle_dd_none"), sizeof(label) - 1);
    } else {
        const BattleStoreRecord* sel = BattleRecordAt_(s_battle_dd_sel);
        // 与列表行同格式「时间 P方案号」，收起时也看得到是哪套方案。
        if (sel)
            _snprintf(label, sizeof(label), "%s  P%d", sel->time, sel->active + 1);
        else
            strncpy(label, T("panel.battle_dd"), sizeof(label) - 1);
    }
    label[sizeof(label) - 1] = 0;
    DrawTxt(destination, GetSmallFont(), label,
        BATTLE_DD_X + 6, BATTLE_DD_Y, BATTLE_DD_W - 20, BATTLE_DD_H,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
    // 金色像素三角箭头（收起▼/展开▲），与卡片/保活下拉一致。
    CellControl_DrawArrow(destination, BATTLE_DD_X + BATTLE_DD_W - 14,
        BATTLE_DD_Y + BATTLE_DD_H / 2 - 2, !s_battle_dd_open);
}

// 本场存档下拉列表浮层：与保活/卡片下拉同款逐项底色+边框（暖色主题：
// 悬停 184,136,48 / 选中 136,88,24 / 普通 68,42,18），时间降序（0=最新），
// 选中项金色文字 + 底色高亮，行「时间 P方案N」。展开时按钮底色同高亮。
static void DrawBattleDropdownList_(H3LoadedPcx16* scr)
{
    if (!scr || !s_battle_dd_open || s_battle_record_count <= 0) return;
    const int list_h = s_battle_record_count * BATTLE_DD_ITEM_H;
    // 列表底：与保活列表同款深底 + 双层金框。
    Fill(scr, BATTLE_DD_LIST_X, BATTLE_DD_LIST_Y, BATTLE_DD_LIST_W, list_h,
        48, 32, 18);
    scr->DrawFrame(BATTLE_DD_LIST_X, BATTLE_DD_LIST_Y, BATTLE_DD_LIST_W,
        list_h, (BYTE)232, (BYTE)196, (BYTE)96);
    scr->DrawFrame(BATTLE_DD_LIST_X + 1, BATTLE_DD_LIST_Y + 1,
        BATTLE_DD_LIST_W - 2, list_h - 2, (BYTE)112, (BYTE)82, (BYTE)36);
    char line[40];
    for (int i = 0; i < s_battle_record_count; ++i) {
        const BattleStoreRecord* rec = BattleRecordAt_(i);
        if (!rec) continue;
        const int iy = BATTLE_DD_LIST_Y + i * BATTLE_DD_ITEM_H;
        // 每项独立底色+边框（同卡片/保活下拉暖色主题）。
        BYTE bg_r, bg_g, bg_b, fr, fg, fb;
        if (i == s_battle_dd_hover) {
            bg_r = 184; bg_g = 136; bg_b = 48;
            fr = 246; fg = 214; fb = 116;
        } else if (i == s_battle_dd_sel) {
            bg_r = 136; bg_g = 88; bg_b = 24;
            fr = 232; fg = 184; fb = 76;
        } else {
            bg_r = 68; bg_g = 42; bg_b = 18;
            fr = 166; fg = 112; fb = 40;
        }
        Fill(scr, BATTLE_DD_LIST_X + 2, iy + 1, BATTLE_DD_LIST_W - 4,
            BATTLE_DD_ITEM_H - 2, bg_r, bg_g, bg_b);
        scr->DrawFrame(BATTLE_DD_LIST_X + 2, iy + 1, BATTLE_DD_LIST_W - 4,
            BATTLE_DD_ITEM_H - 2, fr, fg, fb);
        _snprintf(line, sizeof(line), "%s  P%d", rec->time, rec->active + 1);
        line[sizeof(line) - 1] = 0;
        DrawTxt(scr, GetSmallFont(), line,
            BATTLE_DD_LIST_X + 6, iy, BATTLE_DD_LIST_W - 12, BATTLE_DD_ITEM_H,
            i == s_battle_dd_sel ? (INT32)eTextColor::GOLD
                                 : (INT32)eTextColor::YELLOW,
            eTextAlignment::MIDDLE_LEFT);
    }
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
        T("help.line_summon"),
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
    // 行 C：开源地址（纯文本样式，不做按钮框；点击复制到剪贴板）。
    // 游戏只有 bigfont/smalfont 两档字体，用 smalfont + 紧凑行高 + 灰色
    // 富文本呈现「小一号」的低调观感。复制成功后 1.2 秒内整行变绿。
    {
        int lx = 0, ly = 0, lw = 0, lh = 0;
        GetHelpLinkRect_(&lx, &ly, &lw, &lh);
        const DWORD dt = s_help_link_flash_tick
            ? GetTickCount() - s_help_link_flash_tick : 0xFFFFFFFF;
        DrawRichTxt(scr, small_font,
            T(dt < 1200 ? "help.link_flash" : "help.link"),
            lx, ly, lw, lh, (INT32)eTextColor::GRAY);
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
    // 左侧 Tab 导航条：两页共通。
    for (int i = 0; i < TAB_VISIBLE_COUNT; ++i) {
        const int page = kTabOrder[i];
        const int ty = TAB_FIRST_Y + i * (TAB_ITEM_H + TAB_GAP);
        if (px >= TAB_X && px < TAB_X + TAB_ITEM_W
            && py >= ty && py < ty + TAB_ITEM_H)
            return T(page == PAGE_ARMY ? "tips.tab_army"
                : page == PAGE_SUMMON ? "tips.tab_summon"
                : page == PAGE_STATUS ? "tips.tab_status" : "tips.tab_tactics");
    }
    // 非部队页：只有方案级控件与共通按钮的 tip。
    if (s_p.active_page != PAGE_ARMY) {
        struct P { int x, y, w, h; const char* k; };
        static const P kPageTips[] = {
            { BATTLE_DD_X, BATTLE_DD_Y, BATTLE_DD_W, BATTLE_DD_H, "tips.btn_battle_dd" },
            { PROFILE_BTN_X, PROFILE_BTN_Y, PROFILE_BTNS_W, PROFILE_BTN_H, "tips.btn_profile" },
            { STOP_LABEL_X, STOP_ROW_Y - 4, STOP_LABEL_W + STOP_BOX_W + 8, 30, "tips.stop_row" },
            { STOP_MANA_X - 4, STOP_ROW_Y - 4,
              STOP_MANA_HIT_W + 8, 30, "tips.stop_mana" },
            { STOP_MANA_TH_BOX_X, STOP_ROW_Y - 4,
              STOP_MANA_TH_BOX_W + 2 + STOP_MANA_TAIL_W, 30,
              "tips.stop_mana_th" },
            { OK_X, BTN_Y, BTN_W, BTN_H, "tips.btn_ok" },
            { CANCEL_X, BTN_Y, BTN_W, BTN_H, "tips.btn_cancel" },
        };
        // 召唤页专属控件 tips（法术/行动按当前选中项动态取键）。
        if (s_p.active_page == PAGE_SUMMON) {
            const SummonProfileFields& sf =
                s_p.draft_summon[s_p.selected_profile];
            if (px >= SUMMON_ENABLE_X
                && px < SUMMON_ENABLE_X + SUMMON_ENABLE_HIT_W
                && py >= SUMMON_ENABLE_Y - 2
                && py < SUMMON_ENABLE_Y + SUMMON_ENABLE_H + 2)
                return T("tips.summon_enable");
            if (px >= SUMMON_DD_X && px < SUMMON_DD_X + SUMMON_DD_W
                && py >= SUMMON_SPELL_Y - 2
                && py < SUMMON_SPELL_Y + SUMMON_DD_H + 2) {
                char key[40] = {};
                const int cur = (sf.spell_pick >= 0 && sf.spell_pick <= 4)
                    ? sf.spell_pick : 0;
                _snprintf(key, sizeof(key) - 1, "tips.summon_spell_opt%d", cur);
                return T(key);
            }
            if (px >= SUMMON_CNT_LABEL_X
                && px < SUMMON_CNT_BOX_X + SUMMON_CNT_BOX_W
                && py >= SUMMON_ROW1_Y - 2
                && py < SUMMON_ROW1_Y + SUMMON_DD_H + 2)
                return T("tips.summon_count");
            if (px >= SUMMON_HP_LABEL_X
                && px < SUMMON_HP_BOX_X + SUMMON_HP_BOX_W
                && py >= SUMMON_ROW2_Y - 2
                && py < SUMMON_ROW2_Y + SUMMON_DD_H + 2)
                return T("tips.summon_hp");
            if (px >= SUMMON_CB_DD_X
                && px < SUMMON_CB_DD_X + SUMMON_CB_DD_W
                && py >= SUMMON_ROW2_Y - 2
                && py < SUMMON_ROW2_Y + SUMMON_DD_H + 2)
                return T("tips.summon_cond");
            if (px >= SUMMON_ACT_LABEL_X
                && px < SUMMON_ACT_DD_X + SUMMON_ACT_DD_W
                && py >= SUMMON_ACT_DD_Y - 2
                && py < SUMMON_ACT_DD_Y + SUMMON_ACT_DD_H + 2) {
                const int act = (int)sf.summon_rule.action;
                if (act >= 0 && act < AA_COUNT) {
                    char key[32] = {};
                    _snprintf(key, sizeof(key) - 1, "tips.action_opt%d", act);
                    return T(key);
                }
                return T("tips.summon_action");
            }
            if (sf.summon_rule.action == AA_RANDOM_MOVE
                && px >= SUMMON_FB_X
                && px < SUMMON_FB_X + 10 + 4 + SUMMON_FB_TEXT_W
                && py >= SUMMON_FB_Y - 2 && py < SUMMON_FB_Y + 14)
                return T("tips.summon_fallback");
            if (px >= GRID_FRAME_X && px < GRID_FRAME_X + GRID_FRAME_W
                && py >= SUMMON_NOTE_Y && py < SUMMON_NOTE_Y + SUMMON_NOTE_H)
                return T("panel.summon_note");
        }
        if (s_p.active_page == PAGE_STATUS
            && PointInRect_(px, py, GRID_FRAME_X, FF_ROW_Y,
                FF_LABEL_W + 6 + 2 * (FF_SLOT_W + FF_SLOT_GAP), FF_SLOT_H))
            return T("tips.forcefield_pick");
        // 保持状态页专属控件 tips（阈值行）。
        if (s_p.active_page == PAGE_STATUS
            && px >= GRID_FRAME_X
            && px < STATUS_TH_TAIL_X + STATUS_TH_TAIL_W
            && py >= STATUS_TH_ROW_Y - 2
            && py < STATUS_TH_ROW_Y + STATUS_TH_ROW_H + 2)
            return T("tips.status_th");
        for (const P& t : kPageTips) {
            if (px >= t.x && px < t.x + t.w && py >= t.y && py < t.y + t.h)
                return T(t.k);
        }
        return nullptr;
    }
    const int first_item = s_p.scroll_row * COLS;
    for (int i = 0; i < CELL_COUNT; ++i) {
        if (first_item + i >= s_p.count) break;
        RECT cRc = CellRect(i);
        if (px < cRc.left || px >= cRc.right || py < cRc.top || py >= cRc.bottom)
            continue;
        CellControl* ctrl = &s_p.cells[i];
        if (!ctrl->has_data) continue;
        const CellHitArea hit = CellControl_HitTestInCell(ctrl, px - cRc.left, py - cRc.top);
        const int hov = (s_p.hover_cell == i) ? s_p.hover_idx : -1;
        if (const char* tip = CellControl_TipForHit(hit, ctrl, hov))
            return tip;
        // 标签「行动前循环快捷施法:」本身不在槽位命中区内，整行都给施法说明。
        if (py >= cRc.top + CC_SPELL_Y && py < cRc.top + CC_SPELL_Y + CC_ROW_H
            && px >= cRc.left + CC_COL2_X && px < cRc.left + CC_COL3_RIGHT)
            return CellControl_TipForHit(
                static_cast<CellHitArea>(CELL_HIT_SPELL_BASE), ctrl);
    }
    struct TipRect { int x, y, w, h; const char* text; };
    static const TipRect kTips[] = {
        { BATTLE_DD_X, BATTLE_DD_Y, BATTLE_DD_W, BATTLE_DD_H, nullptr },
        { PROFILE_BTN_X, PROFILE_BTN_Y, PROFILE_BTNS_W, PROFILE_BTN_H, nullptr },
        { OK_X, BTN_Y, BTN_W, BTN_H, nullptr },
        { CANCEL_X, BTN_Y, BTN_W, BTN_H, nullptr },
    };
    static const char* const kTipKeys[] = {
        "tips.btn_battle_dd", "tips.btn_profile",
        "tips.btn_ok", "tips.btn_cancel",
    };
    for (int i = 0; i < (int)(sizeof(kTips) / sizeof(kTips[0])); ++i) {
        const TipRect& t = kTips[i];
        if (px >= t.x && px < t.x + t.w && py >= t.y && py < t.y + t.h)
            return T(kTipKeys[i]);
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

// 多级导航：左侧 Tab 条 + 金框色分隔竖线（方案行以下、确定/取消以上）。
// 选中项亮底金框金字，未选暗底暗框（与卡片按钮暖色体系一致）。
static void DrawTabBar_(H3LoadedPcx16* scr)
{
    if (!scr) return;
    static const char* const kTabKeys[TAB_VISIBLE_COUNT] = {
        "panel.tab_army",
        "panel.tab_summon",
        "panel.tab_status",
    };
    H3Font* small_font = GetSmallFont();
    for (int i = 0; i < TAB_VISIBLE_COUNT; ++i) {
        const int page = kTabOrder[i];
        const int x = TAB_X;
        const int y = TAB_FIRST_Y + i * (TAB_ITEM_H + TAB_GAP);
        const bool sel = (page == s_p.active_page);
        Fill(scr, x, y, TAB_ITEM_W, TAB_ITEM_H,
            sel ? 136 : 74, sel ? 88 : 52, 24);
        scr->DrawFrame(x, y, TAB_ITEM_W, TAB_ITEM_H,
            (BYTE)(sel ? 210 : 112), (BYTE)(sel ? 170 : 82),
            (BYTE)(sel ? 72 : 36));
        DrawTxt(scr, small_font, T(kTabKeys[page]),
            x, y, TAB_ITEM_W, TAB_ITEM_H,
            (INT32)(sel ? eTextColor::GOLD : eTextColor::REGULAR),
            eTextAlignment::MIDDLE_CENTER);
    }
    // 上下两条横向金线 + Tab/内容区分隔竖线（均 2px 金框色 168,141,68）。
    // 横线左右端与竖线上下端相接，围出 Tab 条与内容区。
    Fill(scr, TAB_HLINE_X0, TAB_HLINE_Y, TAB_HLINE_X1 - TAB_HLINE_X0, 2,
        168, 141, 68);
    Fill(scr, TAB_HLINE_X0, TAB_HLINE2_Y, TAB_HLINE_X1 - TAB_HLINE_X0, 2,
        168, 141, 68);
    Fill(scr, TAB_SEP_X, TAB_SEP_Y0, 2, TAB_HLINE2_Y + 2 - TAB_SEP_Y0,
        168, 141, 68);
    // 状态栏分隔线（原图横线已抹除，改代码绘制；位置=原版+20px）。
    Fill(scr, TAB_HLINE_X0, STATUS_SEP_Y, TAB_HLINE_X1 - TAB_HLINE_X0, 2,
        168, 141, 68);
}

// 自动停止行：保活与召唤同一条施法通道（保活优先、召唤兜底）后，
// 行内只剩「停止: [框] 回合」+「[✓] 敌方魔力≤[框] 时停」组合，不再有
// 方案级「保活」下拉；召唤启用在召唤页复选框。
static void DrawStopRow_(H3LoadedPcx16* scr)
{
    if (!scr) return;
    H3Font* small_font = GetSmallFont();

    // 停止条件组金框（1px）：先画外框再画控件，提示「停止 N 回合」与
    // 「敌方魔力≤阈值时停」是一起生效的一组（先画框会被控件底色盖掉）。
    scr->DrawFrame(STOP_GROUP_X, STOP_GROUP_Y, STOP_GROUP_W, STOP_GROUP_H,
        (BYTE)210, (BYTE)170, (BYTE)72);

    DrawTxt(scr, small_font, T("panel.stop_label"),
        STOP_LABEL_X, STOP_ROW_Y, STOP_LABEL_W, STOP_ROW_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_RIGHT);
    char num[8] = {};
    if (s_stop_turns_editing)
        _snprintf(num, sizeof(num), "%s", s_stop_turns_text);
    else
        _snprintf(num, sizeof(num), "%d",
            (int)s_p.draft_stop_turns[s_p.selected_profile]);
    Fill(scr, STOP_BOX_X, STOP_ROW_Y, STOP_BOX_W, STOP_ROW_H,
        s_stop_turns_editing ? 104 : 74,
        s_stop_turns_editing ? 70 : 52,
        s_stop_turns_editing ? 28 : 24);
    scr->DrawFrame(STOP_BOX_X, STOP_ROW_Y, STOP_BOX_W, STOP_ROW_H,
        (BYTE)210, (BYTE)170, (BYTE)72);
    if (s_stop_turns_editing) {
        // 编辑态：左对齐绘制 + 500ms 闪烁光标（BltComplete 每帧重绘面板，
        // 按键/移动重置基准，保证输入后光标立即可见）。
        const int text_x = STOP_BOX_X + 8;
        DrawTxt(scr, small_font, num, text_x, STOP_ROW_Y,
            STOP_BOX_W - 12, STOP_ROW_H,
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
                STOP_ROW_Y + (STOP_ROW_H - 10) / 2, 2, 10,
                210, 170, 72); // 金色竖线光标
        }
    } else {
        DrawTxt(scr, small_font, num[0] ? num : "0",
            STOP_BOX_X, STOP_ROW_Y, STOP_BOX_W, STOP_ROW_H,
            (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
    }

    // 「[✓] 敌方魔力≤[框] 时停」组合（停止行最右，两页共通；复选框在文字前，
    // 阈值框与停止框同款录入；勾选才生效，默认 6，0..32767）。
    {
        const SummonProfileFields& sf =
            s_p.draft_summon[s_p.selected_profile];
        DrawCheckbox_(scr, STOP_MANA_X,
            STOP_ROW_Y + (STOP_ROW_H - 10) / 2,
            sf.stop_enemy_mana != 0,
            T("panel.stop_mana_label"), STOP_MANA_TEXT_W, 10);
        char mnum[8] = {};
        if (s_mana_th_editing)
            _snprintf(mnum, sizeof(mnum), "%s", s_mana_th_text);
        else
            _snprintf(mnum, sizeof(mnum), "%d", sf.stop_mana_th);
        Fill(scr, STOP_MANA_TH_BOX_X, STOP_ROW_Y,
            STOP_MANA_TH_BOX_W, STOP_ROW_H,
            s_mana_th_editing ? 104 : 74,
            s_mana_th_editing ? 70 : 52,
            s_mana_th_editing ? 28 : 24);
        scr->DrawFrame(STOP_MANA_TH_BOX_X, STOP_ROW_Y,
            STOP_MANA_TH_BOX_W, STOP_ROW_H, (BYTE)210, (BYTE)170, (BYTE)72);
        if (s_mana_th_editing) {
            const int text_x = STOP_MANA_TH_BOX_X + 8;
            DrawTxt(scr, small_font, mnum, text_x, STOP_ROW_Y,
                STOP_MANA_TH_BOX_W - 12, STOP_ROW_H,
                (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
            const bool mcaret_on =
                ((GetTickCount() - s_mana_th_caret_tick) / 500) % 2 == 0;
            if (mcaret_on) {
                char mprefix[8] = {};
                const int mcaret = s_mana_th_caret;
                if (mcaret > 0)
                    memcpy(mprefix, mnum, mcaret < 7 ? mcaret : 7);
                const INT32 mprefix_w =
                    small_font ? small_font->GetMaxLineWidth(mprefix) : 0;
                Fill(scr, text_x + mprefix_w,
                    STOP_ROW_Y + (STOP_ROW_H - 10) / 2, 2, 10,
                    210, 170, 72);
            }
        } else {
            DrawTxt(scr, small_font, mnum, STOP_MANA_TH_BOX_X, STOP_ROW_Y,
                STOP_MANA_TH_BOX_W, STOP_ROW_H,
                (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
        }
        DrawTxt(scr, small_font, T("panel.stop_mana_tail"),
            STOP_MANA_TAIL_X, STOP_ROW_Y, STOP_MANA_TAIL_W, STOP_ROW_H,
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

// ===== 召唤页（PAGE_SUMMON）：设置行 + 说明行 + 召唤物行动卡 =====

static void GetSummonSpellDdItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h)
{
    if (out_x) *out_x = SUMMON_DD_X;
    if (out_y) *out_y = SUMMON_SPELL_Y + SUMMON_DD_H + item * SUMMON_DD_ITEM_H;
    if (out_w) *out_w = SUMMON_DD_W;
    if (out_h) *out_h = SUMMON_DD_ITEM_H;
}

// 条件组合下拉列表项矩形（和/或，两项）。
static void GetSummonCondDdItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h)
{
    if (out_x) *out_x = SUMMON_CB_DD_X;
    if (out_y) *out_y = SUMMON_ROW2_Y + SUMMON_DD_H
        + item * SUMMON_CB_DD_ITEM_H;
    if (out_w) *out_w = SUMMON_CB_DD_W;
    if (out_h) *out_h = SUMMON_CB_DD_ITEM_H;
}

static void GetSummonActDdItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h)
{
    if (out_x) *out_x = SUMMON_ACT_DD_X;
    if (out_y) *out_y = SUMMON_ACT_DD_Y + SUMMON_ACT_DD_H
        + item * SUMMON_ACT_DD_ITEM_H;
    if (out_w) *out_w = SUMMON_ACT_DD_W;
    if (out_h) *out_h = SUMMON_ACT_DD_ITEM_H;
}

// 阈值数字框（收起=居中显示草稿值；编辑=左对齐+光标，与停止回合框同款）。
static void DrawSummonNumBox_(H3LoadedPcx16* scr, H3Font* small_font,
    int which, int current_value)
{
    int box_x = 0, box_y = 0, box_w = 0, box_h = 0;
    SummonNumBoxRect_(which, &box_x, &box_y, &box_w, &box_h);
    const bool editing = (s_summon_edit_which == which);
    char num[16] = {};
    if (editing)
        _snprintf(num, sizeof(num), "%s", s_summon_edit_text);
    else
        _snprintf(num, sizeof(num), "%d", current_value);
    Fill(scr, box_x, box_y, box_w, box_h,
        editing ? 104 : 74, editing ? 70 : 52, editing ? 28 : 24);
    scr->DrawFrame(box_x, box_y, box_w, box_h, (BYTE)210, (BYTE)170, (BYTE)72);
    if (editing) {
        const int text_x = SummonNumBoxTextX_(which);
        DrawTxt(scr, small_font, num, text_x, box_y, box_w - 12, box_h,
            (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
        const bool caret_on =
            ((GetTickCount() - s_summon_edit_caret_tick) / 500) % 2 == 0;
        if (caret_on) {
            char prefix[16] = {};
            const int caret = s_summon_edit_caret;
            const int cap = (int)sizeof(prefix) - 1;
            if (caret > 0) memcpy(prefix, num, caret < cap ? caret : cap);
            const INT32 prefix_w =
                small_font ? small_font->GetMaxLineWidth(prefix) : 0;
            Fill(scr, text_x + prefix_w, box_y + (box_h - 10) / 2, 2, 10,
                210, 170, 72); // 金色竖线光标
        }
    } else {
        DrawTxt(scr, small_font, num[0] ? num : "0",
            box_x, box_y, box_w, box_h,
            (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
    }
}

// 通用小下拉框（收起态）：深棕底金框 + 当前项文字 + 三角箭头。
static void DrawSummonCombo_(H3LoadedPcx16* scr, H3Font* small_font,
    int x, int y, int w, int h, const char* text, bool open)
{
    Fill(scr, x, y, w, h, open ? 104 : 74, open ? 70 : 52, open ? 28 : 24);
    scr->DrawFrame(x, y, w, h, (BYTE)210, (BYTE)170, (BYTE)72);
    DrawTxt(scr, small_font, text, x + 6, y, w - 20, h,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
    CellControl_DrawArrow(scr, x + w - 14, y + h / 2 - 2, !open);
}

static void DrawForceFieldRow_(H3LoadedPcx16* scr)
{
    H3Font* font = GetSmallFont();
    const ForceFieldProfileFields& ff = s_p.draft_forcefield[s_p.selected_profile];
    DrawTxt(scr, font, T("panel.forcefield_label"),
        GRID_FRAME_X, FF_ROW_Y, FF_LABEL_W, FF_SLOT_H,
        (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
    for (int i = 0; i < 2; ++i) {
        const bool existing = ff.anchor_hex[i] >= 0;
        if (!existing && i > 0 && ff.anchor_hex[i - 1] < 0) break;
        const int x = FF_SLOT_X + i * (FF_SLOT_W + FF_SLOT_GAP);
        CellControl_DrawButtonBg(scr, x, FF_ROW_Y, FF_SLOT_W, FF_SLOT_H, false, false);
        if (existing) {
            char label[12] = {};
            CellControl_FormatPosition(label, sizeof(label), ff.anchor_hex[i]);
            DrawTxt(scr, font, label, x + 1, FF_ROW_Y, FF_SLOT_W - 2, FF_SLOT_H,
                (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
        } else {
            CellControl_DrawPlusButton(scr, x + (FF_SLOT_W - 16) / 2,
                FF_ROW_Y + (FF_SLOT_H - 16) / 2, 16, false);
        }
    }
}

static void DrawSummonPage_(H3LoadedPcx16* scr)
{
    if (!scr) return;
    H3Font* small_font = GetSmallFont();
    const SummonProfileFields& sf = s_p.draft_summon[s_p.selected_profile];

    // 启用勾选：与「停止:」同一行（左端），勾选后保活无人可救时自动召唤兜底。
    DrawCheckbox_(scr, SUMMON_ENABLE_X,
        SUMMON_ENABLE_Y + (SUMMON_ENABLE_H - 10) / 2,
        sf.enabled != 0,
        T("panel.summon_enable"), SUMMON_ENABLE_TEXT_W, 10);

    // 魔法单独一行；两个条件另成一组，用 1px 金框包住。
    DrawTxt(scr, small_font, T("panel.summon_spell_label"),
        SUMMON_DD_LABEL_X, SUMMON_SPELL_Y, SUMMON_DD_LABEL_W, SUMMON_DD_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
    DrawSummonCombo_(scr, small_font, SUMMON_DD_X, SUMMON_SPELL_Y,
        SUMMON_DD_W, SUMMON_DD_H,
        (sf.spell_pick >= 0 && sf.spell_pick <= 4)
            ? SummonSpellOptLabel_(sf.spell_pick) : "?",
        s_summon_spell_dd_open);
    // 条件组金框：左缘与上下下拉对齐；框左写「条件」。
    DrawTxt(scr, small_font, T("panel.summon_cond_label"),
        SUMMON_COND_LABEL_X, SUMMON_COND_GROUP_Y,
        SUMMON_COND_LABEL_W, SUMMON_COND_GROUP_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
    scr->DrawFrame(SUMMON_COND_GROUP_X, SUMMON_COND_GROUP_Y,
        SUMMON_COND_GROUP_W, SUMMON_COND_GROUP_H,
        (BYTE)210, (BYTE)170, (BYTE)72);
    DrawTxt(scr, small_font, T("panel.summon_count_label"),
        SUMMON_CNT_LABEL_X, SUMMON_ROW1_Y, SUMMON_CNT_LABEL_W, SUMMON_DD_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
    DrawSummonNumBox_(scr, small_font, SUMMON_EDIT_COUNT, sf.count_th);
    DrawSummonCombo_(scr, small_font, SUMMON_CB_DD_X, SUMMON_ROW2_Y,
        SUMMON_CB_DD_W, SUMMON_DD_H,
        SummonCondOptLabel_(sf.cond_combine), s_summon_cond_dd_open);
    DrawTxt(scr, small_font, T("panel.summon_hp_label"),
        SUMMON_HP_LABEL_X, SUMMON_ROW2_Y, SUMMON_HP_LABEL_W, SUMMON_DD_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
    DrawSummonNumBox_(scr, small_font, SUMMON_EDIT_HP, sf.hp_th);

    // 说明行：时机与口径（灰白小字）。
    DrawTxt(scr, small_font, T("panel.summon_note"),
        GRID_FRAME_X, SUMMON_NOTE_Y, GRID_FRAME_W, SUMMON_NOTE_H,
        (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);

    // 召唤物行动行（普通行，不做成卡片）：行动下拉 + 降级勾选。
    DrawTxt(scr, small_font, T("panel.summon_action_label"),
        SUMMON_ACT_LABEL_X, SUMMON_ACT_DD_Y, SUMMON_ACT_LABEL_W,
        SUMMON_ACT_DD_H,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
    DrawSummonCombo_(scr, small_font, SUMMON_ACT_DD_X, SUMMON_ACT_DD_Y,
        SUMMON_ACT_DD_W, SUMMON_ACT_DD_H,
        SummonActOptLabel_(SummonActOptFromAction_(sf.summon_rule.action)),
        s_summon_act_dd_open);
    // 「允许降级为防御」复选框：仅随机移动时显示（框在文字前）。
    if (sf.summon_rule.action == AA_RANDOM_MOVE) {
        DrawCheckbox_(scr, SUMMON_FB_X, SUMMON_FB_Y,
            sf.summon_rule.allowDefendFallback != 0,
            T("cell.allow_fallback"), SUMMON_FB_TEXT_W, 10);
    }
}

static const char* StatusSpellName_(int spell_id, char* out, int out_size)
{
    if (!out || out_size <= 0) return "";
    out[0] = 0;
    if (spell_id <= 0 || spell_id >= 81) {
        _snprintf(out, out_size, "%s", T("panel.status_empty"));
        return out;
    }
    const BYTE* table = *reinterpret_cast<BYTE**>(0x687FA8);
    if (!table) {
        _snprintf(out, out_size, "%s", T("panel.status_empty"));
        return out;
    }
    const char* name = *reinterpret_cast<const char* const*>(
        table + spell_id * 0x88 + 0x10);
    if (!name || !name[0]) {
        _snprintf(out, out_size, "%s", T("panel.status_empty"));
        return out;
    }
    // 游戏法术表名称是系统 ANSI/GBK，面板 DrawTxt 接受 UTF-8，不能直接透传。
    wchar_t wide[128] = {};
    const int wide_len = MultiByteToWideChar(936, 0, name, -1,
        wide, _countof(wide));
    if (wide_len > 0 && WideCharToMultiByte(CP_UTF8, 0, wide, -1,
            out, out_size, nullptr, nullptr) > 0)
        return out;
    _snprintf(out, out_size, "%s", T("panel.status_empty"));
    return out;
}

static void StatusSlotRect_(int index, int* x, int* y)
{
    const int col = index % STATUS_COLS;
    const int row = index / STATUS_COLS;
    if (x) *x = GRID_FRAME_X + col * (STATUS_DD_W + STATUS_GAP);
    if (y) *y = STATUS_ROW0_Y + row * STATUS_ROW_H;
}

static const char* StatusSpellLabel_(int spell_id, char* buf, int cap)
{
    char name[256] = {};
    StatusSpellName_(spell_id, name, sizeof(name));
    if (spell_id == H3AutoPolicy::kSlowSpellId)
        _snprintf(buf, cap, "%s（敌方）", name);
    else
        _snprintf(buf, cap, "%s", name);
    if (cap > 0) buf[cap - 1] = 0;
    return buf;
}

static void DrawStatusPage_(H3LoadedPcx16* scr)
{
    if (!scr) return;
    H3Font* small_font = GetSmallFont();
    DrawForceFieldRow_(scr);
    DrawTxt(scr, small_font, T("panel.status_note"),
        GRID_FRAME_X, STATUS_NOTE_Y, GRID_FRAME_W, 18,
        (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
    const StatusProfileFields& status =
        s_p.draft_status[s_p.selected_profile];
    for (int i = 0; i < status.slot_count; ++i) {
        int x = 0, y = 0;
        StatusSlotRect_(i, &x, &y);
        char label[96] = {};
        DrawSummonCombo_(scr, small_font, x, y, STATUS_DD_W, STATUS_DD_H,
            StatusSpellLabel_(status.slots[i], label, sizeof(label)),
            s_status_dd_open == i);
    }
    if (status.slot_count < H3AutoPolicy::kStatusSlotCapacity) {
        int x = 0, y = 0;
        StatusSlotRect_(status.slot_count, &x, &y);
        CellControl_DrawButtonBg(scr, x, y, STATUS_ADD_W, STATUS_ADD_H,
            s_p.status_add_armed, false);
        CellControl_DrawPlusButton(scr, x + (STATUS_ADD_W - 16) / 2,
            y + (STATUS_ADD_H - 16) / 2, 16, s_p.status_add_armed);
    }

    // 「剩余回合≤[框] 时补」阈值行（方案级 1..9 默认 1）：多个已配置
    // 法术都保持，任一在任一队剩余 ≤ 阈值就补它（多达标取最少者）。
    // 框与其它数字框同款（编辑态高亮底 + 左对齐 + 闪烁光标）。
    DrawTxt(scr, small_font, T("panel.status_th_label"),
        GRID_FRAME_X, STATUS_TH_ROW_Y, STATUS_TH_LABEL_W, STATUS_TH_ROW_H,
        (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
    {
        char tnum[8] = {};
        if (s_status_th_editing)
            _snprintf(tnum, sizeof(tnum), "%s", s_status_th_text);
        else
            _snprintf(tnum, sizeof(tnum), "%d",
                status.refresh_turns > 0 ? status.refresh_turns
                    : H3AutoPolicy::kStatusRefreshTurns);
        Fill(scr, STATUS_TH_BOX_X, STATUS_TH_ROW_Y,
            STATUS_TH_BOX_W, STATUS_TH_ROW_H,
            s_status_th_editing ? 104 : 74,
            s_status_th_editing ? 70 : 52,
            s_status_th_editing ? 28 : 24);
        scr->DrawFrame(STATUS_TH_BOX_X, STATUS_TH_ROW_Y,
            STATUS_TH_BOX_W, STATUS_TH_ROW_H,
            (BYTE)210, (BYTE)170, (BYTE)72);
        if (s_status_th_editing) {
            const int text_x = STATUS_TH_BOX_X + 8;
            DrawTxt(scr, small_font, tnum, text_x, STATUS_TH_ROW_Y,
                STATUS_TH_BOX_W - 12, STATUS_TH_ROW_H,
                (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
            const bool tcaret_on =
                ((GetTickCount() - s_status_th_caret_tick) / 500) % 2 == 0;
            if (tcaret_on) {
                char tprefix[8] = {};
                const int tcaret = s_status_th_caret;
                if (tcaret > 0)
                    memcpy(tprefix, tnum, tcaret < 7 ? tcaret : 7);
                const INT32 tprefix_w =
                    small_font ? small_font->GetMaxLineWidth(tprefix) : 0;
                Fill(scr, text_x + tprefix_w,
                    STATUS_TH_ROW_Y + (STATUS_TH_ROW_H - 10) / 2, 2, 10,
                    210, 170, 72); // 金色竖线光标
            }
        } else {
            DrawTxt(scr, small_font, tnum, STATUS_TH_BOX_X, STATUS_TH_ROW_Y,
                STATUS_TH_BOX_W, STATUS_TH_ROW_H,
                (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
        }
        DrawTxt(scr, small_font, T("panel.status_th_tail"),
            STATUS_TH_TAIL_X, STATUS_TH_ROW_Y, STATUS_TH_TAIL_W,
            STATUS_TH_ROW_H,
            (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
    }
}

// 展开的下拉列表（每项底色+边框，与保活/卡片下拉同主题）。
static void DrawSummonDropdownLists_(H3LoadedPcx16* scr)
{
    if (!scr) return;
    H3Font* small_font = GetSmallFont();
    if (s_summon_spell_dd_open) {
        const int current = s_p.draft_summon[s_p.selected_profile].spell_pick;
        for (int i = 0; i <= 4; ++i) {
            int ix = 0, iy = 0, iw = 0, ih = 0;
            GetSummonSpellDdItemRect_(i, &ix, &iy, &iw, &ih);
            BYTE bg_r, bg_g, bg_b, frame_r, frame_g, frame_b;
            if (i == s_summon_spell_dd_hover) {
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
            DrawTxt(scr, small_font, SummonSpellOptLabel_(i),
                ix + 6, iy, iw - 12, ih,
                (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        }
    }
    if (s_summon_cond_dd_open) {
        const int current = s_p.draft_summon[s_p.selected_profile]
            .cond_combine;
        for (int i = 0; i < 2; ++i) {
            int ix = 0, iy = 0, iw = 0, ih = 0;
            GetSummonCondDdItemRect_(i, &ix, &iy, &iw, &ih);
            BYTE bg_r, bg_g, bg_b, frame_r, frame_g, frame_b;
            if (i == s_summon_cond_dd_hover) {
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
            DrawTxt(scr, small_font, SummonCondOptLabel_(i),
                ix + 6, iy, iw - 12, ih,
                (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        }
    }
    if (s_summon_act_dd_open) {
        AutoActionKind acts[4] = {};
        const int n = H3AutoPolicy::GetAllowedSummonActions(acts);
        const int current = s_p.draft_summon[s_p.selected_profile]
            .summon_rule.action;
        for (int i = 0; i < n; ++i) {
            int ix = 0, iy = 0, iw = 0, ih = 0;
            GetSummonActDdItemRect_(i, &ix, &iy, &iw, &ih);
            BYTE bg_r, bg_g, bg_b, frame_r, frame_g, frame_b;
            if (i == s_summon_act_dd_hover) {
                bg_r = 184; bg_g = 136; bg_b = 48;
                frame_r = 246; frame_g = 214; frame_b = 116;
            } else if ((int)acts[i] == current) {
                bg_r = 136; bg_g = 88; bg_b = 24;
                frame_r = 232; frame_g = 184; frame_b = 76;
            } else {
                bg_r = 68; bg_g = 42; bg_b = 18;
                frame_r = 166; frame_g = 112; frame_b = 40;
            }
            Fill(scr, ix, iy, iw, ih, bg_r, bg_g, bg_b);
            scr->DrawFrame(ix, iy, iw, ih, frame_r, frame_g, frame_b);
            const char* label = g_action_labels[acts[i]]
                ? g_action_labels[acts[i]] : "?";
            DrawTxt(scr, small_font, label,
                ix + 6, iy, iw - 12, ih,
                (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        }
    }
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
    DrawBattleDropdownButton_(scr);
    DrawProfileButtons_(scr);
    DrawTabBar_(scr);

    // 下拉悬停高亮由 WH_MOUSE 钩子即时更新到 s_p.hover_cell/hover_idx，
    // 绘制时直接使用，不再依赖低帧率的游戏坐标。
    H3Font* fntS = GetSmallFont();
    const int first_item = s_p.scroll_row * COLS;
    int max_redraw_bottom = 0; // 记录最下方的重绘边界

    // 保活通道行：全局常显（两页共通：策略/停止/敌方法力都在行上）。
    DrawStopRow_(scr);

    if (s_p.active_page == PAGE_ARMY) {
    // 21 槽卡片表（三趟 + 滚动条 + 金框）。
    {
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

    // 表格外金框已去除（布局线已足够；卡片铺满内容区，见 CELL_W=494）。

    // 最后一趟：展开的下拉项，覆盖在格子/滚动条之上（层级最高）。
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
    } // PAGE_ARMY
    } else if (s_p.active_page == PAGE_SUMMON) {
        // 召唤页：设置行 + 说明行 + 召唤物行动卡（无滚动条）。
        DrawSummonPage_(scr);

    } else if (s_p.active_page == PAGE_STATUS) {
        DrawStatusPage_(scr);
        if (s_status_dd_open >= 0) {
            int x = 0, y = 0;
            StatusSlotRect_(s_status_dd_open, &x, &y);
            for (int i = 0; i < s_status_dd_count; ++i) {
                const int iy = y + STATUS_DD_H + i * STATUS_ITEM_H;
                BYTE bg_r = 68, bg_g = 42, bg_b = 18;
                BYTE fr = 166, fg = 112, fb = 40;
                if (i == s_status_dd_hover) {
                    bg_r = 184; bg_g = 136; bg_b = 48;
                    fr = 246; fg = 214; fb = 116;
                }
                Fill(scr, x, iy, STATUS_DD_W, STATUS_ITEM_H, bg_r, bg_g, bg_b);
                scr->DrawFrame(x, iy, STATUS_DD_W, STATUS_ITEM_H, fr, fg, fb);
                char label[96] = {};
                DrawTxt(scr, GetSmallFont(),
                    StatusSpellLabel_(s_status_dd_ids[i], label, sizeof(label)),
                    x + 6, iy, STATUS_DD_W - 12, STATUS_ITEM_H,
                    (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
            }
        }
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
                20, BTN_Y + BTN_H + 11, PANEL_W - 40, 20, // 状态栏不跟随按钮下移
                (INT32)eTextColor::WHITE);
    }

    // 召唤/本场存档展开列表（盖住表格上缘，画在最后）。
    DrawSummonDropdownLists_(scr);
    DrawBattleDropdownList_(scr);

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

