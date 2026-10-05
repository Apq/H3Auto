// ========== SettingsDlg.inc.cpp ==========
// 打铁助手 - 设置面板
// 渲染：LoHook 0x600430 画面板到 screenPcx16
// 输入捕获：检测"自动战斗"对话框的关闭事件


extern void OnBattleResultAccepted();
extern void EnsureStackTrackingBound();
void OpenSettingsPanel_();
void HandlePanelInput_();

// ========================================================================
// 第一部分：数据声明
// ========================================================================

// 面板内容绘制（实现在 PanelDraw.inc.cpp，本文件之后 include）。
#include "PanelDraw.hpp"
// 面板输入捕获（实现在 PanelInput.inc.cpp，本文件之后 include）。
#include "PanelInput.hpp"


// 面板布局常量（与 PanelGfx/PanelDraw 共用）。
#include "PanelLayout.hpp"

// 界面文案/标签由 UiTexts 提供（T()/g_action_labels/g_panel_title）。

static const INT32 COL_TITLE_TEXT  = 0x03;
static const INT32 COL_TEXT        = 0x01;
static const INT32 COL_ACTION_TEXT = 0x1A;
static const INT32 COL_TARGET_TEXT = 0x0D;

static struct Panel {
    bool active;
    int x, y;
    AutoStackRule draft_rules[PROFILE_COUNT][MAX_STACKS]; // 5 套草稿（每编号一份，切走不丢）
    uint16_t draft_stop_turns[PROFILE_COUNT];       // 自动停止回合草稿，0=关闭，0..999
    SummonProfileFields draft_summon[PROFILE_COUNT]; // 召唤通道草稿（启用/阈值/法术/召唤物规则/敌方法力停）
    StatusProfileFields draft_status[PROFILE_COUNT]; // 保持状态草稿
    bool status_add_armed; // 保持状态「＋」已按下，松开才追加
    int selected_profile;                           // 当前编号 0..4（存档文件编号 = 界面方案 1-5）
    int pressed_profile;
    int count;                 // 可配置部队总数（可大于可见行）
    CellData items[MAX_STACKS]; // 全部部队快照；可见行从这里按 scroll_row 绑定
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
    int active_page;  // 当前 Tab 页：PAGE_ARMY / PAGE_PROFILE
    CellControl cells[CELL_COUNT]; // 仅保存当前可见的最多 3 行控件
} s_p = {};

static bool s_panel_redraw_in_progress = false;
static bool s_panel_modal_suspended = false;
// 战场拾取期间隐藏面板（不绘制），但保持 s_p.active，用于取坐标而不放行点击
static bool s_panel_hidden_for_pick = false;

// 面板打开时安装 WH_KEYBOARD 钩子，立即响应 ESC/Enter，不依赖游戏帧率
static void CommitAndCloseSettingsPanel_();
static void EndSpellPick_();
static bool CommitSpellSlotPick_(int slot_value);
static void DrawPanelToBuffer_();
// 循环施法录入状态需在键盘钩子前声明。
static bool s_help_modal_open = false;
static bool s_help_log_dd_open = false;
static DWORD s_help_link_flash_tick = 0; // 复制开源地址成功时刻，地址行闪绿 1.2s
static bool s_stop_turns_editing = false; // 正在录入当前方案的停止回合
static char s_stop_turns_text[8] = {};     // 最多 3 位 + 结束符
static int  s_stop_turns_caret = 0;        // 插入位置（0..文本长度）
static DWORD s_stop_turns_caret_tick = 0;  // 光标闪烁基准（按键后重置，输入即可见）
// 「敌方魔力≤[框] 时停」阈值录入（与停止回合同款；0..32767，默认 6）。
static bool s_mana_th_editing = false;
static char s_mana_th_text[8] = {};        // 最多 5 位 + 结束符
static int  s_mana_th_caret = 0;
static DWORD s_mana_th_caret_tick = 0;

// 保持状态页「剩余回合≤N 时补」阈值（方案级，1..9 默认 1）。
static bool s_status_th_editing = false;
static char s_status_th_text[8] = {};      // 最多 1 位 + 结束符
static int  s_status_th_caret = 0;
static DWORD s_status_th_caret_tick = 0;
static const int STOP_TURNS_MAX_DIGITS = 3; // 输入上限 3 位；提交截到 999
static char s_status_text[512] = {};
static DWORD s_status_until = 0;
// 当前文本是否来自悬停 tip（tip 在光标静止时只续期，不参与覆盖竞争）。
static bool s_status_is_tip = false;

// 状态栏统一入口：富文本（{颜色名} 标记，如 {绿}{红}{金}{白}{灰}{黄}{蓝}，
// 也认 {XX} 两位十六进制），整段按 hold_ms 延时后自动消失；
// 语义：任何新调用立即覆盖旧文本并重新计时（结果消息、tips 同一规则）。
static void SetStatusText_(const char* rich_text, DWORD hold_ms)
{
    s_status_text[0] = 0;
    s_status_is_tip = false;
    if (rich_text && rich_text[0]) {
        strncpy(s_status_text, rich_text, sizeof(s_status_text) - 1);
        s_status_text[sizeof(s_status_text) - 1] = 0;
        s_status_until = GetTickCount() + hold_ms;
    } else {
        s_status_until = 0;
    }
}

static HHOOK s_kb_hook = nullptr;
static HHOOK s_mouse_hook = nullptr;
static int s_spell_pick_cell = -1;
static int s_spell_pick_slot = -1;
static int s_melee_pick_cell = -1;
static int s_melee_pick_pair = -1;
static int s_melee_pick_phase = 0;
static int s_melee_pick_stand_hex = -1;
static bool s_pick_wait_button_release = false;
static int s_move_pick_cell = -1;
static int s_move_pick_wp = -1;

// ===== 保活通道下拉（方案级）与数字键拦截 =====

// 设置面板存活期间（含隐藏拾取态）拦截 0-9/小键盘，避免原版快捷施法抢键。
static void CommitStopTurnsEdit_();
static void CancelStopTurnsEdit_();


// 面板打开时安装 WH_MOUSE 钩子，鼠标移动立即刷新下拉悬停高亮，不依赖游戏帧率
static void GetSpellKeyModalRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void GetSpellKeyModalCancelRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void DrawSpellKeyModal_(H3LoadedPcx16* scr);

static const UINT s_right_click_dialog_vtable = 0x0063DB40;
// 真正的战斗窗口。0x0063A5E4 是战斗底下仍保留的冒险地图窗口。
static const UINT s_combat_dialog_vtable = 0x0063D528;
// 原版战斗结果窗 CPResult 虚表（FUN_0046fe20 构造）
static const UINT s_cpresult_dialog_vtable = 0x0063D46C;
// BattleUI 底栏 ICM004.def：点击分支切换 H3CombatManager::autoCombat。
static const INT32 s_autofight_button_id = 0x7D4;
// 结果窗「确定/接受」按钮 id（iOkay.def，FUN_0046fe20 / FUN_004716e0）
static const INT32 s_cpresult_ok_id = 0x7802;
// HD ReplayableQB 追加的「取消/重打」按钮 id（iCANCEL.def）
static const INT32 s_cpresult_cancel_id = 0x1FB;
// 战斗中弹出过设置面板
static bool s_panel_popup_done = false;
// 是否检测到 BattleUI 上层的自动战斗右键说明框
static bool s_saw_explanation_dlg_in_battle = false;
// 右键按下时鼠标确实位于自动战斗按钮上。
static bool s_autofight_right_press_armed = false;
// BattleUI 连续缺失帧数，避免对话框切换的瞬时空帧误判为战斗结束
static int s_battle_ui_missing_frames = 0;
// 结果窗生命周期：见过结果窗后，取消重打保留设置；接受才清除。
static H3AutoPolicy::ResultLifecycleState s_result_lifecycle = {};
static bool s_saw_cpresult = false;
static bool s_result_accept_armed = false;
static bool s_result_cancel_armed = false;
// 未命中按钮时，结果窗消失后先给 BattleUI 留出恢复时间，避免取消重打
// 的过渡空窗被当成接受结果。
static DWORD s_result_closed_since = 0;
static const DWORD RESULT_CLOSE_GRACE_MS = 1500;
// 取消已按但 BattleUI 迟迟不回（如结果窗上直接退出/读档）：总超时后按
// 终局裁决。结果窗意图未裁决期间 UI_GONE 兜底退位（见 CheckAutoFight-
// DialogClosed），本超时保证裁决必然发生、状态不会卡在 RESULT。
static const DWORD RESULT_ABORT_TIMEOUT_MS = 8000;

// 前向声明
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

// 复制 UTF-8 文本到剪贴板（CF_UNICODETEXT）。同 LogPack 的重试节奏：
// 剪贴板被其它程序占用是常态。失败返回 false。
static bool CopyTextToClipboard_(const char* utf8_text)
{
    if (!utf8_text || !utf8_text[0]) return false;
    wchar_t wide[256] = {};
    if (MultiByteToWideChar(CP_UTF8, 0, utf8_text, -1, wide,
            (int)(sizeof(wide) / sizeof(wide[0]))) <= 0)
        return false;
    const size_t bytes = (wcslen(wide) + 1) * sizeof(wchar_t);
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (!OpenClipboard(nullptr)) {
            Sleep(30);
            continue;
        }
        bool ok = EmptyClipboard();
        if (ok) {
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (mem) {
                void* raw = GlobalLock(mem);
                if (raw) {
                    memcpy(raw, wide, bytes);
                    GlobalUnlock(mem);
                    ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
                    if (!ok) GlobalFree(mem); // 成功后归剪贴板所有
                } else {
                    GlobalFree(mem);
                    ok = false;
                }
            } else {
                ok = false;
            }
        }
        CloseClipboard();
        return ok;
    }
    return false;
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

static void SaveCurrentCellsToDraft_();
static void RebindVisibleCells_();

static void SetPanelScrollRow_(int row)
{
    const int max_row = PanelMaxScrollRow_();
    if (row < 0) row = 0;
    if (row > max_row) row = max_row;
    if (row == s_p.scroll_row) return;

    // 滚动前保存当前可见 3 行的草稿；滚动后按新 first_item 重新绑定。
    SaveCurrentCellsToDraft_();
    s_p.scroll_row = row;
    RebindVisibleCells_();
}


// ========================================================================
// 第三部分：战斗状态判断
// ========================================================================


// ========================================================================
// 第四部分：检测"自动战斗"对话框关闭 + 结果窗生命周期
// ========================================================================
// 状态机：firstDlg 是底层战斗界面，lastDlg 才是当前最上层对话框。
// 流程：BattleUI 存在 + lastDlg=自动战斗说明框 → lastDlg 回到 BattleUI → 弹窗。
//
// 设置生命周期：
// - 看到 CPResult 结果窗 → 标记 s_saw_cpresult
// - 结果窗上点「确定」(0x7802) 后结果窗消失 → 清除 5 套方案
// - 结果窗上点「取消/重打」(0x1FB) 后 BattleUI 回来 → 保留方案并重绑跟踪
// - 无取消按钮的纯原版结果窗：只能点确定，消失即接受
//
static H3BaseDlg* FindDialogByVtableDeep_(UINT vtable)
{
    H3BaseDlg* found = FindDialogByVtable_(vtable);
    if (found) return found;
    if (o_WndMgr && o_WndMgr->lastDlg && *(UINT*)o_WndMgr->lastDlg == vtable)
        return o_WndMgr->lastDlg;
    return nullptr;
}

