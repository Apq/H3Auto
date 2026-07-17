// AutoExecute.inc.cpp
// Automated battle execution module

static void WriteLog(const char* fmt, ...);

extern void CommitProfiles(int active_profile, AutoStackRule rules[5][21]);
extern void ClearConfirmedProfiles();
extern AutoStackRule g_profiles[5][21];
extern AutoStackRule g_active_rules[21];
extern int  g_active_profile;
extern bool IsPanelActive();
extern void CloseSettingsPanel();

// 接管模型（收敛后）：
// 1) 只在“控制权交给玩家”时介入（HH_ShouldAutoExecute 返回 0 的路径）。
//    被蛊惑/敌方回合等本就不会把控制权交给玩家，无需单独状态机。
// 2) 普通部队：有非手动设置 → 代为提交动作；手动 → 原样留给玩家。
// 3) 仅战争机器有特殊分支（技能条件 / 交回 AI / 可选主动执行）。
// last_handled_stack 防止同一活动单位在一个回合内被重复下达命令。
// 循环施法两阶段：先投递快捷键 1-9/0，等英雄施法结束（或超时）再提交部队动作。
static struct {
    void* last_handled_stack;   // 上次已处理的活动单位指针
    void* action_wake_stack;    // 已投递唤醒消息、等待在 0x4746B0 提交主动作的单位
    void* spell_wait_stack;     // 等待快捷施法完成的活动单位
    void* spell_done_stack;     // 本单位本回合施法阶段已结束，避免重复投键
    int   spell_wait_slot;      // 对应 army_slot
    int   spell_wait_key;       // 已投递的快捷键 0..9
    int   spell_wait_frames;    // 已等待帧数
    DWORD spell_wait_started;   // GetTickCount，避免高频 BltComplete 把调用次数误当帧数
    int   spell_mana_before;    // 投递前法力
    int   spell_casted_before;  // 投递前 hero_casted[side]
    bool  spell_waiting;        // true=已投递快捷键，等待结果

    // 战斗级人工接管：只改执行权，不改 5 套方案/游标。
    bool  battle_manual;        // true=本场全手动
    bool  oneshot_active;       // true=单次接管锁定中
    void* oneshot_stack;        // 锁定到的活动部队指针
    int   oneshot_side;         // 锁定部队 side
    int   oneshot_slot;         // 锁定部队 army_slot
    int   oneshot_creature;     // 锁定部队 creature_id
    bool  oneshot_pending;      // 按住 Ctrl 时若当前不可接管，则等下一支
    bool  prev_toggle_down;     // F11 边沿
    bool  prev_oneshot_down;    // Ctrl 边沿
    char  last_status_text[64]; // 状态提示去重
} g_auto_state;

// 当前战斗的人类侧部队跟踪表（辅助正确套用设置，不是第二套控制权逻辑）。
// 设置提交时绑定“槽位 + 生物类型”；之后刷新存活/位置/数量。
// 执行前校验身份，避免仅凭 army_slot_ix 套错（死亡、召唤复用槽等）。
struct StackTrackEntry {
    bool  bound;         // 本场是否已绑定
    bool  alive;         // 当前是否存活
    int   side;          // 0/1
    int   slot;          // 0..20
    int   creature_id;   // 绑定身份
    int   hex;           // 当前位置
    int   count_alive;   // 当前数量
    int   count_start;   // 绑定时刻/开战数量
    // 本场运行游标：配置只保存序列，进度只存在跟踪表。
    int   move_cursor;
    int   melee_cursor;
    int   spell_cursor;
};
static StackTrackEntry g_stack_track[21] = {};
static int  g_track_side = -1;
static bool g_track_active = false;

