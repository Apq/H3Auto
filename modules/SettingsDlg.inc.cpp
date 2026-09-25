// ========== SettingsDlg.inc.cpp ==========
// 打铁助手 - 设置面板
// 渲染：LoHook 0x600430 画面板到 screenPcx16
// 输入捕获：检测"自动战斗"对话框的关闭事件

#define o_WndMgr (*reinterpret_cast<H3WindowManager**>(0x6992D0))
#define o_DDSurfaceBackBuffer (*reinterpret_cast<LPDIRECTDRAWSURFACE*>(0x6AAD28))

extern void OnBattleResultAccepted();
extern void EnsureStackTrackingBound();

// ========================================================================
// 第一部分：数据声明
// ========================================================================

static const int PANEL_W    = 680;
// 底部状态栏 +26，背景图 HA_bg.pcx 同步为 680×528。
static const int PANEL_H    = 528;
static const int COLS       = 1;   // 一行一格（单列宽格）
static const int VISIBLE_ROWS = 3; // 金框内刚好 3 行
static const int CELL_W     = 568; // 横向占满金框宽（右侧留滚动条）
// 卡片恢复旧版高度 110：金框 624×342，底边 y=414，表格上方不再留设置行。
// SCROLL_H = CELL_H + 2*(CELL_H-2) = 110 + 2*108 = 326。
static const int CELL_H     = 110;
static const int CELL_STEP_X = CELL_W - 2;
static const int CELL_STEP_Y = CELL_H - 2;
static const int GRID_X      = 41;
static const int GRID_Y      = 102;
static const int SCROLL_X    = GRID_X + CELL_W + (COLS - 1) * CELL_STEP_X + 18;
static const int SCROLL_Y    = GRID_Y;
static const int SCROLL_W    = 16;
static const int SCROLL_H    = CELL_H + (VISIBLE_ROWS - 1) * CELL_STEP_Y;  // 326
static const int MARGIN      = 20;
static const int TITLE_H    = 44;
static const int BTN_W      = 64;
static const int BTN_H      = 30;
static const int BTN_FRAME_W = 66;
static const int BTN_FRAME_H = 32;
static const int BTN_GAP    = 24;
static const int BTN_Y      = PANEL_H - 83;
static const int OK_X       = (PANEL_W - BTN_GAP) / 2 - BTN_W;
static const int CANCEL_X   = (PANEL_W + BTN_GAP) / 2;
static const int CELL_COUNT = COLS * VISIBLE_ROWS;
static const int MAX_STACKS = 21;
static const int PROFILE_COUNT = 5;
static const int PROFILE_BTN_W = 104;
static const int PROFILE_BTN_H = 18;
static const int PROFILE_BTN_GAP = 8;
static const int PROFILE_BTN_Y = 47;
static const int PROFILE_BTNS_W = PROFILE_COUNT * PROFILE_BTN_W
    + (PROFILE_COUNT - 1) * PROFILE_BTN_GAP;
static const int PROFILE_BTN_X = (PANEL_W - PROFILE_BTNS_W) / 2;

// 右上角帮助按钮（点击弹使用说明模态框）
static const int HELP_BTN_SIZE = 24;
static const int HELP_BTN_X = PANEL_W - HELP_BTN_SIZE - 20; // 再左收 1px
static const int HELP_BTN_Y = 20; // 再下收 2px

// 「读档」「存档」：? 按钮左侧，同高 24，宽 44，间距 6。
static const int STORE_BTN_W = 44;
static const int STORE_BTN_GAP = 6;
static const int SAVE_BTN_X = HELP_BTN_X - STORE_BTN_GAP - STORE_BTN_W;
static const int LOAD_BTN_X = SAVE_BTN_X - STORE_BTN_GAP - STORE_BTN_W;

// 网格金框：宽 624，高 342（底边 y=436）。
static const int GRID_FRAME_W = 624;
static const int GRID_FRAME_H = 342;
static const int GRID_FRAME_X = 29;
static const int GRID_FRAME_Y = 94;

// 方案行与金框之间的保活策略行（方案级）：label + 下拉框。
static const int PROTECT_DD_Y        = 69;
static const int PROTECT_DD_H        = 18;
static const int PROTECT_DD_LABEL_X  = GRID_FRAME_X;
static const int PROTECT_DD_LABEL_W  = 76;   // 「保活策略:」
static const int PROTECT_DD_X        = GRID_FRAME_X + 80;
static const int PROTECT_DD_W        = 150;
static const int PROTECT_DD_ITEM_H   = 18;

// 保活行最右侧：自动停止回合数。点击后用数字键录入（可编辑文本框：
// 预填当前值、光标可左右移动、退格删除；见 s_stop_turns_* 状态）。
static const int STOP_BOX_W = 52;
static const int STOP_BOX_X = GRID_FRAME_X + GRID_FRAME_W - STOP_BOX_W;
static const int STOP_LABEL_W = 42;
static const int STOP_LABEL_X = STOP_BOX_X - 4 - STOP_LABEL_W;

// 保活策略项（顺序同 ProtectStrategy）
static const char* PROTECT_STRATEGY_LABELS[H3AutoPolicy::PS_COUNT] = {
    "无", "部队全灭后", "回合内首动", "损失量大于恢复量",
};

// 硬编码默认标签（INI 加载失败时使用）
static const char* DEFAULT_ACTION_LABELS[AA_COUNT] = {
    "手动", "防御", "等待", "循环移动", "循环近战", "远程攻击", "急救治疗",
};
static const char* DEFAULT_SELECTOR_LABELS[SEL_COUNT] = {
    "随机", "远程飞兵高速优先", "数量最多", "失血比例", "失血数值",
};

