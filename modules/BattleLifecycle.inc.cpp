// ========== BattleLifecycle.inc.cpp ==========
// 战斗生命周期事件源（自 SettingsDlg 批次2 搬出，代码原样迁入，见
// docs/13-界面与功能分离.md）。本层是状态机 S1-S8 的产生侧：
// - BattleUI 出现/消失兜底（含重打宽限、结果窗意图期间的退位）；
// - 开面板热键消费（S8）与右键自动战斗说明框 → 开面板；
// - CPResult 结果窗意图观察（确定/取消重打/超时终局）。
// 不依赖任何界面实现：开面板/查面板经 IH3AutoUi 契约（g_uiPort）。
// 本文件在 H3Auto.cpp 中排在 BattleState.inc.cpp 之后、界面模块之前：
// 访问 AutoExecute 的 InCombat_/g_auto_state 与 BattleState 的
// g_phase/g_ui_gone_grace_until；FindDialogByVtable_/GetBattleItemUnderCursor_
// 定义在本文件（PanelInput 的悬停屏障随后按同 TU 顺序取用）。
#include "PolicyCore.hpp"
#include "BattleState.hpp"
#include "AutoUiPort.hpp"

static void LogInfo(const char* fmt, ...);  // 分级前向声明（LogWarn/LogError 等见 ConfigLog）
extern IH3AutoUi* g_uiPort;
extern bool InCombat_();

// 真正的战斗窗口。0x0063A5E4 是战斗底下仍保留的冒险地图窗口。
// （PanelInput 的悬停屏障也按此虚表找 BattleUI，保持单一事实。）
static const UINT s_combat_dialog_vtable = 0x0063D528;
static const UINT s_right_click_dialog_vtable = 0x0063DB40;
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

// 按虚表在窗口管理器对话链上找对话框（自 PanelInput 迁入，批次2：
// 生命周期层与 PanelInput 的悬停屏障共用，单一事实）。
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

// 战斗底栏上光标命中的控件 id（自 PanelInput 迁入，批次2；生命周期层需要，
// 无界面变体不包含 PanelInput）。
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
// 检测"自动战斗"对话框关闭 + 结果窗生命周期
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

void CheckAutoFightDialogClosed()
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
            if (g_phase == BP_COMBAT_CLOSED && !g_uiPort->IsPanelActive()) {
                LogInfo("[Panel] OpenSettings hotkey");
                if (g_uiPort->OpenPanel())
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
        // 界面契约开面板（旧：OpenSettingsPanel_() + s_p.active 复核）。
        if (g_uiPort->OpenPanel())
            SetPhase_(BP_COMBAT_OPEN, BE_PANEL_OPEN_REQUESTED);
        s_panel_popup_done = true;
    } else if (!right_button_down && !s_saw_explanation_dlg_in_battle) {
        // 在目标按钮上按下但没有出现对应说明框时，不把状态带到下一次右键。
        s_autofight_right_press_armed = false;
    }
}
