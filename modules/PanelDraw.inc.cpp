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
            // WriteLog("[Panel] melee marker draw hex=%d rel=(%d,%d) abs=(%d,%d) dlg=(%d,%d) screen=%d blt=%d",
            //     s_melee_pick_stand_hex, (int)sq.left, (int)sq.top,
            //     abs_x, abs_y, dlg_x, dlg_y, screen_ok ? 1 : 0, blitted ? 1 : 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[Panel] melee marker draw exception hex=%d", s_melee_pick_stand_hex);
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
    const int h = 336;
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
    DrawTxt(scr, title_font, "使用说明",
        x + 16, y + 12, w - 32, 26,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);

    char toggle_name[16];
    char oneshot_name[16];
    char open_name[16];
    char hotkey_line[160];
    snprintf(hotkey_line, sizeof(hotkey_line), "热键：%s 启停打铁 · %s 单次接管 · %s 打开设置",
        HotkeyDisplayName_(cfg.toggle_manual_vk, toggle_name, sizeof(toggle_name)),
        HotkeyDisplayName_(cfg.one_shot_manual_vk, oneshot_name, sizeof(oneshot_name)),
        HotkeyDisplayName_(cfg.open_settings_vk, open_name, sizeof(open_name)));

    // 打开方法单独一行：右键「自动战斗」或按配置的打开设置键（键名读配置）。
    char open_key[16];
    char open_line[160];
    snprintf(open_line, sizeof(open_line), "打开设置：右键“自动战斗”按钮，或按 %s 键",
        HotkeyDisplayName_(cfg.open_settings_vk, open_key, sizeof(open_key)));

    const char* help_lines[] = {
        open_line,
        "方案 1-5：独立草稿与存档文件，读档/存档针对选中编号",
        "施法/近战/移动：点 ＋ 后按提示设置",
        "停止：敌方预计剩余回合内全灭时交回",
        "读档/存档：仅更新界面显示，点勾号才生效",
        "删除：槽位上右键",
        hotkey_line,
        "设置有效期：同一场战斗，包括取消重打",
    };
    const int line_h = 28;
    int ty = y + 50;
    for (int i = 0; i < (int)(sizeof(help_lines) / sizeof(help_lines[0])); ++i) {
        DrawTxt(scr, small_font, help_lines[i],
            x + 18, ty, w - 36, line_h,
            (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        ty += line_h;
    }

    int bx = 0, by = 0, bw = 0, bh = 0;
    GetHelpModalCloseRect_(&bx, &by, &bw, &bh);
    Fill(scr, bx, by, bw, bh, 74, 50, 27);
    scr->DrawFrame(bx, by, bw, bh, (BYTE)196, (BYTE)154, (BYTE)68);
    DrawTxt(scr, small_font, "关闭",
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
        if (const char* tip = CellControl_TipForHit(hit))
            return tip;
        // 标签「行动前循环施法:」本身不在槽位命中区内，整行都给施法说明。
        if (py >= cRc.top + CC_SPELL_Y && py < cRc.top + CC_SPELL_Y + CC_ROW_H
            && px >= cRc.left + CC_COL2_X && px < cRc.left + CC_COL3_RIGHT)
            return CellControl_TipForHit(
                static_cast<CellHitArea>(CELL_HIT_SPELL_BASE));
    }
    if (px >= 0 && px < PANEL_W && py >= 0 && py < TITLE_H) {
        static char title_tip[192];
        char t1[16], t2[16], t3[16];
        snprintf(title_tip, sizeof(title_tip),
            "热键：%s 启停打铁 · %s 单次接管 · %s 打开设置 · 右键“自动战斗”按钮也可打开",
            HotkeyDisplayName_(cfg.toggle_manual_vk, t1, sizeof(t1)),
            HotkeyDisplayName_(cfg.one_shot_manual_vk, t2, sizeof(t2)),
            HotkeyDisplayName_(cfg.open_settings_vk, t3, sizeof(t3)));
        return title_tip;
    }
    struct TipRect { int x, y, w, h; const char* text; };
    static const TipRect kTips[] = {
        { LOAD_BTN_X, HELP_BTN_Y, STORE_BTN_W, HELP_BTN_SIZE,
          "读档：从选中编号的存档槽读入草稿（四轮部队关联）；仅更新界面，点勾号才生效" },
        { SAVE_BTN_X, HELP_BTN_Y, STORE_BTN_W, HELP_BTN_SIZE,
          "存档：把草稿写入选中编号的存档槽（其他槽不变）；不改变已生效方案" },
        { PROFILE_BTN_X, PROFILE_BTN_Y, PROFILE_BTNS_W, PROFILE_BTN_H,
          "方案 1-5：各编号独立草稿与存档文件；切换编号各自保留，读档/存档针对选中编号" },
        { 20, PROTECT_DD_Y - 4, STOP_LABEL_X - 24, 30,
          "保活策略：无 / 部队全灭后 / 回合内首动 / 损失量大于恢复量" },
        { STOP_LABEL_X, PROTECT_DD_Y - 4, STOP_LABEL_W + STOP_BOX_W + 8, 30,
          "停止：敌方预计剩余回合 ≤ 此值时切回手动；0=关闭，最大 999" },
        { OK_X, BTN_Y, BTN_W, BTN_H,
          "勾号：草稿生效并关闭面板（不写盘）；有效期同一场战斗（含取消重打）" },
        { CANCEL_X, BTN_Y, BTN_W, BTN_H,
          "取消：丢弃全部修改并关闭面板" },
    };
    for (const TipRect& t : kTips) {
        if (px >= t.x && px < t.x + t.w && py >= t.y && py < t.y + t.h)
            return t.text;
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
static void DrawProtectStrategyRow_(H3LoadedPcx16* scr)
{
    if (!scr) return;
    const int current = s_p.draft_protect_strategy[s_p.selected_profile];
    const char* text = (current >= 0 && current < (int)H3AutoPolicy::PS_COUNT)
        ? PROTECT_STRATEGY_LABELS[current] : "?";
    H3Font* small_font = GetSmallFont();

    DrawTxt(scr, small_font, "保活策略:",
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

    DrawTxt(scr, small_font, "停止:",
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
        DrawTxt(scr, small_font, PROTECT_STRATEGY_LABELS[i],
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
    DrawTxt(scr, title_font, "\xe8\xae\xbe\xe7\xbd\xae\xe5\xbf\xab\xe6\x8d\xb7\xe6\x96\xbd\xe6\xb3\x95\xe9\x94\xae", // 设置快捷施法键
        x + 16, y + 14, w - 32, 28,
        (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
    _snprintf(slot_text, sizeof(slot_text),
        "\xe6\xad\xa3\xe5\x9c\xa8\xe8\xae\xbe\xe7\xbd\xae\xe5\xbe\xaa\xe7\x8e\xaf\xe6\x96\xbd\xe6\xb3\x95\xe7\xac\xac %d \xe6\xa7\xbd", // 正在设置循环施法第 %d 槽
        s_spell_pick_slot + 1);
    DrawTxt(scr, small_font, slot_text,
        x + 16, y + 48, w - 32, 20,
        (INT32)eTextColor::WHITE, eTextAlignment::MIDDLE_CENTER);
    DrawTxt(scr, small_font, "请直接按数字键 1-9 或 0（支持小键盘）",
        x + 16, y + 72, w - 32, 20,
        (INT32)eTextColor::LIGHT_GREEN, eTextAlignment::MIDDLE_CENTER);
    DrawTxt(scr, small_font, "输入后自动保存；按 ESC 或点击取消放弃。",
        x + 16, y + 94, w - 32, 18,
        (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_CENTER);

    int bx = 0, by = 0, bw = 0, bh = 0;
    GetSpellKeyModalCancelRect_(&bx, &by, &bw, &bh);
    Fill(scr, bx, by, bw, bh, 74, 50, 27);
    scr->DrawFrame(bx, by, bw, bh, (BYTE)196, (BYTE)154, (BYTE)68);
    DrawTxt(scr, small_font, "取消",
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
    DrawTxt(scr, GetPanelFont(), g_panel_title[0] ? g_panel_title : "打铁设置",
        px + 20, py + 14, PANEL_W - 40, 36,
        COL_TITLE_TEXT, eTextAlignment::MIDDLE_CENTER);
    DrawHelpButton_(scr);
    DrawStoreButton_(scr, LOAD_BTN_X, 4, "读档");
    DrawStoreButton_(scr, SAVE_BTN_X, 5, "存档");
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

    // 方案 A tips：命中提示区即刷新文本与 3 秒保持期；移开后不刷新，
    // 到期自动消失（静止悬停也保持——每帧按光标位置判定，不依赖移动事件）。
    {
        const H3POINT cursor = H3POINT::GetCursorPosition();
        const char* tip = PanelTipAt_(cursor.x - s_p.x, cursor.y - s_p.y);
        if (tip) {
            strncpy(s_tip_text, tip, sizeof(s_tip_text) - 1);
            s_tip_text[sizeof(s_tip_text) - 1] = 0;
            s_tip_deadline = GetTickCount() + 3000;
        }
    }

    if (s_status_text[0]) {
        if (GetTickCount() >= s_status_until)
            s_status_text[0] = 0;
        else
            DrawTxt(scr, GetSmallFont(), s_status_text,
                20, BTN_Y + BTN_H + 14, PANEL_W - 40, 20,
                s_status_error ? (INT32)eTextColor::RED
                               : (INT32)eTextColor::LIGHT_GREEN,
                eTextAlignment::MIDDLE_CENTER);
    } else if (s_tip_text[0]) {
        // 提示色用金色：区别于成功（绿）/失败（红）；结果文字出现时优先。
        if (GetTickCount() >= s_tip_deadline)
            s_tip_text[0] = 0;
        else
            DrawTxt(scr, GetSmallFont(), s_tip_text,
                20, BTN_Y + BTN_H + 14, PANEL_W - 40, 20,
                (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_CENTER);
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