// 运行时标签
const char* g_action_labels[AA_COUNT] = {};
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
    static char storage_selector[SEL_COUNT][64] = {};
    LoadLabelArray_("Actions", DEFAULT_ACTION_LABELS, g_action_labels,
        storage_action, AA_COUNT, ini_path);
    LoadLabelArray_("Selectors", DEFAULT_SELECTOR_LABELS, g_selector_labels,
        storage_selector, SEL_COUNT, ini_path);
    WriteLog("[Panel] 标签已从 %s 加载。", ini_path);
    GetPrivateProfileStringA("Panel", "Title", "打铁设置",
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
    uint8_t draft_protect_strategy[PROFILE_COUNT]; // 保活策略草稿（方案级）
    uint16_t draft_stop_turns[PROFILE_COUNT];     // 自动停止回合草稿，0=关闭，0..999
    int selected_profile;
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
    CellControl cells[CELL_COUNT]; // 仅保存当前可见的最多 3 行控件
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
// 战场拾取期间隐藏面板（不绘制），但保持 s_p.active，用于取坐标而不放行点击
static bool s_panel_hidden_for_pick = false;
static Patch* s_hover_patch_primary = nullptr;
static Patch* s_hover_patch_secondary = nullptr;

// HD_SOD.dll 高亮屏蔽：patch HD 战斗消息钩子 FUN_010d9ce0 (RVA 0xD9CE0) 为 ret 12
static HMODULE s_hd_sod_module = nullptr;
static Patch* s_hd_msgproc_patch = nullptr;

// 面板打开时安装 WH_KEYBOARD 钩子，立即响应 ESC/Enter，不依赖游戏帧率
static void CommitAndCloseSettingsPanel_();
static void EndSpellPick_();
static bool CommitSpellSlotPick_(int slot_value);
static void DrawPanelToBuffer_();
// 循环施法录入状态需在键盘钩子前声明。
static int s_spell_pick_cell = -1;
static int s_spell_pick_slot = -1;
static bool s_help_modal_open = false;
static bool s_protect_dd_open = false;   // 保活策略下拉展开态（方案级）
static int  s_protect_dd_hover = -1;     // 下拉展开时悬停项，-1=无
static bool s_stop_turns_editing = false; // 正在录入当前方案的停止回合
static char s_stop_turns_text[8] = {};     // 最多 3 位 + 结束符
static int  s_stop_turns_caret = 0;        // 插入位置（0..文本长度）
static DWORD s_stop_turns_caret_tick = 0;  // 光标闪烁基准（按键后重置，输入即可见）
static const int STOP_TURNS_MAX_DIGITS = 3; // 输入上限 3 位；提交截到 999
static char s_status_text[96] = {};
static DWORD s_status_until = 0;
static bool s_status_error = false;
static HHOOK s_kb_hook = nullptr;

// ===== 保活策略下拉（方案级）与数字键拦截 =====

// 设置面板存活期间（含隐藏拾取态）拦截 0-9/小键盘，避免原版快捷施法抢键。
static void CommitStopTurnsEdit_();
static void CancelStopTurnsEdit_();

static bool IsDigitKey_(WPARAM vk)
{
    return (vk >= '0' && vk <= '9')
        || (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9);
}

static int DigitFromVk_(WPARAM vk)
{
    if (vk >= '0' && vk <= '9') return (int)(vk - '0');
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (int)(vk - VK_NUMPAD0);
    return -1;
}

static LRESULT CALLBACK PanelKbHook_(int code, WPARAM wParam, LPARAM lParam)
{
    // 只要设置面板还在（含 s_panel_hidden_for_pick），就拦截相关键。
    // 不再判断 s_panel_modal_suspended：隐藏拾取时也要挡住快捷施法。
    if (code == HC_ACTION && s_p.active && !(lParam & 0x80000000)) // keydown only
    {
        if (wParam == VK_ESCAPE) {
            if (s_help_modal_open) {
                s_help_modal_open = false;
                DrawPanelToBuffer_();
            } else if (s_stop_turns_editing) {
                CancelStopTurnsEdit_();
                DrawPanelToBuffer_();
            } else if (s_protect_dd_open) {
                s_protect_dd_open = false;
                s_protect_dd_hover = -1;
                DrawPanelToBuffer_();
            } else if (s_spell_pick_cell >= 0)
                EndSpellPick_();
            else if (!s_panel_hidden_for_pick) {
                SetPhase_(BP_COMBAT_CLOSED, BE_PANEL_CANCEL); // 状态机关闭边（S2.2）
                CloseSettingsPanel();
            }
            return 1;  // swallow
        }
        if (wParam == VK_RETURN && s_stop_turns_editing) {
            CommitStopTurnsEdit_();
            DrawPanelToBuffer_();
            return 1;
        }
        // 帮助/快捷键模态或保活下拉展开时：Enter 不提交设置面板，仅吞掉。
        if (wParam == VK_RETURN && !s_panel_hidden_for_pick
            && s_spell_pick_cell < 0 && !s_help_modal_open
            && !s_protect_dd_open) {
            SetPhase_(BP_COMBAT_CLOSED, BE_PANEL_COMMIT); // 状态机关闭边（S2.2）
            CommitAndCloseSettingsPanel_();
            return 1;  // swallow
        }
        if (wParam == VK_RETURN) return 1;
        // 停止回合录入走钩子即时处理。轮询有帧间隔，短按会丢。
        // 按住方向/退格/删除：首次重复等 400ms，之后每 30ms。
        if (s_stop_turns_editing) {
            static WPARAM s_repeat_vk = 0;
            static DWORD s_repeat_tick = 0;
            const DWORD now = GetTickCount();
            const bool repeatable = wParam == VK_LEFT || wParam == VK_RIGHT
                || wParam == VK_BACK || wParam == VK_DELETE;
            const bool first = (lParam & 0x40000000) == 0;
            if (repeatable && !first) {
                const DWORD gap = (s_repeat_vk == wParam)
                    ? (DWORD)30 : (DWORD)400;
                if (now - s_repeat_tick < gap)
                    return 1;
            }
            const int len = (int)strlen(s_stop_turns_text);
            bool changed = false;
            if (wParam == VK_BACK && s_stop_turns_caret > 0) {
                memmove(s_stop_turns_text + s_stop_turns_caret - 1,
                    s_stop_turns_text + s_stop_turns_caret,
                    len - s_stop_turns_caret + 1);
                --s_stop_turns_caret;
                changed = true;
            } else if (wParam == VK_DELETE && s_stop_turns_caret < len) {
                memmove(s_stop_turns_text + s_stop_turns_caret,
                    s_stop_turns_text + s_stop_turns_caret + 1,
                    len - s_stop_turns_caret);
                changed = true;
            } else if (wParam == VK_LEFT && s_stop_turns_caret > 0) {
                --s_stop_turns_caret;
                changed = true;
            } else if (wParam == VK_RIGHT && s_stop_turns_caret < len) {
                ++s_stop_turns_caret;
                changed = true;
            } else if (IsDigitKey_(wParam)) {
                const int d = DigitFromVk_(wParam);
                if (d >= 0 && len < STOP_TURNS_MAX_DIGITS
                    && s_stop_turns_caret <= len) {
                    memmove(s_stop_turns_text + s_stop_turns_caret + 1,
                        s_stop_turns_text + s_stop_turns_caret,
                        len - s_stop_turns_caret + 1);
                    s_stop_turns_text[s_stop_turns_caret] =
                        static_cast<char>('0' + d);
                    ++s_stop_turns_caret;
                    changed = true;
                }
            }
            if (changed) {
                s_stop_turns_caret_tick = now;
                if (repeatable) {
                    s_repeat_vk = wParam;
                    s_repeat_tick = now;
                }
                DrawPanelToBuffer_();
            }
            if (changed || wParam == VK_LEFT || wParam == VK_RIGHT
                || wParam == VK_BACK || wParam == VK_DELETE
                || IsDigitKey_(wParam))
                return 1;
        }
        if (IsDigitKey_(wParam)) {
            // 快捷施法槽位：钩子先手；停止回合录入与槽位兜底都在轮询。
            if (s_spell_pick_cell >= 0) {
                const int d = DigitFromVk_(wParam);
                if (d >= 0) CommitSpellSlotPick_(d);
            }
            return 1;  // 始终吞掉 0-9，防止原版快捷施法捕获
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// 面板打开时安装 WH_MOUSE 钩子，鼠标移动立即刷新下拉悬停高亮，不依赖游戏帧率
static HHOOK s_mouse_hook = nullptr;
static bool UpdateDropdownHover_(int px, int py);  // 前向声明（钩子先用到）

// 循环近战连续拾取：phase=1 选站立格，phase=2 选相邻攻击格。
static int s_melee_pick_cell = -1;
static int s_melee_pick_pair = -1;
static int s_melee_pick_phase = 0;
static int s_melee_pick_stand_hex = -1;
// 点「＋」/编辑路径是在左键按下时进入拾取；同一次点击的松开不得当作战场第一击。
static bool s_pick_wait_button_release = false;

// 循环移动路径点拾取：与循环近战同交互，但每次只点 1 格。
// s_move_pick_cell 为卡片索引，s_move_pick_wp 为路径点槽 0..5。
static int s_move_pick_cell = -1;
static int s_move_pick_wp = -1;
static void EndMovePathPick_();
// 循环施法录入：弹出模态框，仅按 1-9/0 记录同一数字。
static void DoPickCapture_(int hex, bool right_click);
static void GetSpellKeyModalRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void GetSpellKeyModalCancelRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void DrawSpellKeyModal_(H3LoadedPcx16* scr);

static LRESULT CALLBACK PanelMouseHook_(int code, WPARAM wParam, LPARAM lParam)
{
    // 战场拾取的点击捕获已移回 BattleUI 输入屏障处理器
    // BlockBattleItemMessage_（消息坐标转战场 hex），此处只保留
    // 下拉展开时的悬停高亮刷新。
    __try {
    if (code == HC_ACTION && s_p.active && !s_panel_modal_suspended
        && wParam == WM_MOUSEMOVE)
    {
        // 有下拉展开时才需要即时刷新高亮（卡片下拉或保活策略下拉）
        bool any_expanded = s_protect_dd_open;
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // WriteLog("[Panel] 鼠标钩子异常 code=0x%08X", GetExceptionCode());
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
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
static void DoPickCapture_(int hex, bool right_click);
static void HidePanelForPick_();
static H3CombatManager* GetCombatMgr();
// 原版/HD 调用 0x464380 前都会减掉战斗窗口 dlg->x/dlg->y。
// 传入绝对屏幕坐标会把 02/03 之类的点错映射到 13/14。
static int ResolvePickHexAtScreen_(H3CombatManager* mgr, int abs_x, int abs_y, int* out_rel_x, int* out_rel_y)
{
    if (!mgr) return -1;
    int rel_x = abs_x;
    int rel_y = abs_y;
    int dlg_x = 0;
    int dlg_y = 0;
    __try {
        H3CombatDlg* dlg = mgr->dlg;
        if (dlg) {
            dlg_x = dlg->GetX();
            dlg_y = dlg->GetY();
            rel_x = abs_x - dlg_x;
            rel_y = abs_y - dlg_y;
        } else {
            // 回退：结构偏移 +0x132FC -> dlg，再读 +0x18/+0x1C（xDlg/yDlg）。
            BYTE* base = reinterpret_cast<BYTE*>(mgr);
            BYTE* raw_dlg = *reinterpret_cast<BYTE**>(base + 0x132FC);
            if (raw_dlg) {
                dlg_x = *reinterpret_cast<INT32*>(raw_dlg + 0x18);
                dlg_y = *reinterpret_cast<INT32*>(raw_dlg + 0x1C);
                rel_x = abs_x - dlg_x;
                rel_y = abs_y - dlg_y;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (out_rel_x) *out_rel_x = rel_x;
    if (out_rel_y) *out_rel_y = rel_y;
    int hex = -1;
    __try {
        hex = THISCALL_3(int, 0x464380, mgr, rel_x, rel_y);
    } __except (EXCEPTION_EXECUTE_HANDLER) { hex = -1; }
    if (!CellControl_HexValid(hex)) hex = -1;
    return hex;
}

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
    // 战场拾取期间：屏障 item 会收到战场点击。此时不派发给战场（靠
    // StopProcessing 吞掉，部队绝不行动）。hex 直接从消息中的游戏逻辑坐标
    // 用 SquareAtCoordinates(0x464380) 转换，不依赖 mouseCoord 也不恢复 hover，
    // 因此拾取期间游戏不会显示任何与待行动兵种相关的悬停高亮，只有我
    // 们自己的蓝色格子标示。左键松开=确认，右键=取消。
    if (s_p.active && (s_melee_pick_phase != 0 || s_move_pick_cell >= 0)) {
        const int raw = static_cast<int>(msg.command);
        // 只用松开确认；LCLICK_OUTSIDE/RCLICK_OUTSIDE 与 UP 会在同一次点击里连发，
        // 只处理 UP 即可，无需按时间或格号去重。
        if (raw == static_cast<int>(eMsgCommand::RBUTTON_UP)) {
            s_pick_wait_button_release = false;
            DoPickCapture_(-1, true);
        } else if (raw == static_cast<int>(eMsgCommand::LBUTTON_UP)) {
            if (s_pick_wait_button_release) {
                // 吞掉进入拾取那一击的松开，不作战场选点。
                s_pick_wait_button_release = false;
                WriteLog("[Panel] 拾取已就绪，忽略引发点击的松开");
                return msg.StopProcessing();
            }
            // 原版/HD 都是：绝对光标坐标 - 战斗窗口 dlg->x/y，再喂给 0x464380。
            // 直接传绝对坐标会整体偏移，把 02/03 记成 13/14 一类远处格子。
            const H3POINT cursor = H3POINT::GetCursorPosition();
            int abs_x = cursor.x;
            int abs_y = cursor.y;
            int rel_x = abs_x;
            int rel_y = abs_y;
            int hex = -1;
            H3CombatManager* mgr = GetCombatMgr();
            if (mgr) {
                hex = ResolvePickHexAtScreen_(mgr, abs_x, abs_y, &rel_x, &rel_y);
                // 光标失败时再试消息包坐标（同样先减 dlg 原点）。
                if (!CellControl_HexValid(hex)) {
                    const int mx = static_cast<int>(msg.subtype);
                    const int my = msg.itemId;
                    if (mx >= 0 && my >= 0 && mx <= 4096 && my <= 4096) {
                        int rx2 = mx, ry2 = my;
                        const int h2 = ResolvePickHexAtScreen_(mgr, mx, my, &rx2, &ry2);
                        if (CellControl_HexValid(h2)) {
                            abs_x = mx; abs_y = my;
                            rel_x = rx2; rel_y = ry2;
                            hex = h2;
                        }
                    }
                }
            }
            // WriteLog("[Panel] pick click abs=(%d,%d) rel=(%d,%d) hex=%d",
            //     abs_x, abs_y, rel_x, rel_y, hex);
            DoPickCapture_(hex, false);
        }
        // 其他鼠标消息（包括 LCLICK_OUTSIDE）统统吞掉，不传透。
        return msg.StopProcessing();
    }

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
        const int wheel_delta = static_cast<int>(msg.subtype);
        // 近战站立/攻击下拉展开时，滚轮优先滚动下拉列表
        bool scrolled_dropdown = false;
        for (int i = 0; i < CELL_COUNT; ++i) {
            CellControl* ctrl = &s_p.cells[i];
            if (ctrl->expanded == CEX_STAND || ctrl->expanded == CEX_ATTACK) {
                if (CellControl_ScrollExpanded(ctrl, wheel_delta))
                    scrolled_dropdown = true;
                break;
            }
        }
        if (scrolled_dropdown) {
            DrawPanelToBuffer_();
        } else {
            const int old_row = s_p.scroll_row;
            if (wheel_delta < 0)
                SetPanelScrollRow_(s_p.scroll_row + 1);
            else if (wheel_delta > 0)
                SetPanelScrollRow_(s_p.scroll_row - 1);
            if (s_p.scroll_row != old_row)
                DrawPanelToBuffer_();
        }
    }
    // 右键松开：用于循环施法/移动/近战已有槽位删除。
    // 预翻译包里 RBUTTON_UP 的原始 command 通常为 64。
    if (panel_was_active
        && (raw_command == 4 || raw_command == 8 || raw_command == 16
            || raw_command == 64
            || raw_command == static_cast<int>(eMsgCommand::RBUTTON_UP))) {
        // H3Msg 的鼠标坐标在 position[0x10]/position[0x14]，不是
        // subtype[0x04]/itemId[0x08]。后两者是按钮子类型和 item id；
        // 误用它们会让右键删除命中错误的卡片/槽位，尤其表现为末尾槽删不掉。
        HandlePanelMouseMessage_(raw_command,
            static_cast<int>(msg.position.x), static_cast<int>(msg.position.y));
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
    int expected_height, H3LoadedPcx16*& cache, bool& load_failed,
    bool allow_shorter_height)
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
        || plane_count != 3 || width != expected_width
        || (height != expected_height
            && !(allow_shorter_height && height < expected_height))
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
        s_panel_background, s_panel_background_load_failed, false);
}

static H3LoadedPcx16* LoadPanelCell_()
{
    return LoadPanelPcx24_("HA_cell.pcx", CELL_W, CELL_H,
        s_panel_cell, s_panel_cell_load_failed, false);
}

static H3LoadedPcx16* LoadPanelGridFrame_()
{
    return LoadPanelPcx24_("HA_grid_frame.pcx", GRID_FRAME_W, GRID_FRAME_H,
        s_panel_grid_frame, s_panel_grid_frame_load_failed, false);
}

static bool CopyPanelBackground_(H3LoadedPcx16* destination)
{
    H3LoadedPcx16* background = LoadPanelBackground_();
    if (!background || !destination || !background->buffer || !destination->buffer)
        return false;

    const int row_bytes = background->scanlineSize < destination->scanlineSize
        ? background->scanlineSize : destination->scanlineSize;
    const int copy_h = background->height < PANEL_H
        ? background->height : PANEL_H;
    for (int y = 0; y < copy_h; ++y) {
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
        s_panel_button_frame, s_panel_button_frame_load_failed, false);
    LoadPanelPcx24_("HA_ok_normal.pcx", BTN_W, BTN_H,
        s_panel_ok_normal, s_panel_ok_normal_load_failed, false);
    LoadPanelPcx24_("HA_ok_pressed.pcx", BTN_W, BTN_H,
        s_panel_ok_pressed, s_panel_ok_pressed_load_failed, false);
    LoadPanelPcx24_("HA_cancel_normal.pcx", BTN_W, BTN_H,
        s_panel_cancel_normal, s_panel_cancel_normal_load_failed, false);
    LoadPanelPcx24_("HA_cancel_pressed.pcx", BTN_W, BTN_H,
        s_panel_cancel_pressed, s_panel_cancel_pressed_load_failed, false);
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

// 第一格（站立格）的临时标示。使用原版 CCellShd 蓝色格子资源。
// HD 最终显示走 DirectDraw 后缓冲：只写 drawBuffer/screenPcx16 不够，
// 必须把 45x52 小图直接 blit 到后缓冲；退出拾取时用战场重绘撤销。
static bool BlitPcx16ToBackBuffer_(H3LoadedPcx16* source, int dst_x, int dst_y)
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
                            // 透明键：纯青/近似青跳过，保留蓝色标示。
                            if ((color & 0x00FFFFFFu) == 0x0000FFFFu) continue;
                            dst[x * 4 + 0] = static_cast<BYTE>(color);
                            dst[x * 4 + 1] = static_cast<BYTE>(color >> 8);
                            dst[x * 4 + 2] = static_cast<BYTE>(color >> 16);
                        }
                    } else {
                        const WORD* src = reinterpret_cast<const WORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            const WORD c16 = src[x];
                            if (c16 == 0x7FDF) continue;
                            const DWORD color = PanelRGB565To8888_(c16);
                            dst[x * 4 + 0] = static_cast<BYTE>(color);
                            dst[x * 4 + 1] = static_cast<BYTE>(color >> 8);
                            dst[x * 4 + 2] = static_cast<BYTE>(color >> 16);
                        }
                    }
                } else {
                    WORD* dst = reinterpret_cast<WORD*>(dst_row) + dst_x;
                    if (source_is_32_bit) {
                        const DWORD* src = reinterpret_cast<const DWORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            const DWORD color = src[x];
                            if ((color & 0x00FFFFFFu) == 0x0000FFFFu) continue;
                            dst[x] = PanelRGB8888To565_(color);
                        }
                    } else {
                        const WORD* src = reinterpret_cast<const WORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            if (src[x] == 0x7FDF) continue;
                            dst[x] = src[x];
                        }
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

static void RefreshBattleAfterPick_()
{
    if (H3CombatManager* mgr = GetCombatMgr()) {
        __try {
            // 完整重建战场：从干净 drawBuffer 重画到屏幕，清掉 screen 层临时蓝标。
            // 注意：拾取期间不能 ShadeSquare，否则 drawBuffer 带脏像素，这里会重现蓝标。
            THISCALL_7(void, 0x493FC0, mgr, TRUE, FALSE, FALSE, 0, TRUE, FALSE);
            WriteLog("[Panel] 已请求战场完整重绘以撤销临时标示");
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
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

// 结束循环移动路径拾取：恢复面板输入/绘制。
static void EndMovePathPick_()
{
    if (s_move_pick_cell < 0) return;
    const int old_cell = s_move_pick_cell;
    const int old_wp = s_move_pick_wp;
    s_move_pick_cell = -1;
    s_move_pick_wp = -1;
    s_panel_hidden_for_pick = false;
    s_pick_wait_button_release = false;
    RefreshBattleAfterPick_();
    // 拾取期间 hover 始终保持屏蔽（不再 RestoreBattleHover_），结束时无需重屏蔽。
    ForcePanelModalDepth_(true);
    DrawPanelToBuffer_();
    WriteLog("[Panel] 结束循环移动拾取 cell=%d wp=%d", old_cell, old_wp);
}

// 结束循环施法录入：面板始终保持打开，仅清掉“待按数字键”状态。
static void EndSpellPick_()
{
    if (s_spell_pick_cell < 0) return;
    const int old_cell = s_spell_pick_cell;
    const int old_slot = s_spell_pick_slot;
    s_spell_pick_cell = -1;
    s_spell_pick_slot = -1;
    if (old_cell >= 0 && old_cell < CELL_COUNT)
        s_p.cells[old_cell].spell_pick_request = 0;
    DrawPanelToBuffer_();
    WriteLog("[Panel] 结束循环施法录入 cell=%d slot=%d", old_cell, old_slot);
}

// 写入一个快捷施法数字（1-9/0）到当前拾取槽；成功返回 true。
static bool CommitSpellSlotPick_(int slot_value)
{
    if (s_spell_pick_cell < 0 || s_spell_pick_cell >= CELL_COUNT) return false;
    if (!(slot_value == 0 || (slot_value >= 1 && slot_value <= 9))) return false;
    if (s_spell_pick_slot < 0 || s_spell_pick_slot >= SPELL_SLOT_CAPACITY) return false;

    CellControl* ctrl = &s_p.cells[s_spell_pick_cell];
    if (!ctrl->has_data) return false;

    AutoStackRule& rule = ctrl->data.rule;
    const int slot_index = s_spell_pick_slot;
    const bool append = slot_index == rule.spellSlotCount;
    if (!(slot_index < rule.spellSlotCount || append)) return false;

    rule.spellSlots[slot_index] = static_cast<int8_t>(slot_value);
    if (append && rule.spellSlotCount < SPELL_SLOT_CAPACITY)
        ++rule.spellSlotCount;
    CellControl_NormalizeSpellSlots(&rule);
    ctrl->dirty = true;
    WriteLog("[Panel] spell slot saved cell=%d idx=%d key=%d count=%d",
        s_spell_pick_cell, slot_index, slot_value, (int)rule.spellSlotCount);
    EndSpellPick_();
    return true;
}

// 结束近战选格：恢复面板输入/绘制（屏障始终保留，无需重装）。
static void EndMeleePick_()
{
    const int old_cell = s_melee_pick_cell;
    const int old_pair = s_melee_pick_pair;
    s_melee_pick_phase = 0;
    s_melee_pick_cell = -1;
    s_melee_pick_pair = -1;
    s_melee_pick_stand_hex = -1;
    s_panel_hidden_for_pick = false;
    s_pick_wait_button_release = false;
    RefreshBattleAfterPick_();
    // 拾取期间 hover 始终保持屏蔽（不再 RestoreBattleHover_），结束时无需重屏蔽。
    ForcePanelModalDepth_(true);
    DrawPanelToBuffer_();
    WriteLog("[Panel] 结束循环近战拾取 cell=%d pair=%d", old_cell, old_pair);
}

// 战场拾取坐标捕获：屏障已吞掉点击（不会触发部队行动），这里只把屏幕坐标
// 转成 hex 回填。right_click=true 表示取消当前拾取。
// 战场拾取回填：hex 由屏障处理器从消息坐标通过 SquareAtCoordinates 转换后传入。
// right_click=true 表示取消当前拾取。
static void DoPickCapture_(int hex, bool right_click)
{
    // 循环移动：每次只点 1 格。已有槽覆盖，末尾「＋」追加；右键取消不改原记录。
    if (s_move_pick_cell >= 0 && s_move_pick_cell < CELL_COUNT) {
        if (right_click) {
            EndMovePathPick_();
            return;
        }
        CellControl* ctrl = &s_p.cells[s_move_pick_cell];
        if (ctrl->has_data && CellControl_HexValid(hex)
            && s_move_pick_wp >= 0 && s_move_pick_wp < MOVE_WAYPOINT_CAPACITY) {
            AutoTargetRule& t = ctrl->data.rule.target;
            const int wp = s_move_pick_wp;
            const bool append = wp == t.moveWaypointCount;
            if (wp < t.moveWaypointCount || append) {
                t.moveWaypoints[wp] = static_cast<int16_t>(hex);
                if (append && t.moveWaypointCount < MOVE_WAYPOINT_CAPACITY)
                    ++t.moveWaypointCount;
                ctrl->dirty = true;
                WriteLog("[Panel] move waypoint saved cell=%d wp=%d hex=%d count=%d",
                    s_move_pick_cell, wp, hex, (int)t.moveWaypointCount);
                EndMovePathPick_();
            }
        }
        return;
    }

    // 循环近战连续拾取：第一击保存站立格并保持面板隐藏；第二击选择攻击格。
    // 两格必须不同且相邻；成功后一次性覆盖/追加组合并恢复面板。
    // 右键任一阶段均取消且不改原记录。
    if (s_melee_pick_phase != 0 && s_melee_pick_cell >= 0
        && s_melee_pick_cell < CELL_COUNT) {
        if (right_click) {
            EndMeleePick_();
            return;
        }
        CellControl* ctrl = &s_p.cells[s_melee_pick_cell];
        if (ctrl->has_data && CellControl_HexValid(hex)
            && s_melee_pick_pair >= 0
            && s_melee_pick_pair < MELEE_PAIR_CAPACITY) {
            AutoTargetRule& target = ctrl->data.rule.target;
            if (s_melee_pick_phase == 1) {
                s_melee_pick_stand_hex = hex;
                s_melee_pick_phase = 2;
                WriteLog("[Panel] melee pair stand hex=%d cell=%d pair=%d; wait attack",
                    hex, s_melee_pick_cell, s_melee_pick_pair);
                // 立即画一次，不等下一帧 BltComplete。
                DrawMeleePickMarker_();
            } else if (s_melee_pick_phase == 2) {
                if (hex == s_melee_pick_stand_hex
                    || !CellControl_HexAdjacent(s_melee_pick_stand_hex, hex)) {
                    int neighbors[6] = {};
                    const int nn = CellControl_HexNeighbors(s_melee_pick_stand_hex, neighbors);
                    WriteLog("[Panel] melee pair attack 非相邻或相同 hex=%d stand=%d nb=[%d,%d,%d,%d,%d,%d] n=%d 忽略",
                        hex, s_melee_pick_stand_hex,
                        nn > 0 ? neighbors[0] : -1,
                        nn > 1 ? neighbors[1] : -1,
                        nn > 2 ? neighbors[2] : -1,
                        nn > 3 ? neighbors[3] : -1,
                        nn > 4 ? neighbors[4] : -1,
                        nn > 5 ? neighbors[5] : -1,
                        nn);
                    return;
                }

                const int pair = s_melee_pick_pair;
                const bool append = pair == target.meleePairCount;
                if (pair < target.meleePairCount || append) {
                    target.meleeStandHexes[pair] =
                        static_cast<int16_t>(s_melee_pick_stand_hex);
                    target.meleeAttackHexes[pair] = static_cast<int16_t>(hex);
                    if (append && target.meleePairCount < MELEE_PAIR_CAPACITY)
                        ++target.meleePairCount;
                    // 同步旧版单组兼容镜像。
                    if (target.meleePairCount > 0) {
                        target.meleeStandHex = target.meleeStandHexes[0];
                        target.meleeAttackHex = target.meleeAttackHexes[0];
                    }
                    ctrl->dirty = true;
                    WriteLog("[Panel] melee pair saved cell=%d pair=%d stand=%d attack=%d count=%d",
                        s_melee_pick_cell, pair, s_melee_pick_stand_hex, hex,
                        (int)target.meleePairCount);
                    EndMeleePick_();
                }
            }
        }
        return;
    }
}

static void UpdatePanelModalSuspension_()
{
    if (!s_p.active) return;
    // 战场拾取模式（循环移动路径 / 近战选格）激活期间，面板由拾取逻辑
    // 手动挂起（s_panel_modal_suspended=true）以让出战场点击。此时不能让
    // 本函数按“无系统模态”把挂起状态重置回 false，否则会立刻重装输入拦截、
    // 吃掉战场点击，导致拾取永远收不到坐标、反复重进选格模式。
    if (s_move_pick_cell >= 0 || s_melee_pick_phase != 0) return;
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
            WriteLog("[Life] CPResult 结果窗出现，等待接受/取消重打。");
        }

        // 边沿：在结果窗上按下鼠标左键时记录命中按钮。
        const bool ldown = IsGameMouseInputActive_()
            && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (ldown) {
            const INT32 id = GetDlgItemUnderCursor_(result_dlg);
            if (id == s_cpresult_ok_id) {
                if (!s_result_accept_armed)
                    WriteLog("[Life] 结果窗命中确定/接受 id=0x%X。", id);
                s_result_accept_armed = true;
                s_result_cancel_armed = false;
                H3AutoPolicy::ApplyResultLifecycle(
                    &s_result_lifecycle, H3AutoPolicy::RESULT_ACCEPT_CLICKED);
            } else if (id == s_cpresult_cancel_id) {
                if (!s_result_cancel_armed)
                    WriteLog("[Life] 结果窗命中取消/重打 id=0x%X。", id);
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
    } else if (!s_result_accept_armed && !s_result_cancel_armed) {
        const DWORD now = GetTickCount();
        if (s_result_closed_since == 0)
            s_result_closed_since = now;
        if (now - s_result_closed_since < RESULT_CLOSE_GRACE_MS)
            return;
    }

    const H3AutoPolicy::ResultLifecycleAction lifecycle_action =
        H3AutoPolicy::ApplyResultLifecycle(
            &s_result_lifecycle,
            battle_ui_exists ? H3AutoPolicy::RESULT_CLOSED_WITH_BATTLE_UI
                             : H3AutoPolicy::RESULT_CLOSED_WITHOUT_BATTLE_UI);

    if (lifecycle_action == H3AutoPolicy::RESULT_KEEP_AND_REBIND) {
        WriteLog("[Life] 取消/重打：保留 5 套方案并重绑跟踪。");
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
        WriteLog("[Life] 接受战斗结果：清除设置。 accept=%d cancel=%d battle_ui=%d",
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
        s_battle_ui_missing_frames = 0;
        // 状态机事件源（S1）：战斗 UI 出现。
        if (g_phase == BP_PEACE)
            SetPhase_(BP_COMBAT_CLOSED, BE_BATTLE_UI_APPEARED);
        // S8：打开面板热键消费（面板关分支；与右键路径同构：先开后迁）。
        if (g_auto_state.kb_open_panel_seen) {
            g_auto_state.kb_open_panel_seen = false;
            if (g_phase == BP_COMBAT_CLOSED && !IsPanelActive()) {
                WriteLog("[Panel] OpenSettings hotkey");
                OpenSettingsPanel_();
                if (s_p.active)
                    SetPhase_(BP_COMBAT_OPEN, BE_PANEL_OPEN_REQUESTED);
            }
        }
    } else if (++s_battle_ui_missing_frames >= 3) {
        // 状态机事件源（S1/S4）：战斗 UI 消失兜底（读档/中途退出等未经结算）。
        // 重打宽限期内不视为兜底（UI 重建常超 3 帧，见重构步骤 S4.2）。
        if ((InCombat_() || g_phase == BP_RESULT)
            && GetTickCount() > g_ui_gone_grace_until)
            SetPhase_(BP_ENDED, BE_BATTLE_UI_GONE);
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

// ========================================================================
// 第六部分：绘制
// ========================================================================

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
        "方案：5 套本场有效，点勾号才生效",
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

static void GetSpellKeyModalRect_(int* out_x, int* out_y, int* out_w, int* out_h)
{
    const int w = 360;
    const int h = 164;
    if (out_x) *out_x = (PANEL_W - w) / 2;
    if (out_y) *out_y = (PANEL_H - h) / 2;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

// ===== 保活策略下拉行（方案级，随当前选中方案的草稿） =====
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
    WriteLog("[Panel] 停止回合=%d (方案%d)", value, s_p.selected_profile + 1);
}

static void CancelStopTurnsEdit_()
{
    s_stop_turns_editing = false;
    s_stop_turns_text[0] = 0;
    s_stop_turns_caret = 0;
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

    if (s_status_text[0]) {
        if (GetTickCount() >= s_status_until)
            s_status_text[0] = 0;
        else
            DrawTxt(scr, GetSmallFont(), s_status_text,
                20, BTN_Y + BTN_H + 14, PANEL_W - 40, 20,
                s_status_error ? (INT32)eTextColor::RED
                               : (INT32)eTextColor::LIGHT_GREEN,
                eTextAlignment::MIDDLE_CENTER);
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
            WriteLog("[Panel] 拾取隐藏：已请求战场重绘覆盖面板");
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
    return H3AutoPolicy::IsConfigurablePanelStack(
        stack.type, stack.numberAlive, has_ballistics);
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
    // 方案切换后，全量快照与可见行都按新方案草稿重绑。
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

// 枚举本场人类侧 21 槽部队表（存档部队表 + 读档关联的当前侧）。
// 初始数量的权威来源是英雄军队 H3Hero::army（战斗中不写回，战后才结算
// 伤亡，全程保持战前值——ALT+右键看英雄初始部队即读这里）；战斗单位
// 的 slotIndex 就是军队槽 0..6 下标，按它回查。召唤物/克隆/战争机器
// 不在军队里，退回 numberAtStart。空槽写 -1/0；界面不显示初始数量。
static void BuildPanelArmyTable_(int out_types[21], int out_counts[21])
{
    for (int i = 0; i < 21; ++i) { out_types[i] = -1; out_counts[i] = 0; }
    H3CombatManager* mgr = GetCombatMgr();
    if (!mgr) return;
    int side = 0;
    if (mgr->isHuman[0]) side = 0;
    else if (mgr->isHuman[1]) side = 1;
    else return;
    H3Hero* hero = mgr->hero[side];
    for (int i = 0; i < 21; ++i) {
        H3CombatCreature& stack = mgr->stacks[side][i];
        if (stack.type < 0 || stack.numberAlive <= 0) continue;
        out_types[i] = stack.type;
        int initial = -1;
        if (hero) {
            const int army_slot = stack.slotIndex;
            if (army_slot >= 0 && army_slot < 7
                && hero->army.type[army_slot] == stack.type)
                initial = hero->army.count[army_slot];
        }
        if (initial <= 0)
            initial = (stack.numberAtStart > 0)
                ? stack.numberAtStart : stack.numberAlive;
        out_counts[i] = initial;
    }
}

// 存档：把当前草稿（含未回写的可见行）写入 DLL 同目录 H3Auto.profiles。
// 读档：读回 5 套方案草稿并刷新当前方案的卡片。两者都不改生效方案、不暂停。
static void SaveProfilesToDisk_()
{
    // WriteLog("[Panel] 保存入口：s_p=%p active=%d count=%d profile=%d",
    //     &s_p, s_p.active ? 1 : 0, s_p.count, s_p.selected_profile);
    SaveCurrentCellsToDraft_();
    int army_types[21] = {};
    int army_counts[21] = {};
    BuildPanelArmyTable_(army_types, army_counts);
    const bool ok = SaveProfileStore_(army_types, army_counts,
        s_p.draft_rules, s_p.draft_protect_strategy, s_p.draft_stop_turns);
    WriteLog("[Panel] 方案%s：%s", ok ? "已存档" : "存档失败", g_profiles_path);
    snprintf(s_status_text, sizeof(s_status_text), "%s",
        ok ? "存档成功" : "存档失败");
    s_status_error = !ok;
    s_status_until = GetTickCount() + 5000;
    DrawPanelToBuffer_();
}

static void LoadProfilesFromDisk_()
{
    // 105 条规则约 8KB，堆分配避免游戏线程栈溢出。
    AutoStackRule (*loaded)[MAX_STACKS] = new AutoStackRule[PROFILE_COUNT][MAX_STACKS]();
    uint8_t strategies[PROFILE_COUNT] = {};
    uint16_t stop_turns[PROFILE_COUNT] = {};
    int arch_types[21] = {};
    int arch_counts[21] = {};
    const bool ok = LoadProfileStore_(arch_types, arch_counts, loaded,
        strategies, stop_turns);
    if (ok) {
        // 三轮关联：存档部队 → 当前部队槽。未匹配的当前槽保留原草稿
        // （含打开面板时的初始化），未匹配的存档规则直接丢弃。
        int cur_types[21] = {};
        int cur_counts[21] = {};
        BuildPanelArmyTable_(cur_types, cur_counts);
        int arch_for_cur[21] = {};
        H3AutoPolicy::BuildArchiveSlotMapByRounds(arch_types, arch_counts,
            cur_types, cur_counts, arch_for_cur);
        int matched = 0;
        for (int cur = 0; cur < 21; ++cur) {
            const int arch = arch_for_cur[cur];
            if (arch < 0) continue;
            ++matched;
            for (int p = 0; p < PROFILE_COUNT; ++p)
                s_p.draft_rules[p][cur] = loaded[p][arch];
        }
        memcpy(s_p.draft_protect_strategy, strategies,
            sizeof(s_p.draft_protect_strategy));
        memcpy(s_p.draft_stop_turns, stop_turns, sizeof(s_p.draft_stop_turns));
        s_stop_turns_editing = false;
        for (int k = 0; k < CELL_COUNT; ++k) {
            s_p.cells[k].expanded = CEX_NONE;
            s_p.cells[k].dirty = true;
        }
        s_protect_dd_open = false;
        s_protect_dd_hover = -1;
        LoadSelectedProfileIntoCells_();
        WriteLog("[Panel] 读档关联：三轮匹配 %d/21 槽，未匹配存档槽已忽略",
            matched);
    }
    delete[] loaded;
    WriteLog("[Panel] 方案%s：%s", ok ? "已读档" : "读档失败（文件不存在或损坏）",
        g_profiles_path);
    snprintf(s_status_text, sizeof(s_status_text), "%s",
        ok ? "读档成功" : "读档失败");
    s_status_error = !ok;
    s_status_until = GetTickCount() + 5000;
    DrawPanelToBuffer_();
}

static void SelectProfile_(int profile)
{
    if (profile < 0 || profile >= PROFILE_COUNT
        || profile == s_p.selected_profile)
        return;
    SaveCurrentCellsToDraft_();
    if (s_stop_turns_editing) CommitStopTurnsEdit_();
    s_p.selected_profile = profile;
    s_protect_dd_open = false;   // 策略下拉跟随方案切换，收起重开
    s_protect_dd_hover = -1;
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
    s_protect_dd_open = false;
    s_protect_dd_hover = -1;
    if (!BlockBattleHover_()) {
        s_p.cursor_saved = false;
        WriteLog("[Panel] 无法屏蔽战场悬停，取消打开设置面板。");
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
    s_p.selected_profile = g_active_profile;
    if (s_p.selected_profile < 0 || s_p.selected_profile >= PROFILE_COUNT)
        s_p.selected_profile = 0;
    memcpy(s_p.draft_rules, g_profiles, sizeof(s_p.draft_rules));
    memcpy(s_p.draft_protect_strategy, g_protect_strategy,
        sizeof(s_p.draft_protect_strategy));
    memcpy(s_p.draft_stop_turns, g_stop_turns, sizeof(s_p.draft_stop_turns));
    s_stop_turns_editing = false;
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
    // WriteLog("[Panel] 打开阶段：开始枚举部队");
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
    }
    RebindVisibleCells_();
    // WriteLog("[Panel] 打开阶段：部队枚举完成 count=%d", s_p.count);
    InstallBattleInputBlocker_();
    // WriteLog("[Panel] 打开阶段：输入屏障完成");
    EnsurePanelButtonPcxResources_();
    ForcePanelDefaultCursor_();
    // WriteLog("[Panel] 打开阶段：开始绘制");
    DrawPanelToBuffer_();
    // WriteLog("[Panel] 打开阶段：绘制完成");
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
    // WriteLog("[Panel] 打开设置面板 count=%d at (%d,%d) s_p=%p",
    //     s_p.count, s_p.x, s_p.y, &s_p);
}

void RefreshSettingsPanel() { if (s_p.active) DrawPanelToBuffer_(); }

static void CommitAndCloseSettingsPanel_()
{
    if (!s_p.active) return;

    // 保存当前表格到当前方案副本，再一次性提交全部5套。
    // 保活勾选在 AutoStackRule 内随 draft_rules 一起提交；
    // 保活策略是方案级，随 draft_protect_strategy 提交。
    SaveCurrentCellsToDraft_();
    CommitProfiles(s_p.selected_profile, s_p.draft_rules,
        s_p.draft_protect_strategy, s_p.draft_stop_turns);
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
    s_protect_dd_open = false;
    s_protect_dd_hover = -1;
    if (s_stop_turns_editing) CancelStopTurnsEdit_();
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
    // 保活策略下拉（方案级）同卡片下拉：由本钩子即时更新悬停项。
    int new_dd_hover = -1;
    if (s_protect_dd_open) {
        for (int i = 0; i < (int)H3AutoPolicy::PS_COUNT; ++i) {
            int ix = 0, iy = 0, iw = 0, ih = 0;
            GetProtectDdItemRect_(i, &ix, &iy, &iw, &ih);
            if (PointInRect_(px, py, ix, iy, iw, ih)) {
                new_dd_hover = i;
                break;
            }
        }
    }
    if (new_dd_hover != s_protect_dd_hover) {
        s_protect_dd_hover = new_dd_hover;
        changed = true;
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

    // 帮助模态框打开时：吞掉所有底层点击，仅允许点关闭。
    if (s_help_modal_open) {
        if (raw_command == 16) {
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
        if (!PointInRect_(px, py, STOP_BOX_X, PROTECT_DD_Y,
                STOP_BOX_W, PROTECT_DD_H) && raw_command == 16) {
            CommitStopTurnsEdit_();
            DrawPanelToBuffer_();
        } else if (raw_command == 8
            && PointInRect_(px, py, STOP_BOX_X, PROTECT_DD_Y,
                STOP_BOX_W, PROTECT_DD_H)) {
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
        return;
    }

    // 保活策略下拉展开时：点击项即选中收起，点收起框本身保持展开，
    // 点其他任意处收起（吞掉，不穿透到底层）。
    // 悬停高亮不在此处理：由 WH_MOUSE 钩子的 UpdateDropdownHover_ 即时更新。
    if (s_protect_dd_open) {
        if (raw_command == 4)
            return;
        if (raw_command == 8 || raw_command == 16) {
            for (int i = 0; i < (int)H3AutoPolicy::PS_COUNT; ++i) {
                int ix = 0, iy = 0, iw = 0, ih = 0;
                GetProtectDdItemRect_(i, &ix, &iy, &iw, &ih);
                if (PointInRect_(px, py, ix, iy, iw, ih)) {
                    s_p.draft_protect_strategy[s_p.selected_profile] =
                        (uint8_t)i;
                    s_protect_dd_open = false;
                    s_protect_dd_hover = -1;
                    WriteLog("[Panel] 保活策略=%d (方案%d)", i,
                        s_p.selected_profile + 1);
                    DrawPanelToBuffer_();
                    return;
                }
            }
            // 点在下拉框自身：松开（16）是“展开”这次点击的收尾，保持展开；
            // 再次按下（8）则收起（与卡片下拉一致的切换语义）。
            if (PointInRect_(px, py, PROTECT_DD_X, PROTECT_DD_Y,
                    PROTECT_DD_W, PROTECT_DD_H)) {
                if (raw_command == 8) {
                    s_protect_dd_open = false;
                    s_protect_dd_hover = -1;
                    DrawPanelToBuffer_();
                }
                return;
            }
            s_protect_dd_open = false;
            s_protect_dd_hover = -1;
            DrawPanelToBuffer_();
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
    if (raw_command == 64
        || raw_command == static_cast<int>(eMsgCommand::RBUTTON_UP)) {
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

    if (raw_command == 8) {
        // 保活策略下拉（收起态）：点击展开；先收起卡片下拉避免层级重叠。
        if (PointInRect_(px, py, PROTECT_DD_X, PROTECT_DD_Y,
                PROTECT_DD_W, PROTECT_DD_H)) {
            for (int k = 0; k < CELL_COUNT; ++k) {
                s_p.cells[k].expanded = CEX_NONE;
                s_p.cells[k].dirty = true;
            }
            s_protect_dd_open = true;
            s_protect_dd_hover = -1;
            DrawPanelToBuffer_();
            return;
        }
        if (PointInRect_(px, py, STOP_BOX_X, PROTECT_DD_Y,
                STOP_BOX_W, PROTECT_DD_H)) {
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
        else if (PointInRect_(px, py, LOAD_BTN_X, HELP_BTN_Y, STORE_BTN_W, HELP_BTN_SIZE))
            button = 4;
        else if (PointInRect_(px, py, SAVE_BTN_X, HELP_BTN_Y, STORE_BTN_W, HELP_BTN_SIZE))
            button = 5;
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
                        // 循环移动：已有路径点可覆盖重设，末尾「＋」追加。
                        // 隐藏面板后只点 1 格即回写对应槽位。
                        if (ctrl->move_path_pick_request != 0) {
                            s_move_pick_wp = ctrl->move_path_pick_request - 1;
                            s_move_pick_cell = i;
                            s_panel_hidden_for_pick = true;
                            s_pick_wait_button_release = true;
                            HidePanelForPick_();
                            WriteLog("[Panel] 进入循环移动拾取 cell=%d wp=%d",
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
                            WriteLog("[Panel] 弹出循环施法快捷键模态框 cell=%d slot=%d",
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
                            WriteLog("[Panel] 进入循环近战拾取 cell=%d pair=%d",
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
            activate = PointInRect_(px, py, LOAD_BTN_X, HELP_BTN_Y,
                STORE_BTN_W, HELP_BTN_SIZE);
        else if (pressed == 5)
            activate = PointInRect_(px, py, SAVE_BTN_X, HELP_BTN_Y,
                STORE_BTN_W, HELP_BTN_SIZE);
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
                s_protect_dd_open = false;
                s_protect_dd_hover = -1;
                s_help_modal_open = true;
                WriteLog("[Panel] 打开帮助说明模态框");
                DrawPanelToBuffer_();
            } else if (pressed == 4) {
                LoadProfilesFromDisk_();
                DrawPanelToBuffer_();
            } else if (pressed == 5) {
                SaveProfilesToDisk_();
                DrawPanelToBuffer_();
            }
            return;
        }
        if (redraw) DrawPanelToBuffer_();
        if (pressed != 0)
            return;

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
    static bool previous_digit_down[10] = {};

    const bool up_down = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    const bool down_down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    const bool page_up_down = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
    const bool page_down_down = (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
    // 停止回合编辑态例外：输入法常驻窗会盖住游戏窗口（modal_depth>=2 且
    // 前台判定失败），两条键盘路径都被掐死。GetAsyncKeyState 不依赖焦点，
    // 编辑态放行；滚屏/翻页仍受前台判定保护。
    if (!IsGameWindowForeground_() && !s_stop_turns_editing) {
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