static int ResolveHumanSide_(_BattleMgr_* mgr)
{
    if (!mgr) return -1;
    // 优先走 H3API 的 isHuman；Compat 结构体没有该字段。
    __try {
        if (H3CombatManager* cm = H3CombatManager::Get()) {
            if (cm->isHuman[0]) return 0;
            if (cm->isHuman[1]) return 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (mgr->current_active_side == 0 || mgr->current_active_side == 1)
        return mgr->current_active_side;
    return 0;
}

static void ClearStackTracking_()
{
    memset(g_stack_track, 0, sizeof(g_stack_track));
    g_track_side = -1;
    g_track_active = false;
}

// 从当前战场绑定人类侧所有“开战时存在”的部队身份。
static void BindStackTrackingFromBattle_()
{
    ClearStackTracking_();
    _BattleMgr_* mgr = o_BattleMgr;
    if (!mgr) {
        WriteLog("[Track] bind skipped: no battle manager");
        return;
    }

    const int side = ResolveHumanSide_(mgr);
    if (side < 0 || side > 1) {
        WriteLog("[Track] bind skipped: invalid human side");
        return;
    }

    int bound = 0;
    int configured_action = 0;
    int configured_spell = 0;
    for (int i = 0; i < 21; ++i) {
        _BattleStack_* s = &mgr->stack[side][i];
        // 本场从未上场的空槽跳过。
        if (s->count_at_start <= 0 && s->count_current <= 0) continue;
        if (s->creature_id < 0) continue;

        StackTrackEntry& t = g_stack_track[i];
        t.bound = true;
        t.side = side;
        t.slot = i;
        t.creature_id = s->creature_id;
        t.hex = s->hex_ix;
        t.count_alive = s->count_current;
        t.count_start = (s->count_at_start > 0) ? s->count_at_start : s->count_current;
        t.alive = s->count_current > 0;
        t.move_cursor = 0;
        t.melee_cursor = 0;
        t.spell_cursor = 0;
        ++bound;
        if (g_active_rules[i].action != AA_MANUAL)
            ++configured_action;
        if (g_active_rules[i].spellSlotCount > 0)
            ++configured_spell;

        WriteLog("[Track] bind slot=%d cid=0x%X hex=%d alive=%d count=%d/%d action=%d spells=%d first=%d",
            i, t.creature_id, t.hex, t.alive ? 1 : 0,
            t.count_alive, t.count_start, (int)g_active_rules[i].action,
            (int)g_active_rules[i].spellSlotCount,
            (g_active_rules[i].spellSlotCount > 0)
                ? (int)g_active_rules[i].spellSlots[0] : -1);
    }

    g_track_side = side;
    g_track_active = bound > 0;
    WriteLog("[Track] bound side=%d stacks=%d actions=%d spells=%d",
        side, bound, configured_action, configured_spell);
}

// 刷新跟踪表：存活、位置、数量；身份变化则标记死亡并解除可用。
static void UpdateStackTracking_()
{
    if (!g_track_active || g_track_side < 0 || g_track_side > 1)
        return;
    _BattleMgr_* mgr = o_BattleMgr;
    if (!mgr) return;

    for (int i = 0; i < 21; ++i) {
        StackTrackEntry& t = g_stack_track[i];
        if (!t.bound) continue;

        _BattleStack_* s = &mgr->stack[t.side][i];
        if (s->creature_id != t.creature_id) {
            // 槽位被复用/清空：本绑定失效。
            if (t.alive) {
                WriteLog("[Track] identity lost slot=%d expect=0x%X got=0x%X",
                    i, t.creature_id, s->creature_id);
            }
            t.alive = false;
            t.count_alive = 0;
            t.hex = -1;
            continue;
        }

        const int prev_alive = t.count_alive;
        const int prev_hex = t.hex;
        t.hex = s->hex_ix;
        t.count_alive = s->count_current;
        t.alive = s->count_current > 0;

        if (prev_alive > 0 && !t.alive) {
            WriteLog("[Track] dead slot=%d cid=0x%X last_hex=%d",
                i, t.creature_id, prev_hex);
        } else if (t.alive && (prev_hex != t.hex || prev_alive != t.count_alive)) {
            // 位置/数量变化仅在调试时有用；降噪：只在数量变化时打日志。
            if (prev_alive != t.count_alive) {
                WriteLog("[Track] update slot=%d cid=0x%X hex=%d count=%d",
                    i, t.creature_id, t.hex, t.count_alive);
            }
        }
    }
}

// 当前活动单位是否仍对应跟踪表中的那支人类侧部队。
static bool ActiveStackMatchesTrack_(_BattleStack_* self)
{
    if (!self) return false;
    // 尚未绑定本场跟踪时：仅允许人类侧活动单位走旧逻辑。
    if (!g_track_active) {
        _BattleMgr_* mgr = o_BattleMgr;
        const int human = ResolveHumanSide_(mgr);
        return human >= 0 && self->def_group_ix == human && self->count_current > 0;
    }

    const int idx = self->army_slot_ix;
    if (idx < 0 || idx >= 21) return false;
    const StackTrackEntry& t = g_stack_track[idx];
    if (!t.bound || !t.alive) return false;
    if (self->def_group_ix != t.side) return false;
    if (self->creature_id != t.creature_id) return false;
    if (self->count_current <= 0) return false;
    return true;
}

// 战争机器 creature id（原版内部编号）
enum WarMachineId {
    WM_CATAPULT    = 0x91,  // 145 投石车
    WM_BALLISTA    = 0x92,  // 146 弩车
    WM_FIRST_AID   = 0x93,  // 147 急救帐篷
    WM_AMMO_CART   = 0x94,  // 148 弹药车
    WM_ARROW_TOWER = 0x95,  // 149 箭塔
};

// FUN_0046a080：回放/隐藏战斗判定。非 0 表示当前不是本地人类交互，禁止接管。
static bool IsHiddenBattle(_BattleMgr_* mgr)
{
    return THISCALL_1(char, 0x46A080, mgr) != 0;
}

static bool IsTacticsPhase_(_BattleMgr_* mgr)
{
    if (!mgr) return false;
    __try {
        return reinterpret_cast<BYTE*>(mgr)[0x13D68] != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void ClearSpellWait_();

static bool IsGameWindowForegroundForHotkeys_()
{
    HWND game_window = *reinterpret_cast<HWND*>(0x699650);
    if (!game_window || IsIconic(game_window)) return false;
    HWND foreground = GetForegroundWindow();
    return foreground
        && GetAncestor(foreground, GA_ROOT) == GetAncestor(game_window, GA_ROOT);
}

static bool IsHotkeyDown_(int vk)
{
    if (vk <= 0 || vk >= 256) return false;
    // 左右 Ctrl 分别检测；VK_CONTROL 则任意一侧。
    if (vk == VK_CONTROL)
        return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0
            || (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static void ClearOneShotManual_()
{
    g_auto_state.oneshot_active = false;
    g_auto_state.oneshot_stack = nullptr;
    g_auto_state.oneshot_side = -1;
    g_auto_state.oneshot_slot = -1;
    g_auto_state.oneshot_creature = -1;
    g_auto_state.oneshot_pending = false;
}

static void ShowControlStatus_(const char* text)
{
    if (!text || !text[0]) return;
    if (strncmp(g_auto_state.last_status_text, text,
            sizeof(g_auto_state.last_status_text) - 1) == 0)
        return;
    strncpy(g_auto_state.last_status_text, text,
        sizeof(g_auto_state.last_status_text) - 1);
    g_auto_state.last_status_text[sizeof(g_auto_state.last_status_text) - 1] = 0;

    // 游戏字体走 GBK；源码是 UTF-8，显示前转换。
    char gbk[128] = {};
    const char* show = text;
    bool need_convert = false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p >= 0x80) { need_convert = true; break; }
    }
    if (need_convert) {
        wchar_t wide[128] = {};
        if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, _countof(wide)) > 0
            && WideCharToMultiByte(936, 0, wide, -1, gbk, sizeof(gbk), nullptr, nullptr) > 0)
            show = gbk;
    }

    // 优先写战斗提示栏；失败只记日志。
    __try {
        if (H3CombatManager* cm = H3CombatManager::Get()) {
            if (cm->dlg) {
                cm->dlg->ShowHint(show, FALSE);
                WriteLog("[Control] status shown: %s", text);
                return;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    WriteLog("[Control] status (no dlg): %s", text);
}

static const char* ControlModeLabel_()
{
    if (g_auto_state.oneshot_active) return "单次接管";
    if (g_auto_state.oneshot_pending) return "单次待命";
    if (g_auto_state.battle_manual) return "全手动";
    return "自动";
}

static void RefreshControlStatusHint_()
{
    char buf[64] = {};
    _snprintf(buf, sizeof(buf) - 1, "打铁助手: %s", ControlModeLabel_());
    ShowControlStatus_(buf);
}

// 当前是否已有本插件提交/等待中的动作，不能中途打断。
static bool HasInFlightAutoAction_(_BattleMgr_* mgr)
{
    if (!mgr) return false;
    if (g_auto_state.spell_waiting) return true;
    if (g_auto_state.action_wake_stack) return true;
    if (mgr->action != 0 && g_auto_state.last_handled_stack
        && mgr->active_stack == g_auto_state.last_handled_stack)
        return true;
    return false;
}

static bool ActiveStackIdentityMatches_(_BattleStack_* stack,
    int side, int slot, int creature)
{
    if (!stack) return false;
    if (side < 0 || slot < 0 || creature < 0) return false;
    return stack->def_group_ix == side
        && stack->army_slot_ix == slot
        && stack->creature_id == creature
        && stack->count_current > 0;
}

static bool TryArmOneShotOnStack_(_BattleMgr_* mgr, _BattleStack_* stack, bool from_pending)
{
    if (!mgr || !stack) return false;
    if (!ActiveStackMatchesTrack_(stack)) return false;
    if (HasInFlightAutoAction_(mgr)) {
        // 已投递施法/动作：不能半途打断，记为待命，等下一支。
        g_auto_state.oneshot_pending = true;
        WriteLog("[Control] oneshot deferred (in-flight) slot=%d action=%d spell_wait=%d",
            stack->army_slot_ix, mgr->action, g_auto_state.spell_waiting ? 1 : 0);
        return false;
    }

    g_auto_state.oneshot_active = true;
    g_auto_state.oneshot_stack = stack;
    g_auto_state.oneshot_side = stack->def_group_ix;
    g_auto_state.oneshot_slot = stack->army_slot_ix;
    g_auto_state.oneshot_creature = stack->creature_id;
    g_auto_state.oneshot_pending = false;
    // 单次接管期间清掉本单位自动状态，避免残留游标/等待干扰人工。
    if (g_auto_state.spell_waiting && g_auto_state.spell_wait_stack == stack)
        ClearSpellWait_();
    if (g_auto_state.spell_done_stack == stack)
        g_auto_state.spell_done_stack = nullptr;
    if (g_auto_state.action_wake_stack == stack)
        g_auto_state.action_wake_stack = nullptr;
    if (g_auto_state.last_handled_stack == stack)
        g_auto_state.last_handled_stack = nullptr;

    WriteLog("[Control] oneshot armed%s side=%d slot=%d cid=0x%X",
        from_pending ? " (pending)" : "",
        g_auto_state.oneshot_side, g_auto_state.oneshot_slot,
        g_auto_state.oneshot_creature);
    RefreshControlStatusHint_();
    return true;
}

static void ArmOneShotManual_(_BattleMgr_* mgr)
{
    if (!mgr) {
        g_auto_state.oneshot_pending = true;
        RefreshControlStatusHint_();
        return;
    }
    if (IsTacticsPhase_(mgr) || IsHiddenBattle(mgr) || mgr->auto_combat) {
        g_auto_state.oneshot_pending = true;
        WriteLog("[Control] oneshot pending: tactics/hidden/auto_combat");
        RefreshControlStatusHint_();
        return;
    }
    _BattleStack_* stack = mgr->active_stack;
    if (!stack || stack->count_current <= 0 || !ActiveStackMatchesTrack_(stack)) {
        g_auto_state.oneshot_pending = true;
        WriteLog("[Control] oneshot pending: no matching active stack");
        RefreshControlStatusHint_();
        return;
    }
    if (!TryArmOneShotOnStack_(mgr, stack, false)) {
        g_auto_state.oneshot_pending = true;
        RefreshControlStatusHint_();
    }
}

static void ToggleBattleManual_()
{
    g_auto_state.battle_manual = !g_auto_state.battle_manual;
    // 切到全手动时，单次接管无意义；切回自动时也清掉未完成的单次锁定。
    ClearOneShotManual_();
    WriteLog("[Control] battle_manual=%d", g_auto_state.battle_manual ? 1 : 0);
    RefreshControlStatusHint_();
}

// 每帧/每消息入口：处理热键边沿 + 待命单次接管。
static void PollControlHotkeys_(_BattleMgr_* mgr)
{
    if (IsPanelActive()) {
        // 设置面板打开时不抢热键，只同步边沿，避免关面板后误触发。
        g_auto_state.prev_toggle_down = IsHotkeyDown_(cfg.toggle_manual_vk);
        g_auto_state.prev_oneshot_down = IsHotkeyDown_(cfg.one_shot_manual_vk);
        return;
    }
    if (!IsGameWindowForegroundForHotkeys_()) {
        g_auto_state.prev_toggle_down = IsHotkeyDown_(cfg.toggle_manual_vk);
        g_auto_state.prev_oneshot_down = IsHotkeyDown_(cfg.one_shot_manual_vk);
        return;
    }

    const bool toggle_down = IsHotkeyDown_(cfg.toggle_manual_vk);
    const bool oneshot_down = IsHotkeyDown_(cfg.one_shot_manual_vk);

    if (toggle_down && !g_auto_state.prev_toggle_down)
        ToggleBattleManual_();

    // 全手动时不需要单次接管；松键也不保留 pending。
    if (!g_auto_state.battle_manual) {
        if (oneshot_down && !g_auto_state.prev_oneshot_down) {
            if (!g_auto_state.oneshot_active)
                ArmOneShotManual_(mgr);
        }
        // 待命：下一支可接管人类部队出现时锁定。
        if (g_auto_state.oneshot_pending && !g_auto_state.oneshot_active && mgr) {
            _BattleStack_* stack = mgr->active_stack;
            if (stack && stack->count_current > 0
                && ActiveStackMatchesTrack_(stack)
                && !HasInFlightAutoAction_(mgr))
            {
                TryArmOneShotOnStack_(mgr, stack, true);
            }
        }
    } else if (g_auto_state.oneshot_active || g_auto_state.oneshot_pending) {
        ClearOneShotManual_();
    }

    g_auto_state.prev_toggle_down = toggle_down;
    g_auto_state.prev_oneshot_down = oneshot_down;
}

// 当前是否应把控制权留给玩家（最高优先级门）。
static bool ShouldYieldToPlayer_(_BattleMgr_* mgr)
{
    if (!mgr) return false;
    if (g_auto_state.battle_manual) return true;
    if (!g_auto_state.oneshot_active) return false;
    _BattleStack_* stack = mgr->active_stack;
    if (!stack) return true; // 锁定中但活动单位暂不可见：仍不自动执行
    if (ActiveStackIdentityMatches_(stack,
            g_auto_state.oneshot_side, g_auto_state.oneshot_slot,
            g_auto_state.oneshot_creature))
        return true;
    // 活动单位已变且不是锁定部队：单次接管结束。
    WriteLog("[Control] oneshot expired by active change old_slot=%d new_side=%d new_slot=%d",
        g_auto_state.oneshot_slot, stack->def_group_ix, stack->army_slot_ix);
    ClearOneShotManual_();
    RefreshControlStatusHint_();
    return false;
}

// 原版 0x4786B0 真正开始执行 action 时调用：确认单次接管的人工动作已消费。
int __stdcall HH_OnBattleActionExecute(HiHook* h, _BattleMgr_* This, int flags)
{
    __try {
        // action=1 是英雄施法，不结束单次接管：玩家可先施法再给该部队下命令。
        if (This && g_auto_state.oneshot_active
            && This->action != 0 && This->action != 1)
        {
            _BattleStack_* stack = This->active_stack;
            // 只在锁定部队本人提交动作时结束单次接管。
            // 不把“本插件提交的 last_handled”算作人工动作。
            const bool is_locked = stack
                && ActiveStackIdentityMatches_(stack,
                    g_auto_state.oneshot_side, g_auto_state.oneshot_slot,
                    g_auto_state.oneshot_creature);
            const bool is_auto_submitted = g_auto_state.last_handled_stack
                && g_auto_state.last_handled_stack == stack;
            if (is_locked && !is_auto_submitted) {
                WriteLog("[Control] oneshot completed by player action=%d slot=%d",
                    This->action, stack ? stack->army_slot_ix : -1);
                ClearOneShotManual_();
                RefreshControlStatusHint_();
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return THISCALL_2(int, h->GetDefaultFunc(), This, flags);
}

void ResetAutoState()
{
    if (IsPanelActive())
        CloseSettingsPanel();
    g_auto_state.last_handled_stack = nullptr;
    g_auto_state.action_wake_stack = nullptr;
    g_auto_state.spell_wait_stack = nullptr;
    g_auto_state.spell_done_stack = nullptr;
    g_auto_state.spell_wait_slot = -1;
    g_auto_state.spell_wait_key = -1;
    g_auto_state.spell_wait_frames = 0;
    g_auto_state.spell_wait_started = 0;
    g_auto_state.spell_mana_before = 0;
    g_auto_state.spell_casted_before = 0;
    g_auto_state.spell_waiting = false;
    g_auto_state.battle_manual = false;
    ClearOneShotManual_();
    g_auto_state.prev_toggle_down = false;
    g_auto_state.prev_oneshot_down = false;
    g_auto_state.last_status_text[0] = 0;
    // 策略是本进程内的已确认设置，战斗状态重置时保留；
    // 跟踪表是“当前战斗绑定”，进程重置时清空。
    ClearStackTracking_();
    WriteLog("Auto state reset; confirmed strategies preserved, tracking cleared.");
}

// 玩家点结果窗「确定/接受」：清空 5 套方案 + 运行时状态。
// 「取消/重打」不得调用本函数。
void OnBattleResultAccepted()
{
    ClearConfirmedProfiles();
    ResetAutoState();
    WriteLog("[Life] battle result accepted: profiles+runtime cleared");
}

// 取消重打后战场回来：若仍有生效规则则重新绑定跟踪表。
void EnsureStackTrackingBound()
{
    if (g_track_active) {
        UpdateStackTracking_();
        return;
    }
    bool any = false;
    for (int i = 0; i < 21; ++i) {
        if (g_active_rules[i].action != AA_MANUAL
            || g_active_rules[i].spellSlotCount > 0) {
            any = true;
            break;
        }
    }
    if (any)
        BindStackTrackingFromBattle_();
}

static void ClearSpellWait_()
{
    g_auto_state.spell_waiting = false;
    g_auto_state.spell_wait_stack = nullptr;
    g_auto_state.spell_wait_slot = -1;
    g_auto_state.spell_wait_key = -1;
    g_auto_state.spell_wait_frames = 0;
    g_auto_state.spell_wait_started = 0;
    g_auto_state.spell_mana_before = 0;
    g_auto_state.spell_casted_before = 0;
}

// BltComplete 只负责推进施法状态，不能直接写部队动作：它不在 0x473A00
// 战斗消息循环内，写入 action 后没有执行器接手。异步投递一次鼠标移动，
// 让下一次 0x4746B0 入口提交动作，返回后由 0x473A00 调 0x4786B0 落地。
static void RequestUnitActionWake_(_BattleStack_* self)
{
    if (!self || g_auto_state.action_wake_stack == self)
        return;

    HWND hwnd = *reinterpret_cast<HWND*>(0x699650);
    if (!hwnd) return;

    POINT pt = {};
    if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt)) {
        pt.x = 0;
        pt.y = 0;
    }
    if (PostMessageA(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y))) {
        g_auto_state.action_wake_stack = self;
        WriteLog("[Auto] action wake posted slot=%d hwnd=%p client=(%d,%d)",
            self->army_slot_ix, hwnd, pt.x, pt.y);
    } else {
        WriteLog("[Auto] action wake failed slot=%d hwnd=%p",
            self->army_slot_ix, hwnd);
    }
}

// 把 1-9/0 映射到 H3 战斗消息键码：H3VK_1=2 ... H3VK_9=10, H3VK_0=11。
static int DigitToH3Vk_(int digit)
{
    if (digit >= 1 && digit <= 9) return digit + 1; // 1->2 ... 9->10
    if (digit == 0) return 11; // H3VK_0
    return -1;
}

// SoD_SP 快捷施法槽：1->0 ... 9->8, 0->9。
static int DigitToQuickSpellSlot_(int digit)
{
    if (digit >= 1 && digit <= 9) return digit - 1;
    if (digit == 0) return 9;
    return -1;
}

static int GetHeroMana_(_BattleMgr_* mgr, int side);
static int GetHeroCasted_(_BattleMgr_* mgr, int side);

// 快捷施法实现在 SoD_SP.dll，不在原版 0x4746B0。
// 证据：SoD_SP HiHook 0x473A00 → RVA 0x6B40；数字键 KEY_DOWN 会把
// H3VK_1..0 转成槽位 0..9，再调用 RVA 0x8FE0。这里向游戏窗口投递
// 带真实扫描码的 WM_KEYDOWN/UP，让游戏输入泵在正常时机生成 H3Msg。
static bool TriggerQuickSpellDigit_(int digit)
{
    const int slot = DigitToQuickSpellSlot_(digit);
    const int h3vk = DigitToH3Vk_(digit);
    if (slot < 0 || h3vk < 0) return false;

    HMODULE sod_sp = GetModuleHandleA("SoD_SP.dll");
    if (!sod_sp)
        sod_sp = GetModuleHandleA("SoD_SP");
    if (!sod_sp) {
        WriteLog("[Spell] SoD_SP.dll not loaded; cannot cast quickspell digit=%d",
            digit);
        return false;
    }

    _BattleMgr_* mgr = o_BattleMgr;
    if (!mgr) return false;

    // SoD_SP 1.19.4.2：每槽 12 字节 {spellId, targetHex, targetStackPtr}。
    // 先记录并拒绝空槽；否则内部 RVA 0x8FE0 只会静默返回。
    int spell_id = -1;
    int target_hex = -1;
    void* target_stack = nullptr;
    int spell_flags = -1;
    __try {
        BYTE* entry = reinterpret_cast<BYTE*>(sod_sp) + 0x64918 + slot * 12;
        spell_id = *reinterpret_cast<int*>(entry + 0);
        target_hex = *reinterpret_cast<int*>(entry + 4);
        target_stack = *reinterpret_cast<void**>(entry + 8);
        if (spell_id >= 0 && spell_id < 70) {
            BYTE* spell_table = *reinterpret_cast<BYTE**>(0x687FA8);
            if (spell_table)
                spell_flags = spell_table[spell_id * 0x88 + 0x0C];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[Spell] cannot inspect SoD_SP quickspell slot=%d base=%p",
            slot, (void*)sod_sp);
    }

    const int side = mgr->current_active_side;
    int is_human = -1;
    int tactics = -1;
    __try {
        BYTE* raw = reinterpret_cast<BYTE*>(mgr);
        is_human = (side >= 0 && side <= 1) ? raw[0x54A6 + side] : -1;
        tactics = raw[0x13D68];
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    WriteLog("[Spell] request digit=%d slot=%d spell=%d targetHex=%d targetStack=%p flags=0x%X side=%d human=%d tactics=%d hero=%p casted=%d mana=%d action=%d",
        digit, slot, spell_id, target_hex, target_stack, spell_flags,
        side, is_human, tactics,
        (side >= 0 && side <= 1) ? mgr->hero[side] : nullptr,
        GetHeroCasted_(mgr, side), GetHeroMana_(mgr, side), mgr->action);

    if (spell_id < 0 || spell_id >= 70 || (spell_flags & 1) == 0)
        WriteLog("[Spell] SoD_SP slot appears empty/invalid; still posting digit for other hooks slot=%d spell=%d flags=0x%X",
            slot, spell_id, spell_flags);

    HWND hwnd = *reinterpret_cast<HWND*>(0x699650);
    if (!hwnd) {
        WriteLog("[Spell] game window unavailable digit=%d spell=%d", digit, spell_id);
        return false;
    }

    const WPARAM win_vk = static_cast<WPARAM>('0' + digit);
    const LPARAM key_down = 1 | (static_cast<LPARAM>(h3vk) << 16);
    const LPARAM key_up = key_down | (static_cast<LPARAM>(1) << 30)
        | (static_cast<LPARAM>(1) << 31);
    __try {
        const BOOL down_ok = PostMessageA(hwnd, WM_KEYDOWN, win_vk, key_down);
        const BOOL up_ok = PostMessageA(hwnd, WM_KEYUP, win_vk, key_up);
        WriteLog("[Spell] posted quick key digit=%d h3vk=%d spell=%d hwnd=%p down=%d up=%d",
            digit, h3vk, spell_id, hwnd, down_ok ? 1 : 0, up_ok ? 1 : 0);
        if (!down_ok || !up_ok)
            return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[Spell] posting quick key crashed digit=%d spell=%d", digit, spell_id);
        return false;
    }
}

static int GetHeroMana_(_BattleMgr_* mgr, int side)
{
    if (!mgr || side < 0 || side > 1) return -1;
    _Hero_* hero = mgr->hero[side];
    if (!hero) return -1;
    return hero->spell_points;
}

static int GetHeroCasted_(_BattleMgr_* mgr, int side)
{
    if (!mgr || side < 0 || side > 1) return 0;
    return mgr->hero_casted[side] ? 1 : 0;
}

// 从配置序列 + 运行游标取本回合快捷键；无序列返回 -1。
static int PeekSpellKey_(const AutoStackRule& rule, const StackTrackEntry& runtime)
{
    int n = rule.spellSlotCount;
    if (n <= 0) return -1;
    if (n > SPELL_SLOT_CAPACITY) n = SPELL_SLOT_CAPACITY;
    int cur = runtime.spell_cursor;
    if (cur < 0 || cur >= n) cur = 0;
    const int key = rule.spellSlots[cur];
    if (key == 0 || (key >= 1 && key <= 9)) return key;
    return -1;
}

static void AdvanceSpellCursor_(StackTrackEntry& runtime, int count)
{
    if (count <= 0) {
        runtime.spell_cursor = 0;
        return;
    }
    if (count > SPELL_SLOT_CAPACITY) count = SPELL_SLOT_CAPACITY;
    int cur = runtime.spell_cursor;
    if (cur < 0 || cur >= count) cur = 0;
    runtime.spell_cursor = (cur + 1) % count;
}

int GetAliveStacks(_BattleStack_* out_stacks[], int max_count, int side)
{
    int n = 0;
    for (int i = 0; i < 21 && n < max_count; ++i) {
        if (o_BattleMgr->stack[side][i].count_current > 0 && o_BattleMgr->stack[side][i].count_at_start > 0)
            out_stacks[n++] = &o_BattleMgr->stack[side][i];
    }
    return n;
}

int GetEnemyStacks(_BattleStack_* out_stacks[], int max_count, int my_side)
{
    int enemy_side = 1 - my_side;
    int n = 0;
    for (int i = 0; i < 21 && n < max_count; ++i) {
        if (o_BattleMgr->stack[enemy_side][i].count_current > 0 && o_BattleMgr->stack[enemy_side][i].count_at_start > 0)
            out_stacks[n++] = &o_BattleMgr->stack[enemy_side][i];
    }
    return n;
}

// 英雄二级技能索引（second_skill[28]）
enum SecSkillIndex {
    SK_BALLISTICS = 10,  // 弹道术（投石车）
    SK_ARTILLERY  = 20,  // 炮术（弩车/箭塔）
    SK_FIRST_AID  = 27,  // 急救术（急救帐篷）
};

// 当前行动方英雄是否掌握某二级技能（等级 > 0）。
static bool HeroHasSkill(_BattleMgr_* mgr, int skill_index)
{
    if (!mgr) return false;
    int side = mgr->current_active_side;
    if (side < 0 || side > 1) return false;
    _Hero_* hero = mgr->hero[side];
    if (!hero) return false;
    return hero->second_skill[skill_index] > 0;
}

// CommitProfiles：勾号/Enter 一次性提交全部 5 套内存方案，当前选中方案立即生效。
void CommitProfiles(int active_profile, AutoStackRule rules[5][21])
{
    if (active_profile < 0 || active_profile >= 5)
        active_profile = 0;
    memcpy(g_profiles, rules, sizeof(g_profiles));
    g_active_profile = active_profile;
    memcpy(g_active_rules, g_profiles[g_active_profile], sizeof(g_active_rules));
    int spell_rules = 0;
    int action_rules = 0;
    for (int i = 0; i < 21; ++i) {
        if (g_active_rules[i].action != AA_MANUAL) ++action_rules;
        if (g_active_rules[i].spellSlotCount > 0) {
            ++spell_rules;
            WriteLog("[Auto] commit slot=%d action=%d spells=%d first=%d",
                i, (int)g_active_rules[i].action,
                (int)g_active_rules[i].spellSlotCount,
                (int)g_active_rules[i].spellSlots[0]);
        }
    }
    WriteLog("[Auto] 5 profiles committed; active profile=%d actions=%d spells=%d",
        g_active_profile + 1, action_rules, spell_rules);
    // 提交后立即绑定本场部队身份；后续执行依赖跟踪校验。
    BindStackTrackingFromBattle_();
}

// 控制权三态：
// KEEP_ORIGINAL = 保持原版（人类输入/原版逻辑）
// HAND_TO_AI    = 交给原版 AI
// EXECUTE_H3AUTO= 本回合由 H3Auto 提交主动动作
enum ControlDecision {
    CD_KEEP_ORIGINAL = 0,
    CD_HAND_TO_AI    = 1,
    CD_EXECUTE_H3AUTO = 2,
};

// 原版 eBattleAction 值（与 H3API / FUN_00476500 映射一致）
enum BattleActionCode {
    BA_WALK        = 2,
    BA_DEFEND      = 3,
    BA_WALK_ATTACK = 6,
    BA_SHOOT       = 7,
    BA_WAIT        = 8,
    BA_FIRST_AID   = 11,
};

static bool IsWarMachineCid_(int cid)
{
    return cid == WM_CATAPULT || cid == WM_BALLISTA
        || cid == WM_FIRST_AID || cid == WM_AMMO_CART
        || cid == WM_ARROW_TOWER;
}

// 失血数值 = 阵亡单位满血 + 当前顶层单位已损 HP。
static int WoundValueOf_(_BattleStack_* t)
{
    if (!t) return 0;
    const int hp = (t->creature.hit_points > 0) ? t->creature.hit_points : 1;
    int dead = t->count_at_start - t->count_current;
    if (dead < 0) dead = 0;
    int lost = t->lost_hp;
    if (lost < 0) lost = 0;
    return dead * hp + lost;
}

// 失血比例比较键：wound * 10000 / total_max_hp，便于整数比较。
static int WoundRatioKey_(_BattleStack_* t)
{
    if (!t) return 0;
    const int hp = (t->creature.hit_points > 0) ? t->creature.hit_points : 1;
    int start = t->count_at_start;
    if (start <= 0) start = t->count_current;
    if (start <= 0) return 0;
    const int total = start * hp;
    if (total <= 0) return 0;
    return (int)(((__int64)WoundValueOf_(t) * 10000) / total);
}

// 通用部队选择：side_filter = 0己方 / 1敌方 / 2双方；require_wounded 用于急救。
// 远程：随机 / 远程高速优先 / 数量最多；急救：随机 / 失血比例 / 失血数值。
static _BattleStack_* SelectStackTarget_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule, int side_filter, bool require_wounded,
    bool require_can_shoot)
{
    if (!mgr || !self) return nullptr;

    _BattleStack_* candidates[42] = {};
    int count = 0;
    for (int side = 0; side < 2; ++side) {
        if (side_filter == 0 && side != self->def_group_ix) continue;
        if (side_filter == 1 && side == self->def_group_ix) continue;
        for (int i = 0; i < 21 && count < 42; ++i) {
            _BattleStack_* t = &mgr->stack[side][i];
            if (t == self) continue;
            if (t->count_current <= 0 || t->count_at_start <= 0) continue;
            if (t->creature_id < 0) continue;
            if (require_can_shoot && !self->CanShoot(t)) continue;
            if (require_wounded
                && t->lost_hp <= 0 && t->count_current >= t->count_at_start)
                continue;
            candidates[count++] = t;
        }
    }
    if (count <= 0) return nullptr;

    switch (rule.target.selector) {
    case SEL_COUNT_HIGH: {
        _BattleStack_* best = candidates[0];
        for (int i = 1; i < count; ++i)
            if (candidates[i]->count_current > best->count_current)
                best = candidates[i];
        return best;
    }
    case SEL_RANGED_SPEED: {
        // 远程优先 → 速度降序。射手（shots>0）排在近战前；同类按 speed 高者优先。
        _BattleStack_* best = candidates[0];
        int best_ranged = (best->creature.shots > 0) ? 1 : 0;
        int best_speed = best->creature.speed;
        for (int i = 1; i < count; ++i) {
            int r = (candidates[i]->creature.shots > 0) ? 1 : 0;
            int s = candidates[i]->creature.speed;
            if (r > best_ranged || (r == best_ranged && s > best_speed)) {
                best = candidates[i];
                best_ranged = r;
                best_speed = s;
            }
        }
        return best;
    }
    case SEL_WOUND_VALUE: {
        _BattleStack_* best = candidates[0];
        int best_v = WoundValueOf_(best);
        for (int i = 1; i < count; ++i) {
            const int v = WoundValueOf_(candidates[i]);
            if (v > best_v) {
                best = candidates[i];
                best_v = v;
            }
        }
        return best;
    }
    case SEL_WOUND_RATIO: {
        _BattleStack_* best = candidates[0];
        int best_r = WoundRatioKey_(best);
        for (int i = 1; i < count; ++i) {
            const int r = WoundRatioKey_(candidates[i]);
            if (r > best_r) {
                best = candidates[i];
                best_r = r;
            }
        }
        return best;
    }
    case SEL_RANDOM:
    default:
        return candidates[rand() % count];
    }
}

// 解析位置目标：用部队目标的位置作锚点（循环移动旧单目标兜底）。
static int ResolvePositionTarget_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule)
{
    if (!mgr || !self) return -1;
    int side_filter = 2;
    if (rule.target.side == ATS_OWN) side_filter = 0;
    else if (rule.target.side == ATS_ENEMY) side_filter = 1;
    _BattleStack_* t = SelectStackTarget_(mgr, self, rule, side_filter, false, false);
    if (t) return t->hex_ix;
    return -1;
}

// 提交防御：仅写 battle->action=3，由主循环自然进入 FUN_004786b0。
static bool SubmitDefend_(_BattleMgr_* mgr, _BattleStack_* self)
{
    if (!mgr || !self) return false;
    if (mgr->action != 0) return false; // 已有动作
    mgr->action = BA_DEFEND;
    mgr->action_parameter = -1;
    mgr->action_target = -1;
    mgr->action_parameter2 = 0;
    g_auto_state.last_handled_stack = self;
    WriteLog("[Auto] submit DEFEND slot=%d creature=0x%X",
        self->army_slot_ix, self->creature_id);
    return true;
}

static bool WriteAction_(_BattleMgr_* mgr, _BattleStack_* self,
    int action, int param, int target_hex)
{
    if (!mgr || !self) return false;
    if (mgr->action != 0) return false;
    mgr->action = action;
    mgr->action_parameter = param;
    mgr->action_target = target_hex;
    mgr->action_parameter2 = 0;
    g_auto_state.last_handled_stack = self;
    return true;
}

// 提交远程攻击：action=7, actionTarget=目标 hex。
static bool SubmitRanged_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    _BattleStack_* target = SelectStackTarget_(mgr, self, rule, 1, false, true);
    if (!target) return false;
    if (!WriteAction_(mgr, self, BA_SHOOT, -1, target->hex_ix))
        return false;
    WriteLog("[Auto] submit SHOOT slot=%d -> hex=%d target_slot=%d",
        self->army_slot_ix, target->hex_ix, target->army_slot_ix);
    return true;
}

// 提交移动：action=2, actionTarget=目标 hex。
// 循环移动：按 moveWaypoints 有序巡逻，逐点走向下一个路径点，到达后推进游标（末点回首点）。
// 无路径点时回退到旧的单目标移动（固定位置 / 靠近目标部队）。
// 注：可达性未做完整原版寻路校验。
static bool SubmitMove_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule, StackTrackEntry& runtime)
{
    if (!mgr || !self) return false;

    // 收集有效路径点（1..185）。配置只读，运行游标来自 StackTrackEntry。
    int wps[MOVE_WAYPOINT_CAPACITY];
    int n = 0;
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i) {
        int h = rule.target.moveWaypoints[i];
        if (h >= 1 && h <= 185) wps[n++] = h;
    }

    if (n > 0) {
        // 规范化运行时游标。
        int cur = runtime.move_cursor;
        if (cur < 0 || cur >= n) cur = 0;

        // 到点才推进（方案 A）：已站在当前点，则切到下一个点；
        // 若配置含重复点，跳过连续的脚下点。
        if (self->hex_ix == wps[cur])
            cur = (cur + 1) % n;
        int guard = 0;
        while (wps[cur] == self->hex_ix && guard < n) {
            cur = (cur + 1) % n;
            ++guard;
        }
        runtime.move_cursor = cur;

        int hex = wps[cur];
        if (hex == self->hex_ix) return false; // 所有点都在脚下
        if (!WriteAction_(mgr, self, BA_WALK, -1, hex))
            return false;
        WriteLog("[Auto] submit WALK(patrol) slot=%d -> hex=%d cursor=%d/%d",
            self->army_slot_ix, hex, cur, n);
        return true;
    }

    // 回退：旧单目标移动。
    int hex = ResolvePositionTarget_(mgr, self, rule);
    if (hex < 1 || hex > 185) return false;
    if (hex == self->hex_ix) return false;
    if (!WriteAction_(mgr, self, BA_WALK, -1, hex))
        return false;
    WriteLog("[Auto] submit WALK slot=%d -> hex=%d",
        self->army_slot_ix, hex);
    return true;
}

// 判断某 hex 是否被敌方部队占据（双格头/尾都算）。
// 尾格用 GetSecondSquare(0x4463C0)；失败则仅比头格。
static _BattleStack_* FindEnemyOccupyingHex_(_BattleMgr_* mgr, _BattleStack_* self, int hex)
{
    if (!mgr || !self || hex < 1 || hex > 185) return nullptr;
    const int enemy_side = 1 - self->def_group_ix;
    for (int i = 0; i < 21; ++i) {
        _BattleStack_* t = &mgr->stack[enemy_side][i];
        if (t->count_current <= 0 || t->count_at_start <= 0) continue;
        if (t->creature_id < 0) continue;
        if (t->hex_ix == hex) return t;
        int second = -1;
        __try {
            second = THISCALL_1(int, 0x4463C0, t);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            second = -1;
        }
        if (second == hex) return t;
    }
    return nullptr;
}

// 提交近战：配置 = 站立格(meleeStandHex) + 攻击格(meleeAttackHex)。
// 规则：攻击格上有敌人（头或尾）才出手；站立格为 -1 表示保持当前位置。
// 提交：action=6, actionTarget=攻击格, actionParameter=站立格
// （对应 FUN_00476500 case7 的 0x132d4/0x132d8）。
// 同时写入 mouse_coord/attacker_coord，贴近玩家点击后的状态。
static bool SubmitMelee_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule, StackTrackEntry& runtime)
{
    if (!mgr || !self) return false;
    const AutoTargetRule& target = rule.target;
    int count = target.meleePairCount;
    if (count < 0) count = 0;
    if (count > MELEE_PAIR_CAPACITY) count = MELEE_PAIR_CAPACITY;

    // 兼容旧版单组规则：序列为空时仍执行旧字段，但不虚构新记录。
    const bool legacy = count == 0;
    int cursor = legacy ? 0 : runtime.melee_cursor;
    if (!legacy && (cursor < 0 || cursor >= count)) cursor = 0;

    const int attack_hex = legacy
        ? target.meleeAttackHex : target.meleeAttackHexes[cursor];
    const int stand_hex = legacy
        ? target.meleeStandHex : target.meleeStandHexes[cursor];
    if (attack_hex < 1 || attack_hex > 185
        || stand_hex < 1 || stand_hex > 185) {
        WriteLog("[Auto] melee pair invalid slot=%d cursor=%d/%d stand=%d attack=%d",
            self->army_slot_ix, cursor, count, stand_hex, attack_hex);
        return false;
    }

    _BattleStack_* enemy = FindEnemyOccupyingHex_(mgr, self, attack_hex);
    if (!enemy) {
        WriteLog("[Auto] melee attack hex=%d empty (no enemy head/tail) slot=%d",
            attack_hex, self->army_slot_ix);
        return false;
    }

    // 写悬停相关字段，模拟玩家点过该攻击格
    mgr->mouse_coord = attack_hex;
    mgr->attacker_coord = stand_hex;
    mgr->move_type = 7; // 近战悬停类型（FUN_00475dc0 返回 7）

    if (!WriteAction_(mgr, self, BA_WALK_ATTACK, stand_hex, attack_hex))
        return false;
    // 只在成功提交后推进；失败时保持当前组合不变。
    if (!legacy && count > 0)
        runtime.melee_cursor = (cursor + 1) % count;
    WriteLog("[Auto] submit MELEE(loop) pair=%d/%d stand=%d attack=%d enemy_slot=%d enemy_hex=%d next=%d",
        cursor, count, stand_hex, attack_hex, enemy->army_slot_ix, enemy->hex_ix,
        legacy ? 0 : runtime.melee_cursor);
    return true;
}