static INT32 GetDlgItemUnderCursor_(H3BaseDlg* dlg)
{
    if (!dlg) return -1;
    const H3POINT cursor = H3POINT::GetCursorPosition();

    // 先按绝对坐标扫 dlgItems（后命中覆盖前命中，近似取上层）。
    __try {
        // H3BaseDlg::dlgItems 在 +0x30（见 H3API H3BaseDlg）。
        auto& items = *reinterpret_cast<H3Vector<H3DlgItem*>*>(
            reinterpret_cast<BYTE*>(dlg) + 0x30);
        INT32 hit = -1;
        for (H3DlgItem** it = items.begin(); it != items.end(); ++it) {
            H3DlgItem* item = *it;
            if (!item || !item->IsVisible()) continue;
            const INT32 x = item->GetAbsoluteX();
            const INT32 y = item->GetAbsoluteY();
            if (cursor.x >= x && cursor.x < x + item->GetWidth()
                && cursor.y >= y && cursor.y < y + item->GetHeight())
            {
                hit = item->GetID();
            }
        }
        if (hit != -1) return hit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    H3Msg msg = {};
    msg.position = cursor;
    H3DlgItem* item = dlg->ItemAtPosition(msg);
    return item ? item->GetID() : -1;
}

static void CheckBattleResultLifecycle_()
{
    if (!o_WndMgr) return;

    H3BaseDlg* result_dlg = FindDialogByVtableDeep_(s_cpresult_dialog_vtable);
    H3BaseDlg* combat_dlg = FindDialogByVtable_(s_combat_dialog_vtable);
    const bool result_visible = result_dlg != nullptr;
    const bool battle_ui_exists = combat_dlg != nullptr;

    if (result_visible) {
        s_result_closed_since = 0;
        if (!s_saw_cpresult) {
            s_saw_cpresult = true;
            // 状态机事件源（S1）：结果窗出现（含快速战斗未经战斗 UI 的路径）。
            SetPhase_(BP_RESULT, BE_RESULT_SHOWN);
            H3AutoPolicy::ApplyResultLifecycle(
                &s_result_lifecycle, H3AutoPolicy::RESULT_SHOWN);
            s_result_accept_armed = false;
            s_result_cancel_armed = false;
            LogInfo("[Life] CPResult 结果窗出现，等待接受/取消重打。");
        }

        // 边沿：在结果窗上按下鼠标左键时记录命中按钮。
        const bool ldown = IsGameMouseInputActive_()
            && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (ldown) {
            const INT32 id = GetDlgItemUnderCursor_(result_dlg);
            if (id == s_cpresult_ok_id) {
                if (!s_result_accept_armed)
                    LogInfo("[Life] 结果窗命中确定/接受 id=0x%X。", id);
                s_result_accept_armed = true;
                s_result_cancel_armed = false;
                H3AutoPolicy::ApplyResultLifecycle(
                    &s_result_lifecycle, H3AutoPolicy::RESULT_ACCEPT_CLICKED);
            } else if (id == s_cpresult_cancel_id) {
                if (!s_result_cancel_armed)
                    LogInfo("[Life] 结果窗命中取消/重打 id=0x%X。", id);
                s_result_cancel_armed = true;
                s_result_accept_armed = false;
                H3AutoPolicy::ApplyResultLifecycle(
                    &s_result_lifecycle, H3AutoPolicy::RESULT_CANCEL_CLICKED);
            }
        }
        return;
    }

    // 结果窗刚消失。
    if (!s_saw_cpresult) return;

    if (battle_ui_exists) {
        s_result_closed_since = 0;
    } else {
        // 窗口已消失：从消失时刻起统一计时（armed 与否都计，供总超时用）。
        const DWORD now = GetTickCount();
        if (s_result_closed_since == 0)
            s_result_closed_since = now;
        const bool armed =
            s_result_accept_armed || s_result_cancel_armed;
        // 未捕获到点击（键盘 ESC/回车关窗走这里）：短宽限防过渡空窗误判。
        if (!armed && now - s_result_closed_since < RESULT_CLOSE_GRACE_MS)
            return;
        // 取消已按：等 BattleUI 回来（权威重打信号）；总超时内不裁决。
        if (s_result_cancel_armed
            && now - s_result_closed_since < RESULT_ABORT_TIMEOUT_MS)
            return;
        // 超时仍未回来：战斗没有在重打，按终局（无战斗 UI 收场）裁决，
        // 清掉 cancel_armed 让纯函数走 CLEAR 分支而非无限 WAIT。
        if (s_result_cancel_armed)
            s_result_cancel_armed = false;
    }

    const H3AutoPolicy::ResultLifecycleAction lifecycle_action =
        H3AutoPolicy::ApplyResultLifecycle(
            &s_result_lifecycle,
            battle_ui_exists ? H3AutoPolicy::RESULT_CLOSED_WITH_BATTLE_UI
                             : H3AutoPolicy::RESULT_CLOSED_WITHOUT_BATTLE_UI);

    if (lifecycle_action == H3AutoPolicy::RESULT_KEEP_AND_REBIND) {
        LogInfo("[Life] 取消/重打：保留 5 套方案并重绑跟踪。");
        s_saw_cpresult = false;
        s_result_accept_armed = false;
        s_result_cancel_armed = false;
        s_result_closed_since = 0;
        s_panel_popup_done = false;
        // 状态机重打边（S3.2）：边动作=EnsureStackTrackingBound（重排+重绑+清运行时）。
        SetPhase_(BP_COMBAT_CLOSED, BE_RESULT_RETRY);
        return;
    }

    if (lifecycle_action == H3AutoPolicy::RESULT_CLEAR_SETTINGS) {
        // 点了确定，或原版无取消按钮时默认视为接受。
        LogInfo("[Life] 接受战斗结果：清除设置。 accept=%d cancel=%d battle_ui=%d",
            s_result_accept_armed ? 1 : 0,
            s_result_cancel_armed ? 1 : 0,
            battle_ui_exists ? 1 : 0);
        s_saw_cpresult = false;
        s_result_accept_armed = false;
        s_result_cancel_armed = false;
        s_result_closed_since = 0;
        s_panel_popup_done = false;
        // 状态机接受边（S3.3）：清方案+运行时，ENDED 瞬态后续转 PEACE。
        SetPhase_(BP_ENDED, BE_RESULT_ACCEPTED);
        return;
    }

    // 取消已按但 BattleUI 尚未回来：等下一帧。
}

static void CheckAutoFightDialogClosed()
{
    if (!o_WndMgr) return;

    // 先处理结果窗生命周期（与自动战斗说明框互不依赖）。
    CheckBattleResultLifecycle_();

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
            LogInfo("[AutoFight] 右键按下命中自动战斗按钮 id=0x%X。", s_autofight_button_id);
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
        s_battle_ui_missing_frames = 0;
        // 状态机事件源（S1）：战斗 UI 出现。
        if (g_phase == BP_PEACE)
            SetPhase_(BP_COMBAT_CLOSED, BE_BATTLE_UI_APPEARED);
        // S8：打开面板热键消费（面板关分支；与右键路径同构：先开后迁）。
        if (g_auto_state.kb_open_panel_seen) {
            g_auto_state.kb_open_panel_seen = false;
            if (g_phase == BP_COMBAT_CLOSED && !IsPanelActive()) {
                LogInfo("[Panel] OpenSettings hotkey");
                OpenSettingsPanel_();
                if (s_p.active)
                    SetPhase_(BP_COMBAT_OPEN, BE_PANEL_OPEN_REQUESTED);
            }
        }
    } else if (++s_battle_ui_missing_frames >= 3) {
        // 状态机事件源（S1/S4）：战斗 UI 消失兜底（读档/中途退出等未经结算）。
        // 重打宽限期内不视为兜底（UI 重建常超 3 帧，见重构步骤 S4.2）。
        // 结果窗意图未裁决期间（s_saw_cpresult）兜底退位：玩家用键盘关
        // 结果窗时点击捕捉不到，Life 层要等宽限/超时才裁决，此间 UI_GONE
        // 抢跑会把状态推到 PEACE，随后 RESULT_RETRY/RESULT_ACCEPTED 被拒、
        // 边动作丢失（重打后跟踪不重绑 → 全场 takeover mismatch）。
        // 裁决必然发生：未捕获点击 1500ms 宽限、取消已按 8000ms 总超时。
        if ((InCombat_() || g_phase == BP_RESULT)
            && GetTickCount() > g_ui_gone_grace_until
            && !s_saw_cpresult)
            SetPhase_(BP_ENDED, BE_BATTLE_UI_GONE);
        s_saw_explanation_dlg_in_battle = false;
        s_autofight_right_press_armed = false;
        s_panel_popup_done = false;
    }

    if (explanation_is_top && !s_panel_popup_done) {
        if (!s_saw_explanation_dlg_in_battle) {
            s_saw_explanation_dlg_in_battle = true;
            LogInfo("[AutoFight] 检测到右键按住时的自动战斗说明框，w=%d h=%d。", last_w, last_h);
        }
        return;
    }

    if (s_saw_explanation_dlg_in_battle && battle_ui_exists
        && last_vtable != s_right_click_dialog_vtable && !s_panel_popup_done)
    {
        s_saw_explanation_dlg_in_battle = false;
        s_autofight_right_press_armed = false;
        LogInfo("[AutoFight] 说明框已关闭且 BattleUI 仍在，打开设置面板。");
        OpenSettingsPanel_();
        // 状态机打开边（S2.1）：面板确认打开后再迁移，杜绝「已迁移但没开」死状态。
        if (s_p.active)
            SetPhase_(BP_COMBAT_OPEN, BE_PANEL_OPEN_REQUESTED);
        s_panel_popup_done = true;
    } else if (!right_button_down && !s_saw_explanation_dlg_in_battle) {
        // 在目标按钮上按下但没有出现对应说明框时，不把状态带到下一次右键。
        s_autofight_right_press_armed = false;
    }
}

// ========================================================================
// 第五部分：LoHook
// ========================================================================

extern bool TryAutoExecuteActiveStack(bool allow_unit_action);

