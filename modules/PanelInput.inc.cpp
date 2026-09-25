// PanelInput.inc.cpp - 设置面板的输入捕获层：WH_KEYBOARD/WH_MOUSE 钩子、
// HD 悬停屏蔽、BattleUI 输入屏障、三种战场拾取（移动路径/法术槽/近战）。
// 在 H3Auto.cpp 中排在 SettingsDlg.inc.cpp 之后：面板状态与工具函数
// 来自 SettingsDlg，反向被调用的函数经 PanelInput.hpp 声明。

static void WriteLog(const char* fmt, ...);

void OpenSettingsPanel_();
static void CommitAndCloseSettingsPanel_();
void CloseSettingsPanel();
static void DrawPanelToBuffer_();
void HandlePanelInput_();
static void HandlePanelMouseMessage_(int raw_command, int screen_x, int screen_y);
static bool UpdateDropdownHover_(int px, int py);
static bool BlockBattleHover_();
static void RestoreBattleHover_();
static void UpdatePanelModalSuspension_();

static void DoPickCapture_(int hex, bool right_click);
static void HidePanelForPick_();
static H3CombatManager* GetCombatMgr();

static Patch* s_hover_patch_primary = nullptr;
static Patch* s_hover_patch_secondary = nullptr;

// HD_SOD.dll 高亮屏蔽：patch HD 战斗消息钩子 FUN_010d9ce0 (RVA 0xD9CE0) 为 ret 12
static HMODULE s_hd_sod_module = nullptr;
static Patch* s_hd_msgproc_patch = nullptr;


static bool UpdateDropdownHover_(int px, int py);  // 前向声明（钩子先用到）

// 循环近战连续拾取：phase=1 选站立格，phase=2 选相邻攻击格。
// 点「＋」/编辑路径是在左键按下时进入拾取；同一次点击的松开不得当作战场第一击。

// 循环移动路径点拾取：与循环近战同交互，但每次只点 1 格。
// s_move_pick_cell 为卡片索引，s_move_pick_wp 为路径点槽 0..5。
static void EndMovePathPick_();
// 循环施法录入：弹出模态框，仅按 1-9/0 记录同一数字。
static void DoPickCapture_(int hex, bool right_click);


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