// 提交等待：action=8。
static bool SubmitWait_(_BattleMgr_* mgr, _BattleStack_* self)
{
    if (!WriteAction_(mgr, self, BA_WAIT, -1, -1))
        return false;
    WriteLog("[Auto] submit WAIT slot=%d", self->army_slot_ix);
    return true;
}

// 提交急救：action=11, actionTarget=己方伤员 hex。
static bool SubmitFirstAid_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    _BattleStack_* target = SelectStackTarget_(mgr, self, rule, 0, true, false);
    if (!target) return false;
    if (!WriteAction_(mgr, self, BA_FIRST_AID, -1, target->hex_ix))
        return false;
    WriteLog("[Auto] submit FIRST_AID slot=%d -> hex=%d target_slot=%d",
        self->army_slot_ix, target->hex_ix, target->army_slot_ix);
    return true;
}

// 提交部队主动作（不含循环施法阶段）。
static bool SubmitConfiguredUnitAction_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule, StackTrackEntry& runtime)
{
    if (!mgr || !self) return false;
    const int cid = self->creature_id;
    if (cid == WM_AMMO_CART) return false;

    auto fallback_or_fail = [&](bool ok) -> bool {
        if (ok) return true;
        // 战争机器不降级；普通部队可降级防御。
        if (!IsWarMachineCid_(cid) && rule.allowDefendFallback)
            return SubmitDefend_(mgr, self);
        return false;
    };

    switch (rule.action) {
    case AA_MANUAL:
        // 仅配置了循环施法时：施法阶段结束后把主动作交回玩家。
        return false;
    case AA_DEFEND:
        return SubmitDefend_(mgr, self);
    case AA_WAIT:
        return fallback_or_fail(SubmitWait_(mgr, self));
    case AA_MOVE:
        return fallback_or_fail(SubmitMove_(mgr, self, rule, runtime));
    case AA_MELEE_ATTACK:
        return fallback_or_fail(SubmitMelee_(mgr, self, rule, runtime));
    case AA_RANGED_ATTACK:
        return fallback_or_fail(SubmitRanged_(mgr, self, rule));
    case AA_FIRST_AID:
        if (cid != WM_FIRST_AID) return false;
        return SubmitFirstAid_(mgr, self, rule); // 帐篷不降级
    default:
        return false;
    }
}