INT __stdcall Hook_BltComplete(LoHook* h, HookContext* c)
{
    (void)h; (void)c;
    static int s_frame = 0;
    s_frame++;
    CheckAutoFightDialogClosed();
    if (s_p.active) {
        ForcePanelModalDepth_(true);
        UpdatePanelModalSuspension_();
        // 拾取期间（s_panel_hidden_for_pick）面板隐藏：跳过重绘与常规输入，
        // 点击交给屏障 item 处理。但 suspended 保持 false，否则输入不派发。
        if (s_p.active && !s_panel_modal_suspended && !s_panel_hidden_for_pick) {
            HandlePanelInput_();
            DrawPanelToBuffer_();
            ForcePanelDefaultCursor_();
        }
        if (s_p.active && s_panel_hidden_for_pick)
            DrawMeleePickMarker_();
    } else {
        // 循环施法是两阶段状态机：必须每帧观察 heroCasted/法力变化，
        // 再决定推进游标并提交部队主动作，不能只依赖偶发战斗消息。
        __try {
            TryAutoExecuteActiveStack(false);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return EXEC_DEFAULT;
}

// 战斗消息处理入口（FUN_004746b0 @ 0x4746B0）。this=H3CombatManager 在 ECX，
// 消息指针 msg 在栈上 [esp+4]。msg[0]=类型（4=鼠标移动），msg[4]=x，msg[5]=y。
// 1) 面板打开时：把鼠标移动坐标改成离屏，清掉 hover 高亮。
// 2) 面板关闭时：若当前活动单位应由 H3Auto 主动执行，则在此提交 action 字段，
//    让原版主循环自然进入 FUN_004786b0 执行动画/伤害/回合推进。
INT __stdcall Hook_BattleMsgProc(LoHook* h, HookContext* c)
{
    (void)h;

    // 战场拾取（近战选格 / 循环移动路径）的点击捕获已移到透明 item 屏障
    // BlockBattleItemMessage_ 里处理（靠 StopProcessing 吞点击，绝不触发
    // 部队行动）。此处不再处理拾取，避免与屏障逻辑冲突、空转。

    // 设置面板存活期间（含隐藏拾取态）：吞掉 0-9 数字键消息，
    // 防止原版/HD 快捷施法在战斗消息链里捕获热键。
    if (s_p.active) {
        __try {
            int* msg = *reinterpret_cast<int**>(c->esp + 4);
            if (msg) {
                const int cmd = msg[0];
                // KEY_DOWN=1 / KEY_UP=2 / KEY_HELD=0x100；键码在 subtype(=msg[1])。
                if (cmd == 1 || cmd == 2 || cmd == 0x100) {
                    const int key = msg[1];
                    // H3 虚拟键：H3VK_1=2 ... H3VK_9=10, H3VK_0=11
                    if (key >= 2 && key <= 11)
                        return NO_EXEC_DEFAULT;
                }
                if (!s_panel_modal_suspended && !s_panel_hidden_for_pick
                    && cmd == 4) {   // 鼠标移动
                    msg[4] = -1000; // x 离屏
                    msg[5] = -1000; // y 离屏
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return EXEC_DEFAULT;
    }

    // 面板未打开：尝试主动提交当前单位动作（防御/远程/近战等）。
    // 只写 battle->action，不跳过原函数；原函数看到 action!=0 会走执行路径。
    __try {
        TryAutoExecuteActiveStack(true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return EXEC_DEFAULT;
}


// ===== 保活通道下拉行（方案级，随当前选中方案的草稿） =====
static void CommitStopTurnsEdit_()
{
    int value = 0;
    for (int i = 0; s_stop_turns_text[i]; ++i)
        value = value * 10 + (s_stop_turns_text[i] - '0');
    if (value > 999) value = 999;
    s_p.draft_stop_turns[s_p.selected_profile] = static_cast<uint16_t>(value);
    s_stop_turns_editing = false;
    s_stop_turns_text[0] = 0;
    s_stop_turns_caret = 0;
    LogInfo("[Panel] 停止回合=%d (方案%d)", value, s_p.selected_profile + 1);
}

static void CancelStopTurnsEdit_()
{
    s_stop_turns_editing = false;
    s_stop_turns_text[0] = 0;
    s_stop_turns_caret = 0;
}

// 敌方魔力阈值录入提交：空=0（与停止回合/「剩≤」同款下限语义），
// 溢出截到 32767；写回当前方案召唤草稿。
static void CommitManaThEdit_()
{
    int value = 0;
    for (int i = 0; s_mana_th_text[i]; ++i)
        value = value * 10 + (s_mana_th_text[i] - '0');
    if (value > 32767) value = 32767;
    s_p.draft_summon[s_p.selected_profile].stop_mana_th = value;
    s_mana_th_editing = false;
    s_mana_th_text[0] = 0;
    s_mana_th_caret = 0;
    LogInfo("[Panel] 敌方魔力阈值=%d (方案%d)", value,
        s_p.selected_profile + 1);
}

static void CancelManaThEdit_()
{
    s_mana_th_editing = false;
    s_mana_th_text[0] = 0;
    s_mana_th_caret = 0;
}

// 补状态阈值录入提交：空=1（下限即默认，无 0 语义——0 会导致永不补），
// 钳 1..9；写回当前方案状态草稿。
static void CommitStatusThEdit_()
{
    int value = 0;
    for (int i = 0; s_status_th_text[i]; ++i)
        value = value * 10 + s_status_th_text[i] - '0';
    if (value < 1) value = 1;
    if (value > H3AutoPolicy::kStatusRefreshTurnsMax)
        value = H3AutoPolicy::kStatusRefreshTurnsMax;
    s_p.draft_status[s_p.selected_profile].refresh_turns = value;
    s_status_th_editing = false;
    s_status_th_text[0] = 0;
    s_status_th_caret = 0;
    LogInfo("[Panel] 补状态阈值=%d (方案%d)", value,
        s_p.selected_profile + 1);
}

static void CancelStatusThEdit_()
{
    s_status_th_editing = false;
    s_status_th_text[0] = 0;
    s_status_th_caret = 0;
}

// ===== 召唤页（PAGE_SUMMON）控件状态 =====
// 三个下拉（法术/条件组合/召唤物行动）悬停高亮；法术下拉选项 0..4（自动+四系）。
static bool s_summon_spell_dd_open = false;
static int  s_summon_spell_dd_hover = -1;
static bool s_summon_cond_dd_open = false;
static int  s_summon_cond_dd_hover = -1;
static bool s_summon_act_dd_open = false;
static int  s_summon_act_dd_hover = -1;
static int  s_status_dd_open = -1;
static int  s_status_dd_hover = -1;
static int  s_status_dd_ids[81] = {};
static int  s_status_dd_count = 0;
// 队数/血量阈值数字录入：与停止回合同款（预填当前值、光标可移动）。
enum SummonNumEditWhich { SUMMON_EDIT_NONE = 0, SUMMON_EDIT_COUNT, SUMMON_EDIT_HP };
static int  s_summon_edit_which = SUMMON_EDIT_NONE;
static char s_summon_edit_text[16] = {};
static int  s_summon_edit_caret = 0;
static DWORD s_summon_edit_caret_tick = 0;

// 召唤页数字框矩形（which: SUMMON_EDIT_COUNT / SUMMON_EDIT_HP）。
// 两条件分两行左对齐：队数在设置行一（法术行右半），血量在设置行二。
static void SummonNumBoxRect_(int which, int* out_x, int* out_y,
    int* out_w, int* out_h)
{
    const bool is_hp = (which == SUMMON_EDIT_HP);
    const int x = is_hp ? SUMMON_HP_BOX_X : SUMMON_CNT_BOX_X;
    const int y = is_hp ? SUMMON_ROW2_Y : SUMMON_ROW1_Y;
    const int w = is_hp ? SUMMON_HP_BOX_W : SUMMON_CNT_BOX_W;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = w;
    if (out_h) *out_h = SUMMON_DD_H;
}

// 录入框文本起绘 x（左对齐，与绘制端一致）。
static int SummonNumBoxTextX_(int which)
{
    return (which == SUMMON_EDIT_HP ? SUMMON_HP_BOX_X : SUMMON_CNT_BOX_X) + 8;
}

static int SummonNumBoxMaxDigits_(int which)
{
    return which == SUMMON_EDIT_HP
        ? SUMMON_HP_MAX_DIGITS : SUMMON_CNT_MAX_DIGITS;
}

// 提交（框外点击/回车/切页）：空文本=0；越界钳制（队数 2..21；血量≥0）。
static void CommitSummonNumEdit_()
{
    if (s_summon_edit_which == SUMMON_EDIT_NONE) return;
    long long value = 0;
    for (int i = 0; s_summon_edit_text[i]; ++i)
        value = value * 10 + (s_summon_edit_text[i] - '0');
    SummonProfileFields& sf =
        s_p.draft_summon[s_p.selected_profile];
    if (s_summon_edit_which == SUMMON_EDIT_COUNT) {
        if (value < 2) value = 2; // 最小即默认 2
        if (value > 21) value = 21;
        sf.count_th = (int)value;
    } else {
        if (value > 2147483647LL) value = 2147483647LL;
        sf.hp_th = (int)value;
    }
    LogInfo("[Panel] 召唤阈值：队数<%d 血量≤%d (方案%d)", sf.count_th, sf.hp_th,
        s_p.selected_profile + 1);
    s_summon_edit_which = SUMMON_EDIT_NONE;
    s_summon_edit_text[0] = 0;
    s_summon_edit_caret = 0;
}

static void CancelSummonNumEdit_()
{
    s_summon_edit_which = SUMMON_EDIT_NONE;
    s_summon_edit_text[0] = 0;
    s_summon_edit_caret = 0;
}

// 进入编辑：预填当前草稿值（值 0 预填空，避免「点击即变 0」），光标在末尾。
static void BeginSummonNumEdit_(int which)
{
    if (which != SUMMON_EDIT_COUNT && which != SUMMON_EDIT_HP) return;
    if (s_summon_edit_which == which) return;   // 已在编辑该框：不重置
    CommitSummonNumEdit_();                     // 另一框在录：先提交
    s_summon_edit_which = which;
    s_summon_edit_text[0] = 0;
    const SummonProfileFields& sf =
        s_p.draft_summon[s_p.selected_profile];
    const int cur = (which == SUMMON_EDIT_HP) ? sf.hp_th : sf.count_th;
    if (cur > 0)
        _snprintf(s_summon_edit_text, sizeof(s_summon_edit_text), "%d", cur);
    s_summon_edit_caret = (int)strlen(s_summon_edit_text);
    s_summon_edit_caret_tick = GetTickCount();
}

// 收起召唤页三个下拉与录入态（切页/切方案/关面板/开帮助时）。
static void CloseSummonDropdowns_()
{
    s_summon_spell_dd_open = false;
    s_summon_spell_dd_hover = -1;
    s_summon_cond_dd_open = false;
    s_summon_cond_dd_hover = -1;
    s_summon_act_dd_open = false;
    s_summon_act_dd_hover = -1;
    s_status_dd_open = -1;
    s_status_dd_hover = -1;
}

// 构建保持状态下拉候选：英雄已学的群体增益 + 专家级群体减速；
// skip_slot 以外的槽位已用的法术排除（避免重复）。结果进 s_status_dd_ids。
static int BuildStatusCandidates_(const StatusProfileFields& status, int skip_slot)
{
    s_status_dd_count = 0;
    H3Hero* hero = nullptr;
    H3CombatManager* cm = H3CombatManager::Get();
    if (cm && o_BattleMgr) {
        const int side = ResolveHumanSide_(o_BattleMgr);
        if (side >= 0 && side <= 1)
            hero = reinterpret_cast<H3Hero*>(o_BattleMgr->hero[side]);
    }
    const BYTE* table = *reinterpret_cast<BYTE**>(0x687FA8);
    if (!hero || !table || !cm) return 0;
    int candidates[H3AutoPolicy::kBuffSpellCount + 1] = {};
    int candidate_count = 0;
    for (int spell : H3AutoPolicy::kBuffSpellIds)
        candidates[candidate_count++] = spell;
    candidates[candidate_count++] = H3AutoPolicy::kSlowSpellId;
    for (int n = 0; n < candidate_count; ++n) {
        const int spell = candidates[n];
        bool used = false;
        for (int i = 0; i < status.slot_count; ++i)
            if (i != skip_slot)
                used = used || status.slots[i] == spell;
        if (used) continue;
        const unsigned flags = *reinterpret_cast<const unsigned*>(
            table + spell * 0x88 + 0x0C);
        const bool slow = spell == H3AutoPolicy::kSlowSpellId;
        if (!slow && !H3AutoPolicy::IsMassBuffSpell(spell, flags))
            continue;
        const int expertise = hero->GetSpellExpertise(
            spell, cm->specialTerrain);
        if (expertise <= 0 || (slow && expertise < 3)) continue;
        s_status_dd_ids[s_status_dd_count++] = spell;
    }
    return s_status_dd_count;
}

// 复选框「框在文字前」：复用卡片降级勾选样式（10px 方框 + 勾）。
// 通用绘制：x=框左缘，y=框顶，checked=勾选，label=文字。
static void DrawCheckbox_(H3LoadedPcx16* scr, int x, int y, bool checked,
    const char* label, int text_w, int box)
{
    Fill(scr, x, y, box, box, 40, 28, 12);
    scr->DrawFrame(x, y, box, box, (BYTE)184, (BYTE)139, (BYTE)62);
    if (checked) {
        Fill(scr, x + 2, y + 4, 2, 2, 235, 205, 116);
        Fill(scr, x + 3, y + 5, 2, 2, 235, 205, 116);
        Fill(scr, x + 4, y + 6, 2, 2, 235, 205, 116);
        Fill(scr, x + 5, y + 5, 2, 2, 235, 205, 116);
        Fill(scr, x + 6, y + 4, 2, 2, 235, 205, 116);
        Fill(scr, x + 7, y + 3, 2, 2, 235, 205, 116);
    }
    DrawTxt(scr, GetSmallFont(), label,
        x + box + 4, y - 1, text_w, 14,
        (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
}

// 法术下拉选项文案（0=自动，1..4=气水火土，顺序同 kSummonSpellIds）。
static const char* SummonSpellOptLabel_(int i)
{
    switch (i) {
    case 0: return T("panel.summon_spell_opt0");
    case 1: return T("panel.summon_spell_opt1");
    case 2: return T("panel.summon_spell_opt2");
    case 3: return T("panel.summon_spell_opt3");
    case 4: return T("panel.summon_spell_opt4");
    default: return "?";
    }
}

// 条件组合下拉选项文案（0=和，1=或）。
static const char* SummonCondOptLabel_(int i)
{
    return i == H3AutoPolicy::SUMMON_COMBINE_OR
        ? T("panel.summon_cond_or") : T("panel.summon_cond_and");
}

// 召唤物行动下拉选项文案（专用 4 动作集，见 GetAllowedSummonActions）。
static const char* SummonActOptLabel_(int i)
{
    AutoActionKind acts[4] = {};
    const int n = H3AutoPolicy::GetAllowedSummonActions(acts);
    if (i < 0 || i >= n || !g_action_labels[acts[i]])
        return "?";
    return g_action_labels[acts[i]];
}

static int SummonActOptFromAction_(AutoActionKind action)
{
    AutoActionKind acts[4] = {};
    const int n = H3AutoPolicy::GetAllowedSummonActions(acts);
    for (int i = 0; i < n; ++i)
        if (acts[i] == action) return i;
    return 0;
}

// ===== 部队卡片「剩≤」数量阈值录入（保活通道=按数量） =====
// 状态挂在各 CellControl 上；这里只做面板级的查找与收尾。
static bool PanelAnyProtectCountEditing_()
{
    for (int i = 0; i < CELL_COUNT; ++i)
        if (s_p.cells[i].cnt_editing) return true;
    return false;
}

static CellControl* PanelEditingProtectCountCell_()
{
    for (int i = 0; i < CELL_COUNT; ++i)
        if (s_p.cells[i].cnt_editing) return &s_p.cells[i];
    return nullptr;
}

static void PanelCommitAllProtectCountEdits_()
{
    for (int i = 0; i < CELL_COUNT; ++i)
        if (s_p.cells[i].cnt_editing)
            CellControl_CommitCountEdit_(&s_p.cells[i]);
}

static void PanelCancelAllProtectCountEdits_()
{
    for (int i = 0; i < CELL_COUNT; ++i)
        CellControl_CancelCountEdit_(&s_p.cells[i]);
}


// 拾取模式：隐藏面板，并让原版战场重绘覆盖面板储留像素（只隐藏，不关闭）。
// 仅 H3Redraw 区域失效不够：不会把已经 blit 到屏幕的面板储留清掉。
// 必须请求 CombatManager::Refresh 重画战场；此时 s_panel_hidden_for_pick
// 已为 true，Hook_BltComplete 不会把面板再画回去。
static void HidePanelForPick_()
{
    if (H3CombatManager* mgr = GetCombatMgr()) {
        __try {
            s_panel_redraw_in_progress = true;
            // 与 H3CombatManager::Refresh(TRUE, 0, TRUE) 同参：
            // redrawScreen / timeDelay / redrawBackground。
            // 把战场重画到屏幕，覆盖面板储留像素。
            THISCALL_7(void, 0x493FC0, mgr, TRUE, FALSE, FALSE, 0, TRUE, FALSE);
            s_panel_redraw_in_progress = false;
            LogInfo("[Panel] 拾取隐藏：已请求战场重绘覆盖面板");
            return;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s_panel_redraw_in_progress = false;
        }
    }
    if (!o_WndMgr) return;
    __try {
        s_panel_redraw_in_progress = true;
        o_WndMgr->H3Redraw(s_p.x, s_p.y, PANEL_W, PANEL_H);
        s_panel_redraw_in_progress = false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_panel_redraw_in_progress = false;
    }
}

// ========================================================================
// 第七部分：面板控制
// ========================================================================

static bool IsConfigurablePanelStack_(const H3CombatCreature& stack, const H3Hero* hero)
{
    const bool has_ballistics = hero
        && hero->secSkill[eSecondary::BALLISTICS] > 0;
    // 与 MakeStableStackIdentity 同口径：克隆强制无军队槽；军队槽 0..6 之外
    // 且非战争机器 = 召唤物/克隆物（STACK_ID_SUMMON）→ 部队页不收（§2.6）。
    // 玩家自带的元素生物（军队槽 0..6）不受影响。
    const H3AutoPolicy::StableStackIdentity id =
        H3AutoPolicy::MakeStableStackIdentity(0,
            (stack.cloneId > 0) ? -1 : stack.slotIndex, stack.type, 0);
    return H3AutoPolicy::IsConfigurablePanelStack(
        stack.type, (int)stack.numberAtStart, has_ballistics,
        id.kind == H3AutoPolicy::STACK_ID_SUMMON);
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
    // 「剩≤」录入先落进卡片规则，再随规则一起收进草稿。
    PanelCommitAllProtectCountEdits_();
    const int profile = s_p.selected_profile;
    if (profile < 0 || profile >= PROFILE_COUNT) return;
    for (int i = 0; i < CELL_COUNT; ++i) {
        CellControl* ctrl = &s_p.cells[i];
        if (!ctrl->has_data) continue;
        const int slot = ctrl->data.army_slot_ix;
        if (slot < 0 || slot >= MAX_STACKS) continue;
        s_p.draft_rules[profile][slot] = ctrl->data.rule;
        // 同步全量快照，避免再次滚回时短暂显示旧规则。
        const int item_index = s_p.scroll_row * COLS + i;
        if (item_index >= 0 && item_index < s_p.count)
            s_p.items[item_index].rule = ctrl->data.rule;
    }
}

// 根据 scroll_row 把全量部队快照重新绑定到可见 3 行。
static void RebindVisibleCells_()
{
    const int profile = s_p.selected_profile;
    const int first_item = s_p.scroll_row * COLS;
    for (int i = 0; i < CELL_COUNT; ++i) {
        CellControl* ctrl = &s_p.cells[i];
        const int item_index = first_item + i;
        if (item_index >= 0 && item_index < s_p.count) {
            CellData cd = s_p.items[item_index];
            const int slot = cd.army_slot_ix;
            if (profile >= 0 && profile < PROFILE_COUNT
                && slot >= 0 && slot < MAX_STACKS)
                cd.rule = s_p.draft_rules[profile][slot];
            CellControl_SetData(ctrl, &cd);
        } else {
            // 保留控件缓冲对象，只清数据并标脏。
            ctrl->has_data = false;
            ctrl->data.army_slot_ix = -1;
            ctrl->expanded = CEX_NONE;
            ctrl->hover_item = -1;
            ctrl->melee_pair_pick_request = 0;
            ctrl->move_path_pick_request = 0;
            ctrl->spell_pick_request = 0;
            ctrl->dirty = true;
        }
    }
    s_p.hover_cell = -1;
    s_p.hover_idx = -1;
}

static void LoadSelectedProfileIntoCells_()
{
    const int profile = s_p.selected_profile;
    if (profile < 0 || profile >= PROFILE_COUNT) return;
    // 编号切换/读档后：全量快照与可见行都按该编号草稿重绑。
    for (int i = 0; i < s_p.count; ++i) {
        const int slot = s_p.items[i].army_slot_ix;
        if (slot < 0 || slot >= MAX_STACKS) continue;
        s_p.items[i].rule = s_p.draft_rules[profile][slot];
        CellControl_NormalizeRule(&s_p.items[i].rule, s_p.items[i].creature_type,
            s_p.items[i].is_ranged, s_p.items[i].has_artillery,
            s_p.items[i].has_first_aid);
    }
    RebindVisibleCells_();
}

// 接受战斗结果时随 ClearConfirmedProfiles 调用（ConfigLog 前向声明）：
// 生效方案清空时草稿同步清空——否则下一场打开面板仍显示上一场部队的
// 驻留规则，点「确定」后旧规则复活生效（方案生效≠界面所见）。下一场想
// 沿用配置打开面板即自动载入本场最新存档（§17）。
void ResetPanelDrafts()
{
    const AutoStackRule def = H3AutoPolicy::MakeDefaultRule();
    const SummonProfileFields summon_def =
        H3AutoPolicy::MakeDefaultSummonFields();
    for (int p = 0; p < PROFILE_COUNT; ++p) {
        for (int s = 0; s < MAX_STACKS; ++s)
            s_p.draft_rules[p][s] = def;
        s_p.draft_stop_turns[p] = H3AutoPolicy::DEFAULT_STOP_TURNS;
        s_p.draft_summon[p] = summon_def;
        s_p.draft_status[p] = H3AutoPolicy::MakeDefaultStatusFields();
    }
    LogInfo("[Panel] 草稿已随战斗结果清空（5 套方案+召唤参数）");
}

// ======================================================================
// 本场存档下拉框（§17 智能存读档）：
//   - 「确定」时 AutoExecute 把 5 套方案追加进 <指纹>.json（每场保 30 条）；
//   - 打开面板：若本场已有存档且本会话未载入过 → 自动载最新一条进草稿；
//   - 下拉列出全部存档（时间降序），选中即载入草稿（确定才生效）。
// ======================================================================
static BattleStoreRecord s_battle_records[BATTLE_DD_MAX_ITEMS]; // ~300KB .bss
static int  s_battle_record_count = 0;   // 文件序（升序：末尾=最新）
static unsigned long long s_battle_loaded_fp = 0; // 该指纹本会话已载入
static bool s_battle_dd_open = false;
static int  s_battle_dd_hover = -1;      // 列表 hover（降序下标）
static int  s_battle_dd_sel = -1;        // 当前选中条（降序下标，0=最新）
// 选中项记忆（进程内、与战斗指纹关联）：「确定」时记下当前选中档的
// 时间戳；下次打开同指纹面板按时间戳找回选中项（新档插到末尾不影响
// 旧时间戳）。换战斗指纹不命中 → 默认最新。只两个量，不落盘。
static unsigned long long s_battle_sel_fp = 0;
static char s_battle_sel_time[20] = {}; // "yyyymmdd-hhmmss"

// 降序下标（0=最新）→ 记录指针；越界 nullptr。
static const BattleStoreRecord* BattleRecordAt_(int desc_index)
{
    if (desc_index < 0 || desc_index >= s_battle_record_count) return nullptr;
    return &s_battle_records[s_battle_record_count - 1 - desc_index];
}

// 把一条战斗存档载入面板草稿（5 套全量 + 激活编号；只改显示，确定才生效）。
static void LoadBattleRecordIntoDrafts_(const BattleStoreRecord& rec)
{
    for (int p = 0; p < PROFILE_COUNT; ++p) {
        for (int s = 0; s < MAX_STACKS; ++s)
            s_p.draft_rules[p][s] = rec.rules[p][s];
        s_p.draft_stop_turns[p] = rec.stop_turns[p];
        s_p.draft_summon[p] = rec.summon[p];
        s_p.draft_status[p] = rec.status[p];
    }
    s_p.selected_profile = rec.active;
    s_stop_turns_editing = false;
    CancelManaThEdit_();        // 草稿整体被替换：录入作废
    CancelSummonNumEdit_();     // 草稿整体被替换：录入作废
    CloseSummonDropdowns_();
    PanelCancelAllProtectCountEdits_();
    for (int k = 0; k < CELL_COUNT; ++k) {
        s_p.cells[k].expanded = CEX_NONE;
        s_p.cells[k].dirty = true;
    }
    LoadSelectedProfileIntoCells_();
}

// 刷新本场存档列表 + 首开自动载最新（打开面板时调用，草稿快照之后）。
// 选中项恢复：本场指纹在内存里记过上次「确定」时的选中时间戳 →
// 按时间戳找回对应条（新存档插入到末尾不影响旧时间戳，天然稳定）；
// 找不到（记录被淘汰/首次）→ 默认最新（0）。自动载入仍只发生一次
// （s_battle_loaded_fp 未置位时），选中项每次打开都恢复。
static void RefreshBattleRecordsAndAutoload_()
{
    const unsigned long long fp = GetBattleFingerprint();
    s_battle_dd_open = false;
    s_battle_dd_hover = -1;
    s_battle_dd_sel = -1;
    s_battle_record_count = 0;
    if (!fp) return;
    if (!LoadBattleStore(fp, s_battle_records, BATTLE_DD_MAX_ITEMS,
            &s_battle_record_count))
        return;
    if (s_battle_record_count <= 0) return;

    // 选中项：同指纹且时间戳仍在列表里 → 恢复；否则默认最新。
    int sel = 0;
    if (s_battle_sel_fp == fp && s_battle_sel_time[0]) {
        for (int i = 0; i < s_battle_record_count; ++i) {
            const BattleStoreRecord* rec = BattleRecordAt_(i);
            if (rec && strcmp(rec->time, s_battle_sel_time) == 0) { sel = i; break; }
        }
    }
    s_battle_dd_sel = sel;

    // 首开自动载入：本会话该指纹还没载过 → 载「当前选中项」进草稿。
    if (s_battle_loaded_fp != fp) {
        const BattleStoreRecord* rec = BattleRecordAt_(sel);
        if (rec) {
            LoadBattleRecordIntoDrafts_(*rec);
            s_battle_loaded_fp = fp;
            LogInfo("[Panel] 本场已有 %d 条存档，自动载入 %s（方案%d）",
                s_battle_record_count, rec->time, rec->active + 1);
        }
    }
}

// 切换方案编号：先存回当前编号草稿，再把面板切到新编号的草稿
// （未存档/未读档的编号是默认空配置；切回来草稿仍在）。
// 切换 Tab 页：跨页收尾（提交/收起一切临时编辑态），不动方案草稿。
// 切页 ≠ 切方案：不 Save/Load 草稿，部队页的卡片状态原样保留。
static void SwitchPanelPage_(int page)
{
    if (page < 0 || page >= PAGE_COUNT || page == s_p.active_page) return;
    if (s_stop_turns_editing) CommitStopTurnsEdit_();
    if (s_mana_th_editing) CommitManaThEdit_();
    if (s_status_th_editing) CommitStatusThEdit_(); // 跨页收尾
    CommitSummonNumEdit_();        // 召唤页阈值录入跨页收尾
    CloseSummonDropdowns_();
    PanelCommitAllProtectCountEdits_();
    for (int k = 0; k < CELL_COUNT; ++k) {
        s_p.cells[k].expanded = CEX_NONE;
        s_p.cells[k].dirty = true;
    }
    s_p.hover_cell = -1;
    s_p.hover_idx = -1;
    s_p.active_page = page;
    DrawPanelToBuffer_();
}

static void SelectProfile_(int profile)
{
    if (profile < 0 || profile >= PROFILE_COUNT
        || profile == s_p.selected_profile)
        return;
    SaveCurrentCellsToDraft_();
    if (s_stop_turns_editing) CommitStopTurnsEdit_();
    if (s_mana_th_editing) CommitManaThEdit_(); // 魔力阈值属于旧编号
    if (s_status_th_editing) CommitStatusThEdit_(); // 补状态阈值属于旧编号
    CommitSummonNumEdit_();        // 召唤阈值属于旧编号：切换前提交
    CloseSummonDropdowns_();
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
    s_panel_hidden_for_pick = false;
    s_pick_wait_button_release = false;
    s_melee_pick_cell = -1;
    s_melee_pick_pair = -1;
    s_melee_pick_phase = 0;
    s_melee_pick_stand_hex = -1;
    s_move_pick_cell = -1;
    s_move_pick_wp = -1;
    s_spell_pick_cell = -1;
    s_spell_pick_slot = -1;
    s_help_modal_open = false;
    s_battle_dd_open = false;
    s_battle_dd_hover = -1;
    CancelSummonNumEdit_();
    CloseSummonDropdowns_();
    s_p.active_page = PAGE_ARMY;   // 打开面板默认部队页
    if (!BlockBattleHover_()) {
        s_p.cursor_saved = false;
        LogWarn("[Panel] 无法屏蔽战场悬停，取消打开设置面板。");
        return;
    }
    s_p.active = true;
    s_p.count  = 0;
    memset(s_p.items, 0, sizeof(s_p.items));
    s_p.scroll_row = 0;
    s_p.scroll_dragging = false;
    s_p.scroll_drag_offset = 0;
    s_p.scroll_button_pressed = 0;
    s_p.hover_cell = -1;
    s_p.hover_idx = -1;
    s_p.pressed_button = 0;
    s_p.pressed_profile = -1;
    // 自动选中上次存/读档的编号（INI 记忆，无值默认 1）；不自动读档。
    s_p.selected_profile = g_last_profile;
    if (s_p.selected_profile < 0 || s_p.selected_profile >= PROFILE_COUNT)
        s_p.selected_profile = 0;
    memcpy(s_p.draft_rules, g_profiles, sizeof(s_p.draft_rules));
    memcpy(s_p.draft_stop_turns, g_stop_turns, sizeof(s_p.draft_stop_turns));
    memcpy(s_p.draft_summon, g_summon, sizeof(s_p.draft_summon));
    memcpy(s_p.draft_status, g_status, sizeof(s_p.draft_status));
    s_stop_turns_editing = false;
    CancelManaThEdit_();
    for (int i = 0; i < CELL_COUNT; ++i)
        CellControl_Init(&s_p.cells[i]);

    if (o_WndMgr && o_WndMgr->screenPcx16) {
        s_p.x = (o_WndMgr->screenPcx16->width  - PANEL_W) / 2;
        s_p.y = (o_WndMgr->screenPcx16->height - PANEL_H) / 2 - 40;
    } else {
        s_p.x = (800 - PANEL_W) / 2; s_p.y = (600 - PANEL_H) / 2 - 40;
    }
    if (s_p.x < 0) s_p.x = 0; if (s_p.y < 0) s_p.y = 0;

    H3CombatManager* mgr = GetCombatMgr();
    LogDebug("[Panel] 打开阶段：开始枚举部队");
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
                cd.is_ranged = is_ranged;
                // 炮术/急救术：分别决定弩车·箭塔 / 帐篷是否可选手动。
                cd.has_artillery = hero
                    && hero->secSkill[eSecondary::ARTILLERY] > 0;
                cd.has_first_aid = hero
                    && hero->secSkill[eSecondary::FIRST_AID] > 0;
                CellControl_NormalizeRule(&cd.rule, cd.creature_type, is_ranged,
                    cd.has_artillery, cd.has_first_aid);
                s_p.items[s_p.count] = cd;
                ++s_p.count;
            }
        }
        // 草稿快照口径（方案生效范围=面板所见）：仅本场面板部队（含阵亡的
        // 己方非召唤部队）保留草稿，其余槽位一律回默认——上一场战斗/已移除
        // 部队的驻留规则不再随「确定」生效，也不会被存档按钮写进 ini。
        int cleared = 0;
        for (int p = 0; p < PROFILE_COUNT; ++p) {
            for (int i = 0; i < MAX_STACKS; ++i) {
                if (IsConfigurablePanelStack_(mgr->stacks[side][i], hero))
                    continue;
                const AutoStackRule& d = s_p.draft_rules[p][i];
                if (d.action != AA_MANUAL || d.spellSlotCount > 0) ++cleared;
                s_p.draft_rules[p][i] = H3AutoPolicy::MakeDefaultRule();
            }
        }
        if (cleared > 0)
            LogInfo("[Panel] 草稿快照：清掉 %d 条非本场部队驻留规则", cleared);
    }
    // 本场存档下拉数据 + 首开自动载最新（在草稿快照之后覆盖草稿）。
    RefreshBattleRecordsAndAutoload_();
    RebindVisibleCells_();
    LogDebug("[Panel] 打开阶段：部队枚举完成 count=%d", s_p.count);
    InstallBattleInputBlocker_();
    LogDebug("[Panel] 打开阶段：输入屏障完成");
    EnsurePanelButtonPcxResources_();
    ForcePanelDefaultCursor_();
    LogDebug("[Panel] 打开阶段：开始绘制");
    DrawPanelToBuffer_();
    LogDebug("[Panel] 打开阶段：绘制完成");
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
    LogDebug("[Panel] 打开设置面板 count=%d at (%d,%d) s_p=%p",
        s_p.count, s_p.x, s_p.y, &s_p);
}

void RefreshSettingsPanel() { if (s_p.active) DrawPanelToBuffer_(); }

static void CommitAndCloseSettingsPanel_()
{
    if (!s_p.active) return;

    // 保存当前表格到当前编号草稿，再一次性提交全部 5 套（选中编号生效）。
    // 保活方式在 AutoStackRule 内随 draft_rules 一起提交；
    // 召唤启用/阈值与停止阈值是方案级，随 draft 数组提交。
    SaveCurrentCellsToDraft_();
    CommitSummonNumEdit_();        // 召唤阈值录入随勾号提交
    CloseSummonDropdowns_();
    // out_store_added：内容有变、磁盘上真的新增了一条。内容未变时不新增，
    // 选中项保持玩家当前选的那条。
    bool store_added = false;
    CommitProfiles(s_p.selected_profile, s_p.draft_rules,
        s_p.draft_stop_turns, s_p.draft_summon, s_p.draft_status,
        &store_added);
    // 编号记忆随勾号生效写入（存档/读档只动草稿，不记编号）。
    RememberProfileSlot(s_p.selected_profile);
    // 本场存档下拉：新增时立刻重读内存列表并切到新档（降序 0=最新），
    // 不等下次打开面板——否则右上角条数不增加、选中项还停在旧档。
    // 选中项记忆（与战斗指纹关联，下次打开按时间戳恢复选中）。
    {
        const unsigned long long fp = GetBattleFingerprint();
        if (fp && store_added) {
            if (LoadBattleStore(fp, s_battle_records, BATTLE_DD_MAX_ITEMS,
                    &s_battle_record_count) && s_battle_record_count > 0)
                s_battle_dd_sel = 0;
        }
        const BattleStoreRecord* sel = BattleRecordAt_(s_battle_dd_sel);
        if (fp && sel) {
            s_battle_sel_fp = fp;
            strncpy(s_battle_sel_time, sel->time, sizeof(s_battle_sel_time) - 1);
            s_battle_sel_time[sizeof(s_battle_sel_time) - 1] = 0;
        }
    }
    SyncActiveProtect();
    PauseAutoExecution();
    CloseSettingsPanel();
}

void CloseSettingsPanel()
{
    if (!s_p.active) return;
    s_p.active = false;
    s_panel_modal_suspended = false;
    s_melee_pick_phase = 0;
    s_melee_pick_cell = -1;
    s_melee_pick_pair = -1;
    s_melee_pick_stand_hex = -1;
    s_panel_hidden_for_pick = false;
    s_pick_wait_button_release = false;
    s_move_pick_cell = -1;
    s_move_pick_wp = -1;
    s_spell_pick_cell = -1;
    s_spell_pick_slot = -1;
    s_help_modal_open = false;
    s_battle_dd_open = false;
    s_battle_dd_hover = -1;
    if (s_stop_turns_editing) CancelStopTurnsEdit_();
    if (s_mana_th_editing) CancelManaThEdit_();
    if (s_status_th_editing) CancelStatusThEdit_();
    CancelSummonNumEdit_();
    CloseSummonDropdowns_();
    PanelCancelAllProtectCountEdits_();
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
        // 打开时鼠标停在自动战斗按钮上，保存的是按钮光标；
        // 关闭后鼠标回到战场，应恢复战场默认光标而不是按钮光标。
        if (H3MouseManager* mouse = H3MouseManager::Get())
            mouse->DefaultCursor();
        s_p.cursor_saved = false;
    }
    ReleasePanelComposite_();
    if (H3CombatManager* mgr = GetCombatMgr())
        THISCALL_7(void, 0x493FC0, mgr, FALSE, TRUE, FALSE, 0, TRUE, FALSE);
    LogInfo("[Panel] 设置面板已关闭。");
}

bool IsPanelActive() { return s_p.active; }

// ========================================================================
// 第八部分：点击处理
// ========================================================================

bool HandlePanelClick(int sx, int sy)
{
    if (!s_p.active) return false;
    if (s_p.active_page != PAGE_ARMY) return false; // 召唤页无卡片
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
        CellControl_GetExpandRectForCtrl(ctrl, cRc.left, cRc.top, &dropRc);

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
    if (s_spell_pick_cell >= 0) return false;
    const int first_item = s_p.scroll_row * COLS;
    int new_hover_cell = -1, new_hover_idx = -1;
    for (int i = 0; i < CELL_COUNT; ++i) {
        const int item_index = first_item + i;
        if (item_index >= s_p.count) break;
        CellControl* ctrl = &s_p.cells[i];
        if (ctrl->expanded == CEX_NONE) continue;
        RECT cRc = CellRect(i);
        // 按当前展开类型的真实列表矩形判定（行动=小列2，选择器/阵营=小列3）。
        // 旧逻辑写死 CC_COMBO_X，导致右侧选择器永远无 hover 高亮。
        RECT dropRc = {};
        if (!CellControl_GetExpandRectForCtrl(ctrl, cRc.left, cRc.top, &dropRc))
            continue;
        if (!PointInRect_(px, py, dropRc.left, dropRc.top,
            dropRc.right - dropRc.left, dropRc.bottom - dropRc.top)) {
            // 光标不在该展开列表内：清除 hover，继续看其它展开项。
            continue;
        }
        const int lx = px - cRc.left;
        const int ly = py - cRc.top;
        // 站立/攻击下拉返回绝对项索引（含滚动）；其它下拉等同可见索引。
        const int idx = CellControl_HitExpandIndex(ctrl, lx, ly);
        new_hover_cell = i;
        new_hover_idx = idx;
        break;
    }
    bool changed = false;
    if (new_hover_cell != s_p.hover_cell || new_hover_idx != s_p.hover_idx) {
        s_p.hover_cell = new_hover_cell;
        s_p.hover_idx = new_hover_idx;
        changed = true;
    }
    // 本场存档下拉同款：钩子即时更新悬停行（时间降序下标）。
    if (s_battle_dd_open) {
        int new_battle_hover = -1;
        for (int i = 0; i < s_battle_record_count; ++i) {
            const int iy = BATTLE_DD_LIST_Y + i * BATTLE_DD_ITEM_H;
            if (PointInRect_(px, py, BATTLE_DD_LIST_X, iy,
                    BATTLE_DD_LIST_W, BATTLE_DD_ITEM_H)) {
                new_battle_hover = i;
                break;
            }
        }
        if (new_battle_hover != s_battle_dd_hover) {
            s_battle_dd_hover = new_battle_hover;
            changed = true;
        }
    } else if (s_battle_dd_hover != -1) {
        s_battle_dd_hover = -1;
        changed = true;
    }
    // 召唤页三个下拉（法术/条件组合/召唤物行动）同卡片下拉：钩子即时更新悬停项。
    if (s_summon_spell_dd_open) {
        int new_sum_hover = -1;
        for (int i = 0; i <= 4; ++i) {
            int ix = 0, iy = 0, iw = 0, ih = 0;
            GetSummonSpellDdItemRect_(i, &ix, &iy, &iw, &ih);
            if (PointInRect_(px, py, ix, iy, iw, ih)) {
                new_sum_hover = i;
                break;
            }
        }
        if (new_sum_hover != s_summon_spell_dd_hover) {
            s_summon_spell_dd_hover = new_sum_hover;
            changed = true;
        }
    }
    if (s_summon_cond_dd_open) {
        int new_cond_hover = -1;
        for (int i = 0; i < 2; ++i) {
            int ix = 0, iy = 0, iw = 0, ih = 0;
            GetSummonCondDdItemRect_(i, &ix, &iy, &iw, &ih);
            if (PointInRect_(px, py, ix, iy, iw, ih)) {
                new_cond_hover = i;
                break;
            }
        }
        if (new_cond_hover != s_summon_cond_dd_hover) {
            s_summon_cond_dd_hover = new_cond_hover;
            changed = true;
        }
    }
    if (s_summon_act_dd_open) {
        int new_act_hover = -1;
        AutoActionKind acts[4] = {};
        const int n = H3AutoPolicy::GetAllowedSummonActions(acts);
        for (int i = 0; i < n; ++i) {
            int ix = 0, iy = 0, iw = 0, ih = 0;
            GetSummonActDdItemRect_(i, &ix, &iy, &iw, &ih);
            if (PointInRect_(px, py, ix, iy, iw, ih)) {
                new_act_hover = i;
                break;
            }
        }
        if (new_act_hover != s_summon_act_dd_hover) {
            s_summon_act_dd_hover = new_act_hover;
            changed = true;
        }
    }
    if (s_status_dd_open >= 0) {
        int x = 0, y = 0, hover = -1;
        StatusSlotRect_(s_status_dd_open, &x, &y);
        for (int i = 0; i < s_status_dd_count; ++i) {
            const int iy = y + STATUS_DD_H + i * STATUS_ITEM_H;
            if (PointInRect_(px, py, x, iy, STATUS_DD_W, STATUS_ITEM_H)) {
                hover = i;
                break;
            }
        }
        if (hover != s_status_dd_hover) {
            s_status_dd_hover = hover;
            changed = true;
        }
    }
    if (changed) {
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

    // 帮助模态框打开时：吞掉所有底层点击，仅允许关闭/日志级别/打包日志。
    if (s_help_modal_open) {
        if (raw_command == 16) {
            // 展开的级别列表优先：命中选项即切换级别；点外部收起。
            if (s_help_log_dd_open) {
                bool picked = false;
                for (int i = 0; i < 5; ++i) {
                    int ix = 0, iy = 0, iw = 0, ih = 0;
                    GetHelpLogLevelItemRect_(i, &ix, &iy, &iw, &ih);
                    if (!PointInRect_(px, py, ix, iy, iw, ih)) continue;
                    picked = true;
                    s_help_log_dd_open = false;
                    if (i != g_log_level) {
                        static const char* const kNames[5] = {
                            "trace", "debug", "info", "warn", "error",
                        };
                        g_log_level = i;
                        IniWriteKeyUtf8(g_user_ini_path, "Logging", "MinLevel", kNames[i]);
                        char msg[128];
                        _snprintf(msg, sizeof(msg) - 1, T("help.log_level_set"),
                            kNames[i]);
                        msg[sizeof(msg) - 1] = 0;
                        SetStatusText_(msg, 4000);
                        LogInfo("[Config] 日志级别切换为 %s（已写入 user.ini）", kNames[i]);
                    }
                    DrawPanelToBuffer_();
                    break;
                }
                if (!picked) s_help_log_dd_open = false;
                return; // 展开期间吞掉其余点击（含关闭按钮，先收起）
            }
            // 日志级别下拉框：开/关。
            {
                int dx = 0, dy = 0, dw = 0, dh = 0;
                GetHelpLogLevelDdRect_(&dx, &dy, &dw, &dh);
                if (PointInRect_(px, py, dx, dy, dw, dh)) {
                    s_help_log_dd_open = true;
                    DrawPanelToBuffer_();
                    return;
                }
            }
            // 打包日志按钮。
            {
                int bx = 0, by = 0, bw = 0, bh = 0;
                GetHelpPackBtnRect_(&bx, &by, &bw, &bh);
                if (PointInRect_(px, py, bx, by, bw, bh)) {
                    // 提示不带文件路径，out_path 只供函数内部写盘与复制，传空。
                    char reason[192] = {};
                    if (PackRecentLogs_(nullptr, 0,
                            reason, sizeof(reason))) {
                        // 系统对话框：文本可选中复制（玩家要复制 QQ 号），
                        // 自绘状态栏/模态文字都做不到复制。缓冲按文案键上限
                        // （覆盖缓冲 512）+ 路径留余量，不能只给 MAX_PATH+256。
                        char msg[1024];
                        _snprintf(msg, sizeof(msg) - 1, "%s", T("help.pack_ok"));
                        msg[sizeof(msg) - 1] = 0;
                        // ini 值写不了真换行：| 记换行位，弹窗前还原。
                        for (char* c = msg; *c; ++c) if (*c == '|') *c = '\n';
                        wchar_t wmsg[1024] = {}, wtitle[64] = {};
                        MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg, 1024);
                        MultiByteToWideChar(CP_UTF8, 0, T("help.pack_title"), -1, wtitle, 64);
                        MessageBoxW(nullptr, wmsg, wtitle, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
                    } else {
                        char msg[512];
                        _snprintf(msg, sizeof(msg) - 1, T("help.pack_fail"), reason);
                        msg[sizeof(msg) - 1] = 0;
                        // ini 值写不了真换行：| 记换行位，弹窗前还原。
                        for (char* c = msg; *c; ++c) if (*c == '|') *c = '\n';
                        wchar_t wmsg[512] = {}, wtitle[32] = {};
                        MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg, 512);
                        MultiByteToWideChar(CP_UTF8, 0, T("help.pack_title"), -1, wtitle, 32);
                        MessageBoxW(nullptr, wmsg, wtitle, MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
                        LogWarn("[LogPack] 打包失败：%s", reason);
                    }
                    return;
                }
            }
            // 开源地址行（纯文本样式）：点击复制到剪贴板，成功后地址行
            // 原位闪绿 1.2 秒（状态栏在帮助模态的遮暗层下，不够醒目）。
            {
                int lx = 0, ly = 0, lw = 0, lh = 0;
                GetHelpLinkRect_(&lx, &ly, &lw, &lh);
                if (PointInRect_(px, py, lx, ly, lw, lh)) {
                    const char* url = T("help.link_url");
                    const bool ok = CopyTextToClipboard_(url);
                    if (ok) s_help_link_flash_tick = GetTickCount();
                    SetStatusText_(T(ok ? "help.link_copied" : "help.link_fail"),
                        3000);
                    LogInfo("[Panel] %s开源地址 %s",
                        ok ? "已复制" : "复制失败", url);
                    DrawPanelToBuffer_();
                    return;
                }
            }
            int bx = 0, by = 0, bw = 0, bh = 0;
            GetHelpModalCloseRect_(&bx, &by, &bw, &bh);
            if (PointInRect_(px, py, bx, by, bw, bh)) {
                s_help_modal_open = false;
                DrawPanelToBuffer_();
            }
        }
        return;
    }

    // 停止回合录入中：点框外提交；点框内（按下）把光标定位到最近字符边界。
    if (s_stop_turns_editing && (raw_command == 8 || raw_command == 16)) {
        if (!PointInRect_(px, py, STOP_BOX_X, STOP_ROW_Y,
                STOP_BOX_W, STOP_ROW_H) && raw_command == 16) {
            CommitStopTurnsEdit_();
            DrawPanelToBuffer_();
        } else if (raw_command == 8
            && PointInRect_(px, py, STOP_BOX_X, STOP_ROW_Y,
                STOP_BOX_W, STOP_ROW_H)) {
            H3Font* fnt = GetSmallFont();
            const int text_x = STOP_BOX_X + 8; // 与绘制端一致
            const int len = (int)strlen(s_stop_turns_text);
            int best = len, best_dist = 0x7FFFFFFF;
            for (int i = 0; i <= len; ++i) {
                char prefix[8] = {};
                if (i > 0) memcpy(prefix, s_stop_turns_text, i);
                const int bx = text_x
                    + (fnt ? fnt->GetMaxLineWidth(prefix) : 0);
                int dist = px - bx;
                if (dist < 0) dist = -dist;
                if (dist < best_dist) { best_dist = dist; best = i; }
            }
            if (best != s_stop_turns_caret) {
                s_stop_turns_caret = best;
                s_stop_turns_caret_tick = GetTickCount();
                DrawPanelToBuffer_();
            }
        }
        if (raw_command == 16 && PanelAnyProtectCountEditing_()
            && !s_cnt_lb_in_box) {
            PanelCommitAllProtectCountEdits_();
            DrawPanelToBuffer_();
        }
        return;
    }

    // 敌方魔力阈值录入中：与停止回合同款（点外提交、点内按光标定位）。
    if (s_mana_th_editing && (raw_command == 8 || raw_command == 16)) {
        if (!PointInRect_(px, py, STOP_MANA_TH_BOX_X, STOP_ROW_Y,
                STOP_MANA_TH_BOX_W, STOP_ROW_H) && raw_command == 16) {
            CommitManaThEdit_();
            DrawPanelToBuffer_();
        } else if (raw_command == 8
            && PointInRect_(px, py, STOP_MANA_TH_BOX_X, STOP_ROW_Y,
                STOP_MANA_TH_BOX_W, STOP_ROW_H)) {
            H3Font* fnt = GetSmallFont();
            const int text_x = STOP_MANA_TH_BOX_X + 8; // 与绘制端一致
            const int len = (int)strlen(s_mana_th_text);
            int best = len, best_dist = 0x7FFFFFFF;
            for (int i = 0; i <= len; ++i) {
                char prefix[8] = {};
                if (i > 0) memcpy(prefix, s_mana_th_text, i);
                const int bx = text_x
                    + (fnt ? fnt->GetMaxLineWidth(prefix) : 0);
                int dist = px - bx;
                if (dist < 0) dist = -dist;
                if (dist < best_dist) { best_dist = dist; best = i; }
            }
            if (best != s_mana_th_caret) {
                s_mana_th_caret = best;
                s_mana_th_caret_tick = GetTickCount();
                DrawPanelToBuffer_();
            }
        }
        if (raw_command == 16 && PanelAnyProtectCountEditing_()
            && !s_cnt_lb_in_box) {
            PanelCommitAllProtectCountEdits_();
            DrawPanelToBuffer_();
        }
        return;
    }

    // 补状态阈值录入中：与敌方魔力阈值同款（点外提交、点内按光标定位）。
    if (s_status_th_editing && (raw_command == 8 || raw_command == 16)) {
        if (!PointInRect_(px, py, STATUS_TH_BOX_X, STATUS_TH_ROW_Y,
                STATUS_TH_BOX_W, STATUS_TH_ROW_H) && raw_command == 16) {
            CommitStatusThEdit_();
            DrawPanelToBuffer_();
        } else if (raw_command == 8
            && PointInRect_(px, py, STATUS_TH_BOX_X, STATUS_TH_ROW_Y,
                STATUS_TH_BOX_W, STATUS_TH_ROW_H)) {
            H3Font* fnt = GetSmallFont();
            const int text_x = STATUS_TH_BOX_X + 8; // 与绘制端一致
            const int len = (int)strlen(s_status_th_text);
            int best = len, best_dist = 0x7FFFFFFF;
            for (int i = 0; i <= len; ++i) {
                char prefix[8] = {};
                if (i > 0) memcpy(prefix, s_status_th_text, i);
                const int bx = text_x
                    + (fnt ? fnt->GetMaxLineWidth(prefix) : 0);
                int dist = px - bx;
                if (dist < 0) dist = -dist;
                if (dist < best_dist) { best_dist = dist; best = i; }
            }
            if (best != s_status_th_caret) {
                s_status_th_caret = best;
                s_status_th_caret_tick = GetTickCount();
                DrawPanelToBuffer_();
            }
        }
        if (raw_command == 16 && PanelAnyProtectCountEditing_()
            && !s_cnt_lb_in_box) {
            PanelCommitAllProtectCountEdits_();
            DrawPanelToBuffer_();
        }
        return;
    }

    // 召唤页阈值录入中：与停止回合同款（点外提交、点内按光标定位）。
    if (s_summon_edit_which != SUMMON_EDIT_NONE
        && (raw_command == 8 || raw_command == 16)) {
        int box_x = 0, box_y = 0, box_w = 0, box_h = 0;
        SummonNumBoxRect_(s_summon_edit_which, &box_x, &box_y, &box_w, &box_h);
        if (!PointInRect_(px, py, box_x, box_y, box_w, box_h)
            && raw_command == 16) {
            CommitSummonNumEdit_();
            DrawPanelToBuffer_();
        } else if (raw_command == 8
            && PointInRect_(px, py, box_x, box_y, box_w, box_h)) {
            H3Font* fnt = GetSmallFont();
            const int text_x = SummonNumBoxTextX_(s_summon_edit_which);
            const int len = (int)strlen(s_summon_edit_text);
            int best = len, best_dist = 0x7FFFFFFF;
            for (int i = 0; i <= len; ++i) {
                char prefix[16] = {};
                if (i > 0) memcpy(prefix, s_summon_edit_text, i);
                const int bx = text_x
                    + (fnt ? fnt->GetMaxLineWidth(prefix) : 0);
                int dist = px - bx;
                if (dist < 0) dist = -dist;
                if (dist < best_dist) { best_dist = dist; best = i; }
            }
            if (best != s_summon_edit_caret) {
                s_summon_edit_caret = best;
                s_summon_edit_caret_tick = GetTickCount();
                DrawPanelToBuffer_();
            }
        }
        return;
    }

    // 本场存档下拉展开时：点历史档即载入草稿、点按钮收起、点外收起
    //（与召唤下拉同款语义；优先处理防穿透）。
    if (s_battle_dd_open) {
        if (raw_command == 4)
            return;
        if (raw_command == 8 || raw_command == 16) {
            for (int i = 0; i < s_battle_record_count; ++i) {
                const int iy = BATTLE_DD_LIST_Y + i * BATTLE_DD_ITEM_H;
                if (PointInRect_(px, py, BATTLE_DD_LIST_X, iy,
                        BATTLE_DD_LIST_W, BATTLE_DD_ITEM_H)) {
                    const BattleStoreRecord* rec = BattleRecordAt_(i);
                    if (rec) {
                        LoadBattleRecordIntoDrafts_(*rec);
                        s_battle_dd_sel = i;
                        s_battle_loaded_fp = GetBattleFingerprint();
                        SetStatusText_(T("panel.status_battle_loaded"), 4000);
                        LogInfo("[Panel] 载入本场存档 %s（方案%d，未确定不生效）",
                            rec->time, rec->active + 1);
                    }
                    s_battle_dd_open = false;
                    s_battle_dd_hover = -1;
                    DrawPanelToBuffer_();
                    return;
                }
            }
            if (PointInRect_(px, py, BATTLE_DD_X, BATTLE_DD_Y,
                    BATTLE_DD_W, BATTLE_DD_H)) {
                if (raw_command == 8) {
                    s_battle_dd_open = false;
                    s_battle_dd_hover = -1;
                    DrawPanelToBuffer_();
                }
                return;
            }
            s_battle_dd_open = false;
            s_battle_dd_hover = -1;
            DrawPanelToBuffer_();
            return;
        }
    }

    // 召唤页三个下拉展开时：选项即选、点框保持/收起、点外收起
    // （与保活下拉完全同款语义）。
    if (s_summon_spell_dd_open || s_summon_cond_dd_open
        || s_summon_act_dd_open) {
        if (raw_command == 4)
            return;
        if (raw_command == 8 || raw_command == 16) {
            if (s_summon_spell_dd_open) {
                for (int i = 0; i <= 4; ++i) {
                    int ix = 0, iy = 0, iw = 0, ih = 0;
                    GetSummonSpellDdItemRect_(i, &ix, &iy, &iw, &ih);
                    if (PointInRect_(px, py, ix, iy, iw, ih)) {
                        s_p.draft_summon[s_p.selected_profile].spell_pick = i;
                        s_summon_spell_dd_open = false;
                        s_summon_spell_dd_hover = -1;
                        LogInfo("[Panel] 召唤法术=%d (方案%d)", i,
                            s_p.selected_profile + 1);
                        DrawPanelToBuffer_();
                        return;
                    }
                }
                if (PointInRect_(px, py, SUMMON_DD_X, SUMMON_SPELL_Y,
                        SUMMON_DD_W, SUMMON_DD_H)) {
                    if (raw_command == 8) {
                        s_summon_spell_dd_open = false;
                        s_summon_spell_dd_hover = -1;
                        DrawPanelToBuffer_();
                    }
                    return;
                }
                s_summon_spell_dd_open = false;
                s_summon_spell_dd_hover = -1;
                DrawPanelToBuffer_();
                return;
            }
            if (s_summon_cond_dd_open) {
                for (int i = 0; i < 2; ++i) {
                    int ix = 0, iy = 0, iw = 0, ih = 0;
                    GetSummonCondDdItemRect_(i, &ix, &iy, &iw, &ih);
                    if (PointInRect_(px, py, ix, iy, iw, ih)) {
                        s_p.draft_summon[s_p.selected_profile].cond_combine
                            = (i == H3AutoPolicy::SUMMON_COMBINE_OR)
                            ? H3AutoPolicy::SUMMON_COMBINE_OR
                            : H3AutoPolicy::SUMMON_COMBINE_AND;
                        s_summon_cond_dd_open = false;
                        s_summon_cond_dd_hover = -1;
                        LogInfo("[Panel] 召唤条件组合=%d (方案%d)", i,
                            s_p.selected_profile + 1);
                        DrawPanelToBuffer_();
                        return;
                    }
                }
                if (PointInRect_(px, py, SUMMON_CB_DD_X, SUMMON_ROW2_Y,
                        SUMMON_CB_DD_W, SUMMON_DD_H)) {
                    if (raw_command == 8) {
                        s_summon_cond_dd_open = false;
                        s_summon_cond_dd_hover = -1;
                        DrawPanelToBuffer_();
                    }
                    return;
                }
                s_summon_cond_dd_open = false;
                s_summon_cond_dd_hover = -1;
                DrawPanelToBuffer_();
                return;
            }
            if (s_summon_act_dd_open) {
                AutoActionKind acts[4] = {};
                const int n = H3AutoPolicy::GetAllowedSummonActions(acts);
                for (int i = 0; i < n; ++i) {
                    int ix = 0, iy = 0, iw = 0, ih = 0;
                    GetSummonActDdItemRect_(i, &ix, &iy, &iw, &ih);
                    if (PointInRect_(px, py, ix, iy, iw, ih)) {
                        AutoStackRule& rule =
                            s_p.draft_summon[s_p.selected_profile].summon_rule;
                        rule.action = acts[i];
                        H3AutoPolicy::NormalizeSummonRule(&rule); // 顺带清降级
                        s_summon_act_dd_open = false;
                        s_summon_act_dd_hover = -1;
                        LogInfo("[Panel] 召唤物行动=%d (方案%d)", (int)acts[i],
                            s_p.selected_profile + 1);
                        DrawPanelToBuffer_();
                        return;
                    }
                }
                if (PointInRect_(px, py, SUMMON_ACT_DD_X, SUMMON_ACT_DD_Y,
                        SUMMON_ACT_DD_W, SUMMON_ACT_DD_H)) {
                    if (raw_command == 8) {
                        s_summon_act_dd_open = false;
                        s_summon_act_dd_hover = -1;
                        DrawPanelToBuffer_();
                    }
                    return;
                }
                s_summon_act_dd_open = false;
                s_summon_act_dd_hover = -1;
                DrawPanelToBuffer_();
                return;
            }
        }
        return;
    }

    // 快捷键录入模态框打开时：吞掉所有底层点击，仅允许点取消。
    if (s_spell_pick_cell >= 0) {
        if (raw_command == 16) {
            int bx = 0, by = 0, bw = 0, bh = 0;
            GetSpellKeyModalCancelRect_(&bx, &by, &bw, &bh);
            if (PointInRect_(px, py, bx, by, bw, bh))
                EndSpellPick_();
        }
        return;
    }

    // 右键松开：循环施法 / 循环移动 / 循环近战 已有槽位直接删除。
    // （召唤页没有卡片，右键直接吞掉。）
    if (raw_command == 64
        || raw_command == static_cast<int>(eMsgCommand::RBUTTON_UP)) {
        // 保持状态页：右键已有下拉框删除该项，后面的项和「+」前移。
        if (s_p.active_page == PAGE_STATUS) {
            StatusProfileFields& status =
                s_p.draft_status[s_p.selected_profile];
            for (int i = 0; i < status.slot_count; ++i) {
                int x = 0, y = 0;
                StatusSlotRect_(i, &x, &y);
                if (!PointInRect_(px, py, x, y, STATUS_DD_W, STATUS_DD_H))
                    continue;
                for (int n = i; n + 1 < status.slot_count; ++n)
                    status.slots[n] = status.slots[n + 1];
                status.slots[--status.slot_count] = 0;
                s_status_dd_open = -1;
                s_status_dd_hover = -1;
                LogInfo("[Panel] 保持状态移除第%d项 (方案%d)",
                    i + 1, s_p.selected_profile + 1);
                DrawPanelToBuffer_();
                return;
            }
            return;
        }
        if (s_p.active_page != PAGE_ARMY) return;
        const int first_item = s_p.scroll_row * COLS;
        for (int i = 0; i < CELL_COUNT; ++i) {
            const int item_index = first_item + i;
            if (item_index >= s_p.count) break;
            RECT cRc = CellRect(i);
            if (!PointInRect_(px, py, cRc.left, cRc.top, CELL_W, CELL_H))
                continue;
            CellControl* ctrl = &s_p.cells[i];
            if (CellControl_OnMouse(ctrl, 5,
                    px - cRc.left, py - cRc.top, false)) {
                DrawPanelToBuffer_();
            }
            return;
        }
        return;
    }

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

    // 保持状态下拉展开时：选项即选、点框保持/收起、点外收起
    // （与召唤页下拉完全同款语义）。
    if (s_p.active_page == PAGE_STATUS && s_status_dd_open >= 0) {
        if (raw_command == 4)
            return;
        if (raw_command == 8 || raw_command == 16) {
            StatusProfileFields& status =
                s_p.draft_status[s_p.selected_profile];
            int x = 0, y = 0;
            StatusSlotRect_(s_status_dd_open, &x, &y);
            for (int i = 0; i < s_status_dd_count; ++i) {
                const int iy = y + STATUS_DD_H + i * STATUS_ITEM_H;
                if (!PointInRect_(px, py, x, iy, STATUS_DD_W, STATUS_ITEM_H))
                    continue;
                if (s_status_dd_open < status.slot_count)
                    status.slots[s_status_dd_open] = s_status_dd_ids[i];
                LogInfo("[Panel] 保持状态选择 slot=%d spell=%d (方案%d)",
                    s_status_dd_open + 1, s_status_dd_ids[i],
                    s_p.selected_profile + 1);
                s_status_dd_open = -1;
                s_status_dd_hover = -1;
                DrawPanelToBuffer_();
                return;
            }
            if (PointInRect_(px, py, x, y, STATUS_DD_W, STATUS_DD_H)) {
                if (raw_command == 8) {
                    s_status_dd_open = -1;
                    s_status_dd_hover = -1;
                    DrawPanelToBuffer_();
                }
                return;
            }
            s_status_dd_open = -1;
            s_status_dd_hover = -1;
            DrawPanelToBuffer_();
            return;
        }
        return;
    }

    if (raw_command == 8) {
        s_cnt_lb_in_box = false; // 按下是否落在「剩≤」框内，由命中测试回填
        // 左侧 Tab 导航：按下即切页（各页内容互不重叠，无穿透问题）。
        for (int i = 0; i < TAB_VISIBLE_COUNT; ++i) {
            const int page = kTabOrder[i];
            const int ty = TAB_FIRST_Y + i * (TAB_ITEM_H + TAB_GAP);
            if (PointInRect_(px, py, TAB_X, ty, TAB_ITEM_W, TAB_ITEM_H)) {
                SwitchPanelPage_(page);
                return;
            }
        }
        if (PointInRect_(px, py, STOP_BOX_X, STOP_ROW_Y,
                STOP_BOX_W, STOP_ROW_H)) {
            // 进入编辑：预填当前草稿值（值 0 预填空，避免「点击即变 0」），
            // 光标在末尾。已在编辑态时点框内不再重置。
            if (!s_stop_turns_editing) {
                s_stop_turns_editing = true;
                const int cur = (int)s_p.draft_stop_turns[s_p.selected_profile];
                s_stop_turns_text[0] = 0;
                if (cur > 0)
                    _snprintf(s_stop_turns_text, sizeof(s_stop_turns_text),
                        "%d", cur);
                s_stop_turns_caret = (int)strlen(s_stop_turns_text);
                s_stop_turns_caret_tick = GetTickCount();
                DrawPanelToBuffer_();
            }
            return;
        }

        // 「敌方魔力≤[框] 时停」阈值框：与停止框同款进入编辑
        // （预填当前值、光标在末尾；已在编辑态点框内不重置）。
        if (PointInRect_(px, py, STOP_MANA_TH_BOX_X, STOP_ROW_Y,
                STOP_MANA_TH_BOX_W, STOP_ROW_H)) {
            if (!s_mana_th_editing) {
                s_mana_th_editing = true;
                const int cur =
                    s_p.draft_summon[s_p.selected_profile].stop_mana_th;
                s_mana_th_text[0] = 0;
                if (cur > 0)
                    _snprintf(s_mana_th_text, sizeof(s_mana_th_text),
                        "%d", cur);
                s_mana_th_caret = (int)strlen(s_mana_th_text);
                s_mana_th_caret_tick = GetTickCount();
                DrawPanelToBuffer_();
            }
            return;
        }

        // 「[✓] 敌方魔力≤N 时停」复选框（命中=框+「敌方魔力≤」文字；
        // 点阈值框是录入，不切勾选）。
        if (PointInRect_(px, py, STOP_MANA_X, STOP_ROW_Y,
                STOP_MANA_HIT_W, STOP_ROW_H)) {
            SummonProfileFields& sf =
                s_p.draft_summon[s_p.selected_profile];
            sf.stop_enemy_mana = sf.stop_enemy_mana ? 0 : 1;
            LogInfo("[Panel] 敌方魔力停止=%d 阈值=%d (方案%d)",
                sf.stop_enemy_mana, sf.stop_mana_th,
                s_p.selected_profile + 1);
            DrawPanelToBuffer_();
            return;
        }

        // 保持状态页：点已有槽位展开下拉换法术；点「＋」追加一个槽并展开。
        if (s_p.active_page == PAGE_STATUS) {
            StatusProfileFields& status =
                s_p.draft_status[s_p.selected_profile];
            // 「剩余回合≤[框] 时补」阈值框：与其它数字框同款进入编辑
            // （预填当前值、光标在末尾；已在编辑态点框内不重置）。
            if (PointInRect_(px, py, STATUS_TH_BOX_X, STATUS_TH_ROW_Y,
                    STATUS_TH_BOX_W, STATUS_TH_ROW_H)) {
                if (!s_status_th_editing) {
                    s_status_th_editing = true;
                    const int cur = status.refresh_turns;
                    s_status_th_text[0] = 0;
                    if (cur > 0)
                        _snprintf(s_status_th_text,
                            sizeof(s_status_th_text), "%d", cur);
                    s_status_th_caret = (int)strlen(s_status_th_text);
                    s_status_th_caret_tick = GetTickCount();
                    DrawPanelToBuffer_();
                }
                return;
            }
            bool handled = false;
            for (int i = 0; i < status.slot_count && !handled; ++i) {
                int x = 0, y = 0;
                StatusSlotRect_(i, &x, &y);
                if (!PointInRect_(px, py, x, y, STATUS_DD_W, STATUS_DD_H))
                    continue;
                handled = true;
                if (BuildStatusCandidates_(status, i) > 0) {
                    s_status_dd_open = i;
                    s_status_dd_hover = -1;
                } else {
                    SetStatusText_(T("panel.status_empty"), 3000);
                }
            }
            if (!handled) {
                int add_x = 0, add_y = 0;
                StatusSlotRect_(status.slot_count, &add_x, &add_y);
                if (status.slot_count < H3AutoPolicy::kStatusSlotCapacity
                    && PointInRect_(px, py, add_x, add_y,
                        STATUS_ADD_W, STATUS_ADD_H)) {
                    handled = true;
                    s_p.status_add_armed = true;
                    if (BuildStatusCandidates_(status, -1) > 0) {
                        const int picked = s_status_dd_ids[0];
                        status.slots[status.slot_count++] = picked;
                        s_status_dd_open = status.slot_count - 1;
                        s_status_dd_hover = -1;
                        LogInfo("[Panel] 保持状态追加 spell=%d (方案%d)",
                            picked, s_p.selected_profile + 1);
                    } else {
                        SetStatusText_(T("panel.status_empty"), 3000);
                    }
                    s_p.status_add_armed = false;
                }
            }
            if (handled) {
                DrawPanelToBuffer_();
                return;
            }
            // 内容区空白点击不穿透；方案行/确定取消(y=468+)在区外照常处理。
            if (py >= GRID_FRAME_Y && py < TAB_HLINE2_Y) return;
        }

        // 召唤页（PAGE_SUMMON）内容区控件：设置行 + 行动行。
        if (s_p.active_page == PAGE_SUMMON) {
            // 「启用自动召唤」复选框（与「停止:」同一行）：保活无人可救时才召唤。
            if (PointInRect_(px, py, SUMMON_ENABLE_X, SUMMON_ENABLE_Y,
                    SUMMON_ENABLE_HIT_W, SUMMON_ENABLE_H)) {
                SummonProfileFields& sf =
                    s_p.draft_summon[s_p.selected_profile];
                sf.enabled = sf.enabled ? 0 : 1;
                LogInfo("[Panel] 自动召唤启用=%d (方案%d)", sf.enabled,
                    s_p.selected_profile + 1);
                DrawPanelToBuffer_();
                return;
            }
            // 法术下拉（收起态）：点击展开；先收起另外两处避免层级重叠。
            if (PointInRect_(px, py, SUMMON_DD_X, SUMMON_SPELL_Y,
                    SUMMON_DD_W, SUMMON_DD_H)) {
                s_summon_cond_dd_open = false;
                s_summon_cond_dd_hover = -1;
                s_summon_act_dd_open = false;
                s_summon_act_dd_hover = -1;
                s_summon_spell_dd_open = true;
                s_summon_spell_dd_hover = -1;
                DrawPanelToBuffer_();
                return;
            }
            // 队数阈值框：进入录入（预填当前值）。
            if (PointInRect_(px, py, SUMMON_CNT_BOX_X, SUMMON_ROW1_Y,
                    SUMMON_CNT_BOX_W, SUMMON_DD_H)) {
                BeginSummonNumEdit_(SUMMON_EDIT_COUNT);
                DrawPanelToBuffer_();
                return;
            }
            // 血量阈值框：进入录入。
            if (PointInRect_(px, py, SUMMON_HP_BOX_X, SUMMON_ROW2_Y,
                    SUMMON_HP_BOX_W, SUMMON_DD_H)) {
                BeginSummonNumEdit_(SUMMON_EDIT_HP);
                DrawPanelToBuffer_();
                return;
            }
            // 条件组合下拉（收起态）：点击展开。
            if (PointInRect_(px, py, SUMMON_CB_DD_X, SUMMON_ROW2_Y,
                    SUMMON_CB_DD_W, SUMMON_DD_H)) {
                s_summon_spell_dd_open = false;
                s_summon_spell_dd_hover = -1;
                s_summon_act_dd_open = false;
                s_summon_act_dd_hover = -1;
                s_summon_cond_dd_open = true;
                s_summon_cond_dd_hover = -1;
                DrawPanelToBuffer_();
                return;
            }
            // 行动下拉（收起态）：点击展开。
            if (PointInRect_(px, py, SUMMON_ACT_DD_X, SUMMON_ACT_DD_Y,
                    SUMMON_ACT_DD_W, SUMMON_ACT_DD_H)) {
                s_summon_spell_dd_open = false;
                s_summon_spell_dd_hover = -1;
                s_summon_cond_dd_open = false;
                s_summon_cond_dd_hover = -1;
                s_summon_act_dd_open = true;
                s_summon_act_dd_hover = -1;
                DrawPanelToBuffer_();
                return;
            }
            // 「允许降级为防御」复选框：仅随机移动时显示并生效。
            if (PointInRect_(px, py, SUMMON_FB_X, SUMMON_FB_Y,
                    SUMMON_FB_CHECK_W + 4 + SUMMON_FB_TEXT_W, SUMMON_ACT_DD_H)) {
                AutoStackRule& rule =
                    s_p.draft_summon[s_p.selected_profile].summon_rule;
                if (rule.action == AA_RANDOM_MOVE) {
                    rule.allowDefendFallback = !rule.allowDefendFallback;
                    LogInfo("[Panel] 召唤物降级防御=%d (方案%d)",
                        rule.allowDefendFallback ? 1 : 0,
                        s_p.selected_profile + 1);
                    DrawPanelToBuffer_();
                }
                return;
            }
            // 召唤页内容区（设置行以下、下横线以上）的空白点击不穿透；
            // 方案行(y=47)/确定取消(y=468+)在区外照常处理。
            if (py >= GRID_FRAME_Y && py < TAB_HLINE2_Y)
                return;
        }

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
        else if (PointInRect_(px, py, HELP_BTN_X, HELP_BTN_Y, HELP_BTN_SIZE, HELP_BTN_SIZE))
            button = 3;
        else if (PointInRect_(px, py, BATTLE_DD_X, BATTLE_DD_Y,
                 BATTLE_DD_W, BATTLE_DD_H))
            button = 4;   // 本场存档下拉（原读档+存档合并区）
        if (button != 0) {
            s_p.pressed_button = button;
            DrawPanelToBuffer_();
            return;
        }

        if (s_p.active_page == PAGE_ARMY && max_row > 0
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
        if (s_p.active_page == PAGE_ARMY
            && HandleExpandedDropdownClick_(px, py, 4))
            return;

        // ---- 格子单元格点击 ----
        if (s_p.active_page == PAGE_ARMY) {
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
                        // 循环移动：已有路径点可覆盖重设，末尾「＋」追加。
                        // 隐藏面板后只点 1 格即回写对应槽位。
                        if (ctrl->move_path_pick_request != 0) {
                            s_move_pick_wp = ctrl->move_path_pick_request - 1;
                            s_move_pick_cell = i;
                            s_panel_hidden_for_pick = true;
                            s_pick_wait_button_release = true;
                            HidePanelForPick_();
                            LogInfo("[Panel] 进入循环移动拾取 cell=%d wp=%d",
                                i, s_move_pick_wp);
                            ctrl->move_path_pick_request = 0;
                        }
                        // 循环施法：已有快捷键可覆盖，末尾「＋」追加。
                        // 弹出模态框，只等按 1-9/0（含小键盘）；不再点选快捷施法栏。
                        if (ctrl->spell_pick_request != 0) {
                            // 切换到另一个槽时，清掉旧槽高亮。
                            if (s_spell_pick_cell >= 0 && s_spell_pick_cell < CELL_COUNT
                                && s_spell_pick_cell != i)
                                s_p.cells[s_spell_pick_cell].spell_pick_request = 0;
                            s_spell_pick_slot = ctrl->spell_pick_request - 1;
                            s_spell_pick_cell = i;
                            s_help_modal_open = false;
                            // 收起所有下拉，避免模态框下方还有展开层。
                            for (int k = 0; k < CELL_COUNT; ++k) {
                                s_p.cells[k].expanded = CEX_NONE;
                                s_p.cells[k].dirty = true;
                            }
                            // 保留 spell_pick_request 做槽位高亮；数字键提交后再清。
                            LogInfo("[Panel] 弹出循环施法快捷键模态框 cell=%d slot=%d",
                                i, s_spell_pick_slot);
                        }
                        // 循环近战：已有组合可覆盖重设，末尾「＋」追加。
                        // 单次隐藏面板后连续选站立格、相邻攻击格。
                        if (ctrl->melee_pair_pick_request != 0) {
                            s_melee_pick_pair = ctrl->melee_pair_pick_request - 1;
                            s_melee_pick_cell = i;
                            s_melee_pick_phase = 1;
                            s_melee_pick_stand_hex = -1;
                            s_panel_hidden_for_pick = true;
                            s_pick_wait_button_release = true;
                            HidePanelForPick_();
                            LogInfo("[Panel] 进入循环近战拾取 cell=%d pair=%d",
                                i, s_melee_pick_pair);
                            ctrl->melee_pair_pick_request = 0;
                        }
                        // 进入拾取后必须保持面板隐藏；只在普通卡片操作时重画。
                        if (!s_panel_hidden_for_pick)
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
        bool activate = false;
        if (pressed == 1)
            activate = PointInRect_(px, py, OK_X, BTN_Y, BTN_W, BTN_H);
        else if (pressed == 2)
            activate = PointInRect_(px, py, CANCEL_X, BTN_Y, BTN_W, BTN_H);
        else if (pressed == 3)
            activate = PointInRect_(px, py, HELP_BTN_X, HELP_BTN_Y,
                HELP_BTN_SIZE, HELP_BTN_SIZE);
        else if (pressed == 4)
            activate = PointInRect_(px, py, BATTLE_DD_X, BATTLE_DD_Y,
                BATTLE_DD_W, BATTLE_DD_H);
        const bool redraw = pressed != 0 || s_p.scroll_button_pressed != 0
            || s_p.scroll_dragging;
        s_p.pressed_button = 0;
        s_p.scroll_button_pressed = 0;
        s_p.scroll_dragging = false;
        if (activate) {
            if (pressed == 1) {
                SetPhase_(BP_COMBAT_CLOSED, BE_PANEL_COMMIT); // 状态机关闭边（S2.2）
                CommitAndCloseSettingsPanel_();
            } else if (pressed == 2) {
                SetPhase_(BP_COMBAT_CLOSED, BE_PANEL_CANCEL); // 状态机关闭边（S2.2）
                CloseSettingsPanel();
            } else if (pressed == 3) {
                // 打开帮助前收起下拉，避免模态层下还有展开列表。
                for (int k = 0; k < CELL_COUNT; ++k) {
                    s_p.cells[k].expanded = CEX_NONE;
                    s_p.cells[k].dirty = true;
                }
                CancelSummonNumEdit_();
                CloseSummonDropdowns_();
                s_help_modal_open = true;
                LogInfo("[Panel] 打开帮助说明模态框");
                DrawPanelToBuffer_();
            } else if (pressed == 4) {
                // 打开/收起本场存档下拉（列表数据在打开面板时刷新）。
                if (s_battle_record_count > 0) {
                    s_battle_dd_open = !s_battle_dd_open;
                    s_battle_dd_hover = -1;
                    LogDebug("[Panel] 本场存档下拉 %s（%d 条）",
                        s_battle_dd_open ? "展开" : "收起",
                        s_battle_record_count);
                } else {
                    SetStatusText_(T("panel.status_battle_none"), 3000);
                }
                DrawPanelToBuffer_();
            }
            return;
        }
        if (redraw) DrawPanelToBuffer_();
        if (pressed != 0)
            return;

        // 优先处理展开的下拉：防止选中点击穿透到下方格子
        if (s_p.active_page == PAGE_ARMY
            && HandleExpandedDropdownClick_(px, py, 8))
            return;

        // 面板松开(raw 16) → 格子松开(8)，选中展开的下拉项
        if (s_p.active_page == PAGE_ARMY) {
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

void HandlePanelInput_()
{
    static bool previous_up_down = false;
    static bool previous_down_down = false;
    static bool previous_page_up_down = false;
    static bool previous_page_down_down = false;
    static bool previous_digit_down[10] = {};

    const bool up_down = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    const bool down_down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    const bool page_up_down = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
    const bool page_down_down = (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
    // 停止回合编辑态例外：输入法常驻窗会盖住游戏窗口（modal_depth>=2 且
    // 前台判定失败），两条键盘路径都被掐死。GetAsyncKeyState 不依赖焦点，
    // 编辑态放行；滚屏/翻页仍受前台判定保护。
    if (!IsGameWindowForeground_() && !s_stop_turns_editing
        && !s_mana_th_editing && !s_status_th_editing) {
        CancelPanelTransientInput_();
        previous_up_down = up_down;
        previous_down_down = down_down;
        previous_page_up_down = page_up_down;
        previous_page_down_down = page_down_down;
        for (int d = 0; d < 10; ++d) previous_digit_down[d] = false;
        return;
    }

    // 循环施法模态框：只支持直接按 1-9/0（含小键盘）；ESC 取消。
    // 底层滚动键在模态框打开时不处理。
    if (s_spell_pick_cell >= 0) {
        for (int d = 0; d <= 9; ++d) {
            const int vk_main = (d == 0) ? '0' : ('0' + d);
            const int vk_num = (d == 0) ? VK_NUMPAD0 : (VK_NUMPAD0 + d);
            const bool down = ((GetAsyncKeyState(vk_main) & 0x8000) != 0)
                || ((GetAsyncKeyState(vk_num) & 0x8000) != 0);
            if (down && !previous_digit_down[d]) {
                if (CommitSpellSlotPick_(d))
                    break;
            }
            previous_digit_down[d] = down;
        }
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
            EndSpellPick_();
        previous_up_down = up_down;
        previous_down_down = down_down;
        previous_page_up_down = page_up_down;
        previous_page_down_down = page_down_down;
        return;
    }

    for (int d = 0; d < 10; ++d) previous_digit_down[d] = false;

    const int old_row = s_p.scroll_row;
    if (up_down && !previous_up_down) SetPanelScrollRow_(s_p.scroll_row - 1);
    if (down_down && !previous_down_down) SetPanelScrollRow_(s_p.scroll_row + 1);
    if (page_up_down && !previous_page_up_down)
        SetPanelScrollRow_(s_p.scroll_row - CELL_COUNT / COLS);
    if (page_down_down && !previous_page_down_down)
        SetPanelScrollRow_(s_p.scroll_row + CELL_COUNT / COLS);
    if (s_p.scroll_row != old_row)
        DrawPanelToBuffer_();

    previous_up_down = up_down;
    previous_down_down = down_down;
    previous_page_up_down = page_up_down;
    previous_page_down_down = page_down_down;
}
