// ========== SettingsDlg.inc.cpp ==========
// 战场自动化 - 设置面板
// 渲染：LoHook 0x600430 画面板到 screenPcx16
// 输入捕获：检测"自动战斗"对话框的关闭事件

#define o_WndMgr (*reinterpret_cast<H3WindowManager**>(0x6992D0))

// ========================================================================
// 第一部分：数据声明
// ========================================================================

static const int PANEL_W    = 680;
static const int PANEL_H    = 380;
static const int COLS       = 4;
static const int CELL_W     = 158;
static const int CELL_H     = 60;
static const int GAP_X      = 8;
static const int GAP_Y      = 4;
static const int MARGIN     = 20;
static const int TITLE_H    = 28;
static const int BTN_W      = 80;
static const int BTN_H      = 24;
static const int BTN_Y      = PANEL_H - 44;
static const int OK_X       = PANEL_W - MARGIN - BTN_W;
static const int CANCEL_X   = OK_X - BTN_W - 10;
static const int CELL_COUNT = COLS * 3;

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
    rc.left   = MARGIN + (idx % COLS) * (CELL_W + GAP_X);
    rc.top    = TITLE_H + MARGIN + (idx / COLS) * (CELL_H + GAP_Y);
    rc.right  = rc.left + CELL_W;
    rc.bottom = rc.top + CELL_H;
    return rc;
}

static RECT ActionBtnRect(int idx)
{
    RECT rc = CellRect(idx);
    rc.left += 4; rc.right -= 4;
    rc.top   = rc.bottom - 16;
    return rc;
}

static RECT TargetBtnRect(int idx)
{
    RECT rc = CellRect(idx);
    rc.left += 4; rc.right -= 4;
    rc.top   = rc.bottom - 14;
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
    scr->TextDraw(fnt, text, x, y, w, h, (eTextColor)color, align);
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
    H3LoadedPcx16* scr = o_WndMgr ? o_WndMgr->screenPcx16 : nullptr;
    if (!scr) return;

    int px = s_p.x, py = s_p.y;

    Fill(scr, px, py, PANEL_W, PANEL_H, 20, 20, 60);
    scr->DrawFrame(px, py, PANEL_W, PANEL_H, (BYTE)80, (BYTE)80, (BYTE)140);
    Fill(scr, px + 1, py + 1, PANEL_W - 2, TITLE_H - 1, 40, 40, 100);
    DrawTxt(scr, GetPanelFont(), "部队自动行动设置",
        px + 4, py + 3, PANEL_W - 8, TITLE_H - 4, COL_TITLE_TEXT, eTextAlignment::MIDDLE_LEFT);

    H3Font* fntS = GetSmallFont();
    for (int i = 0; i < s_p.count && i < CELL_COUNT; ++i) {
        int si = s_p.stack_idx[i];
        if (si < 0) continue;
        RECT cRc = CellRect(i);
        RECT aRc = ActionBtnRect(i);
        RECT tRc = TargetBtnRect(i);

        Fill(scr, cRc.left, cRc.top, CELL_W, CELL_H, 10, 30, 70);
        scr->DrawFrame(cRc.left, cRc.top, CELL_W, CELL_H, (BYTE)50, (BYTE)80, (BYTE)160);

        char coord[16] = "--";
        H3CombatManager* mgr = GetCombatMgr();
        if (mgr && si >= 0 && si < 21) {
            int hx = mgr->stacks[0][si].position;
            if (hx >= 0 && hx < 40)
                _snprintf(coord, sizeof(coord), "%c%02d", 'A' + (hx % 8), hx / 8 + 1);
        }
        DrawTxt(scr, fntS, coord, cRc.left + 4, cRc.top + 2, CELL_W - 8, 16,
            COL_TEXT, eTextAlignment::TOP_LEFT);

        Fill(scr, aRc.left, aRc.top, aRc.right - aRc.left, aRc.bottom - aRc.top, 30, 50, 100);
        DrawTxt(scr, fntS, g_action_labels[s_p.action[si]],
            aRc.left, aRc.top, aRc.right - aRc.left, aRc.bottom - aRc.top,
            COL_ACTION_TEXT, eTextAlignment::MIDDLE_CENTER);

        Fill(scr, tRc.left, tRc.top, tRc.right - tRc.left, tRc.bottom - tRc.top, 25, 40, 80);
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