// 尝试按规则提交主动动作；成功返回 true。
// 含循环施法两阶段：先投递快捷键，等待施法结束/超时后再提交部队动作。
static bool TrySubmitConfiguredAction_(_BattleMgr_* mgr, bool allow_unit_action)
{
    if (!mgr) return false;
    if (mgr->auto_combat) return false;
    if (IsHiddenBattle(mgr)) return false;
    if (IsTacticsPhase_(mgr)) return false;
    if (mgr->action != 0) return false;
    UpdateStackTracking_();
    _BattleStack_* self = mgr->active_stack;
    if (!self || self->count_current <= 0) return false;

    // 活动单位变化：清空施法等待/完成标记与 last_handled。
    if (g_auto_state.spell_waiting
        && g_auto_state.spell_wait_stack
        && g_auto_state.spell_wait_stack != self)
        ClearSpellWait_();
    if (g_auto_state.spell_done_stack
        && g_auto_state.spell_done_stack != self)
        g_auto_state.spell_done_stack = nullptr;
    if (g_auto_state.action_wake_stack
        && g_auto_state.action_wake_stack != self)
        g_auto_state.action_wake_stack = nullptr;
    if (g_auto_state.last_handled_stack == self
        && !g_auto_state.spell_waiting)
        return false; // 本单位已处理完

    // 必须是跟踪表中仍存活、身份匹配的人类侧部队。
    if (!ActiveStackMatchesTrack_(self)) {
        if (g_auto_state.spell_waiting)
            ClearSpellWait_();
        static void* s_last_mismatch = nullptr;
        if (s_last_mismatch != self) {
            s_last_mismatch = self;
            WriteLog("[Track] skip action: mismatch side=%d slot=%d cid=0x%X count=%d",
                self->def_group_ix, self->army_slot_ix,
                self->creature_id, self->count_current);
        }
        return false;
    }

    const int idx = self->army_slot_ix;
    if (idx < 0 || idx >= 21) return false;
    const AutoStackRule& rule = g_active_rules[idx];
    StackTrackEntry& runtime = g_stack_track[idx];
    const int side = self->def_group_ix;
    const int spell_key = PeekSpellKey_(rule, runtime);
    const bool want_spell = spell_key >= 0;
    const bool want_action = rule.action != AA_MANUAL;

    // 既无施法序列、又无主动作：不介入。
    if (!want_spell && !want_action)
        return false;

    // —— 阶段 1：投递快捷施法键 ——
    if (want_spell && !g_auto_state.spell_waiting
        && g_auto_state.spell_done_stack != self) {
        // 本英雄本回合已施过法：跳过施法，直接进入部队动作。
        if (GetHeroCasted_(mgr, side) != 0) {
            WriteLog("[Spell] already cast this turn side=%d; skip quick key=%d",
                side, spell_key);
            // 英雄每回合只能施法一次；本部队没有实际尝试，不消费循环槽位。
            g_auto_state.spell_done_stack = self;
        } else {
            g_auto_state.spell_mana_before = GetHeroMana_(mgr, side);
            g_auto_state.spell_casted_before = GetHeroCasted_(mgr, side);
            g_auto_state.spell_wait_started = GetTickCount();
            if (!TriggerQuickSpellDigit_(spell_key)) {
                WriteLog("[Spell] trigger failed digit=%d; fallthrough to unit action",
                    spell_key);
                AdvanceSpellCursor_(runtime, rule.spellSlotCount);
                g_auto_state.spell_done_stack = self;
            } else {
                g_auto_state.spell_waiting = true;
                g_auto_state.spell_wait_stack = self;
                g_auto_state.spell_wait_slot = idx;
                g_auto_state.spell_wait_key = spell_key;
                g_auto_state.spell_wait_frames = 0;
                WriteLog("[Spell] wait start slot=%d key=%d mana=%d casted=%d",
                    idx, spell_key, g_auto_state.spell_mana_before,
                    g_auto_state.spell_casted_before);
                return false; // 本帧只投键，不提交部队动作
            }
        }
    }

    // —— 阶段 2：等待施法结果 ——
    if (g_auto_state.spell_waiting && g_auto_state.spell_wait_stack == self) {
        ++g_auto_state.spell_wait_frames;
        const DWORD elapsed = GetTickCount() - g_auto_state.spell_wait_started;
        const int mana_now = GetHeroMana_(mgr, side);
        const int casted_now = GetHeroCasted_(mgr, side);
        const bool cast_done =
            (casted_now != 0 && g_auto_state.spell_casted_before == 0)
            || (mana_now >= 0 && g_auto_state.spell_mana_before >= 0
                && mana_now < g_auto_state.spell_mana_before);
        // BltComplete 在本环境中一毫秒内可触发多次，必须按真实时间超时。
        const bool timed_out = elapsed >= 2000;
        if (!cast_done && !timed_out)
            return false; // 继续等

        WriteLog("[Spell] wait end slot=%d key=%d calls=%d elapsed=%lu cast_done=%d timed_out=%d mana %d->%d casted %d->%d",
            idx, g_auto_state.spell_wait_key, g_auto_state.spell_wait_frames,
            (unsigned long)elapsed,
            cast_done ? 1 : 0, timed_out ? 1 : 0,
            g_auto_state.spell_mana_before, mana_now,
            g_auto_state.spell_casted_before, casted_now);

        // 无论成功/失败/超时都推进游标，避免同一键卡死整场。
        AdvanceSpellCursor_(runtime, rule.spellSlotCount);
        ClearSpellWait_();
        g_auto_state.spell_done_stack = self;
        // 施法动画/选择目标期间 action 可能被占用；若仍非 0 则下帧再提交主动作。
        if (mgr->action != 0)
            return false;
    }

    // —— 阶段 3：提交部队主动作 ——
    if (!want_action) {
        // 仅循环施法、主动作为手动：施法流程结束后交回玩家。
        g_auto_state.last_handled_stack = self;
        return false;
    }

    if (!allow_unit_action) {
        RequestUnitActionWake_(self);
        return false;
    }

    const bool ok = SubmitConfiguredUnitAction_(mgr, self, rule, runtime);
    if (ok) {
        WriteLog("[Auto] action consumed wake slot=%d action=%d",
            self->army_slot_ix, mgr->action);
        g_auto_state.action_wake_stack = nullptr;
    }
    return ok;
}

// DecideTakeover：仅在“原版本会把控制权交给玩家”时被询问（HH 里 orig==0）。
// 普通部队：有配置则本插件提交动作；手动则留给玩家。
// 战争机器：才有“交回 AI / 保持原版 / 按配置执行”的特殊分支。
int DecideTakeover(_BattleMgr_* mgr)
{
    if (!mgr) return CD_KEEP_ORIGINAL;
    if (mgr->auto_combat) return CD_KEEP_ORIGINAL;
    if (IsHiddenBattle(mgr)) return CD_KEEP_ORIGINAL;
    if (IsTacticsPhase_(mgr)) return CD_KEEP_ORIGINAL;
    UpdateStackTracking_();

    // 最高优先级：本场全手动 / 单次接管 → 一律把控制权留给玩家。
    // 不改配置、不推进游标；正在飞行中的自动动作由热键层延后接管。
    if (ShouldYieldToPlayer_(mgr))
        return CD_KEEP_ORIGINAL;

    _BattleStack_* stack = mgr->active_stack;
    if (!stack || stack->count_current <= 0) return CD_KEEP_ORIGINAL;

    // 非本场已绑定的人类侧存活单位：不介入（控制权本就不该由我们改写）。
    if (!ActiveStackMatchesTrack_(stack)) {
        static void* s_last_takeover_mismatch = nullptr;
        if (s_last_takeover_mismatch != stack) {
            s_last_takeover_mismatch = stack;
            const int slot = stack->army_slot_ix;
            const StackTrackEntry* expected =
                (slot >= 0 && slot < 21) ? &g_stack_track[slot] : nullptr;
            WriteLog("[Track] takeover mismatch ptr=%p side=%d slot=%d cid=0x%X count=%d track_active=%d expected_bound=%d expected_alive=%d expected_side=%d expected_cid=0x%X",
                stack, stack->def_group_ix, slot, stack->creature_id,
                stack->count_current, g_track_active ? 1 : 0,
                expected && expected->bound ? 1 : 0,
                expected && expected->alive ? 1 : 0,
                expected ? expected->side : -1,
                expected ? expected->creature_id : -1);
        }
        return CD_KEEP_ORIGINAL;
    }

    int idx = stack->army_slot_ix;
    if (idx < 0 || idx >= 21) return CD_KEEP_ORIGINAL;
    const AutoStackRule& rule = g_active_rules[idx];
    const int cid = stack->creature_id;

    // —— 仅战争机器特殊处理 ——
    switch (cid) {
    case WM_AMMO_CART:
        return CD_KEEP_ORIGINAL;

    case WM_FIRST_AID:
        // 对齐弩车：有急救术可选手动（原版）；配置急救→插件执行；
        // 无急救术 UI 仅急救，残留手动→交 AI。
        if (rule.action == AA_FIRST_AID)
            return CD_EXECUTE_H3AUTO;
        if (rule.action == AA_MANUAL && HeroHasSkill(mgr, SK_FIRST_AID))
            return CD_KEEP_ORIGINAL;
        return CD_HAND_TO_AI;

    case WM_CATAPULT:
        // 仅手动 / 防御：防御由普通主动作路径提交；手动交回原版。
        if (rule.action == AA_DEFEND)
            return CD_EXECUTE_H3AUTO;
        return CD_KEEP_ORIGINAL;

    case WM_BALLISTA:
    case WM_ARROW_TOWER:
        // 有炮术：可选手动（交回原版）或远程攻击（插件执行）。
        // 无炮术：UI 只给远程攻击，这里也只执行远程；其余交 AI。
        if (rule.action == AA_RANGED_ATTACK)
            return CD_EXECUTE_H3AUTO;
        if (rule.action == AA_MANUAL && HeroHasSkill(mgr, SK_ARTILLERY))
            return CD_KEEP_ORIGINAL;
        return CD_HAND_TO_AI;

    default:
        // 普通部队：无特殊 AI 分支。
        // - 有非手动主动作 → 代发动作
        // - 主动作手动但配置了循环施法 → 仍进入执行路径（仅做施法阶段）
        // - 都没有 → 留给玩家
        if (rule.action != AA_MANUAL)
            return CD_EXECUTE_H3AUTO;
        if (rule.spellSlotCount > 0) {
            static void* s_last_spell_takeover = nullptr;
            if (s_last_spell_takeover != stack) {
                s_last_spell_takeover = stack;
                WriteLog("[Spell] takeover slot=%d spells=%d first=%d",
                    idx, (int)rule.spellSlotCount,
                    (int)rule.spellSlots[0]);
            }
            return CD_EXECUTE_H3AUTO;
        }
        return CD_KEEP_ORIGINAL;
    }
}

// 供战斗消息入口调用：若当前单位应由 H3Auto 主动执行，则提交动作。
// 返回 true 表示已写入 action，原版输入可继续走默认路径处理后续。
bool TryAutoExecuteActiveStack(bool allow_unit_action)
{
    _BattleMgr_* mgr = o_BattleMgr;
    if (!mgr) return false;
    __try {
        PollControlHotkeys_(mgr);
        const int decision = DecideTakeover(mgr);
        if (decision != CD_EXECUTE_H3AUTO)
            return false;
        return TrySubmitConfiguredAction_(mgr, allow_unit_action);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ==== 接管钩子：控制权交给玩家的时机 ====
// FUN_004744d0 @ 0x4744D0：
//   返回非 0 = 自动/AI；返回 0 = 等待本地人类输入（控制权交给玩家）。
// 只在 orig==0（本会交给玩家）时介入：
//   - 战争机器未配置且应走 AI → 改返回 1 交回 AI
//   - 有配置要代发动作 → 仍返回 0，由消息入口提交 action
//   - 手动 / 其它 → 返回 0，真正留给玩家
// 蛊惑等“本就不会交给玩家”的情况：orig 已非 0，我们直接放行。
int __stdcall HH_ShouldAutoExecute(HiHook* h, _BattleMgr_* This)
{
    int orig = THISCALL_1(int, h->GetDefaultFunc(), This);
    if (orig != 0)
        return orig;            // 非“交给玩家”路径：不介入
    __try {
        PollControlHotkeys_(This);
        if (This && This->active_stack) {
            if (g_auto_state.last_handled_stack
                && g_auto_state.last_handled_stack != This->active_stack)
                g_auto_state.last_handled_stack = nullptr;
            if (g_auto_state.spell_done_stack
                && g_auto_state.spell_done_stack != This->active_stack)
                g_auto_state.spell_done_stack = nullptr;
            if (g_auto_state.spell_waiting
                && g_auto_state.spell_wait_stack
                && g_auto_state.spell_wait_stack != This->active_stack)
                ClearSpellWait_();
        }

        const int decision = DecideTakeover(This);
        if (decision == CD_HAND_TO_AI)
            return 1;           // 仅战争机器特殊：交回 AI
        // CD_EXECUTE_H3AUTO / CD_KEEP_ORIGINAL：返回 0（控制权在玩家路径）。
        // 若需代发动作，在 Hook_BattleMsgProc 入口提交。
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
