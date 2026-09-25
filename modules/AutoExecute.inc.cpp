// AutoExecute.inc.cpp
// Automated battle execution module

static void LogInfo(const char* fmt, ...);  // 分级前向声明（LogWarn/LogError 等见 ConfigLog）

extern void CommitProfiles(int active_profile, AutoStackRule rules[5][21],
    const uint8_t protect_strategy[5], const uint16_t stop_turns[5]);
extern void ClearConfirmedProfiles();
extern AutoStackRule g_profiles[5][21];
extern AutoStackRule g_active_rules[21];
extern int  g_active_profile;
extern int  g_last_profile;
extern uint8_t  g_protect_strategy[5];
extern uint16_t g_stop_turns[5];
extern bool IsPanelActive();
extern void CloseSettingsPanel();

// 战场阶段状态机（enum/g_phase/SetPhase_）：声明在 BattleState.hpp，
// 实现在 BattleState.inc.cpp（本文件之后 include）。
#include "BattleState.hpp"

// 定义在本文件后部：SetControlMode_ 边动作需要，先声明（C2065 预防）。
static void RefreshControlStatusHint_();

// ===== 控制权子状态（重构步骤 §1.2；仅战斗中·面板关内有效）=====
enum ControlMode {
    CM_AUTO,           // 自动执行
    CM_MANUAL,         // F9 全手动：本场所有已配置部队交回玩家
    CM_ONESHOT_WAIT,   // J 单次接管待命：等下一支可接管部队
    CM_ONESHOT_LOCKED, // J 单次接管锁定中：锁定部队完成玩家动作后回 AUTO
};
static ControlMode g_control = CM_AUTO;

// ===== 单位管线子状态（重构步骤 §1.2；锚定 g_pipeline_stack 活动单位）=====
// 取代旧 spell_waiting/spell_wait_stack/spell_done_stack/last_handled_stack 四标志。
enum PipelineStage {
    PS_IDLE,         // 当前单位无未完成管线
    PS_SPELL_POSTED, // 已投递快捷施法键，等待结果（超时数据在 g_auto_state.spell_wait_*）
    PS_SPELL_DONE,   // 本单位本回合施法阶段已结束，不再投键
    PS_HANDLED,      // 本单位本回合已处理（已下命令或交回玩家），不再重复处理
};
static PipelineStage g_pipeline_stage = PS_IDLE;
static void* g_pipeline_stack = nullptr; // 管线锚定的活动单位

static const char* ControlModeName_(ControlMode m)
{
    switch (m) {
    case CM_AUTO:           return "AUTO";
    case CM_MANUAL:         return "MANUAL";
    case CM_ONESHOT_WAIT:   return "ONESHOT_WAIT";
    case CM_ONESHOT_LOCKED: return "ONESHOT_LOCKED";
    default:                return "?";
    }
}

// 用户可见的模式切换（F9/J/面板打开/边动作）：日志 + 状态提示。
static void SetControlMode_(ControlMode m)
{
    if (g_control == m) return;
    g_control = m;
    LogInfo("[Control] control=%s", ControlModeName_(m));
    RefreshControlStatusHint_();
}

bool InCombat_()
{
    return g_phase == BP_COMBAT_CLOSED || g_phase == BP_COMBAT_OPEN;
}

bool PanelOpen_()
{
    return g_phase == BP_COMBAT_OPEN;
}

// 接管模型（收敛后）：
// 1) 只在“控制权交给玩家”时介入（HH_ShouldAutoExecute 返回 0 的路径）。
//    被蛊惑/敌方回合等本就不会把控制权交给玩家，无需单独状态机。
// 2) 普通部队：有非手动设置 → 代为提交动作；手动 → 原样留给玩家。
// 3) 仅战争机器有特殊分支（技能条件 / 交回 AI / 可选主动执行）。
// 管线四标志（waiting/wait_stack/done_stack/last_handled）已并入
// g_pipeline_stage + g_pipeline_stack；此处只留超时观测与单次接管数据。
static struct {
    void* action_wake_stack;    // 已投递唤醒消息、等待在 0x4746B0 提交主动作的单位
    int   spell_wait_slot;      // 对应 army_slot
    int   spell_wait_key;       // 已投递的快捷键 0..9
    int   spell_wait_frames;    // 已等待帧数
    DWORD spell_wait_started;   // GetTickCount，避免高频 BltComplete 把调用次数误当帧数
    int   spell_mana_before;    // 投递前法力
    int   spell_casted_before;  // 投递前 hero_casted[side]

    // 战斗级人工接管：只改执行权，不改 5 套方案/游标。
    // 控制权模式在 g_control（CM_AUTO/CM_MANUAL/CM_ONESHOT_WAIT/CM_ONESHOT_LOCKED）。
    // 以下仅保留单次接管的锁定目标数据。
    void* oneshot_stack;        // 锁定到的活动部队指针
    int   oneshot_side;         // 锁定部队 side
    int   oneshot_slot;         // 锁定部队 army_slot
    int   oneshot_creature;     // 锁定部队 creature_id
    bool  kb_toggle_seen;       // 键盘钩子捕获的启停键按下（待消费）
    bool  kb_oneshot_seen;      // 键盘钩子捕获的单次接管键按下（待消费）
    bool  kb_open_panel_seen;   // 键盘钩子捕获的打开面板键按下（待消费）
    char  last_status_text[64]; // 状态提示去重
} g_auto_state;

// 当前战斗的人类侧部队跟踪表（辅助正确套用设置，不是第二套控制权逻辑）。
// 设置提交时绑定“槽位 + 生物类型”；之后刷新存活/位置/数量。
// 执行前校验身份，避免仅凭 army_slot_ix 套错（死亡、召唤复用槽等）。
struct StackTrackEntry {
    bool  bound;         // 本场是否已绑定
    bool  alive;         // 当前是否存活
    H3AutoPolicy::StableStackIdentity identity; // 跨重打配置身份
    int   attempt_id;    // 本次战场初始化代次
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
static int  g_battle_attempt_id = 0;
static H3AutoPolicy::StableStackIdentity g_rule_identities[21] = {};
// 首动保活：已判定过的战斗回合号（-1=尚未判定本回合）。
static int  g_protect_checked_turn = -1;
// 自动停止：最近两笔敌方血量。两笔都有效才外推，更早的不参与。
static int g_enemy_hp_turn[2] = { -1, -1 };
static int g_enemy_hp_value[2] = {};

static bool CreatureInfoIndexValid_(int creature_id)
{
    return creature_id >= 0 && creature_id <= 150;
}

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

static void BuildStableIdentitiesFromBattle_(_BattleMgr_* mgr, int side,
    H3AutoPolicy::StableStackIdentity out[21])
{
    if (!out) return;
    memset(out, 0, sizeof(H3AutoPolicy::StableStackIdentity) * 21);
    if (!mgr || side < 0 || side > 1) return;

    // 同类型出现序号：战争机器与召唤物共用（按生物 id 计数）。
    int occurrences[151 + 1] = {};
    for (int i = 0; i < 21; ++i) {
        _BattleStack_* s = &mgr->stack[side][i];
        if (s->creature_id < 0) continue;
        if (!CreatureInfoIndexValid_(s->creature_id)) continue;
        if (s->count_at_start <= 0 && s->count_current <= 0) continue;

        int occurrence = 0;
        const int cid = s->creature_id;
        if (cid <= 150)
            occurrence = occurrences[cid]++;
        // 克隆体可能继承本体的 source_army_slot：先判克隆，一律按召唤物
        // 身份（本场内执行，重打丢弃），避免与本体身份冲突。
        const int army_slot = (s->clone_id > 0) ? -1 : s->source_army_slot;
        out[i] = H3AutoPolicy::MakeStableStackIdentity(side,
            army_slot, s->creature_id, occurrence);
    }
}

// 从当前战场绑定人类侧所有“开战时存在”的部队身份。
static void BindStackTrackingFromBattle_()
{
    ClearStackTracking_();
    _BattleMgr_* mgr = o_BattleMgr;
    if (!mgr) {
        LogWarn("[Track] bind skipped: no battle manager");
        return;
    }

    const int side = ResolveHumanSide_(mgr);
    if (side < 0 || side > 1) {
        LogWarn("[Track] bind skipped: invalid human side");
        return;
    }

    H3AutoPolicy::StableStackIdentity current_identities[21] = {};
    BuildStableIdentitiesFromBattle_(mgr, side, current_identities);

    int bound = 0;
    int configured_action = 0;
    int configured_spell = 0;
    for (int i = 0; i < 21; ++i) {
        _BattleStack_* s = &mgr->stack[side][i];
        // 本场从未上场的空槽跳过。
        if (s->count_at_start <= 0 && s->count_current <= 0) continue;
        if (s->creature_id < 0) continue;
        if (current_identities[i].kind == H3AutoPolicy::STACK_ID_NONE) continue;

        StackTrackEntry& t = g_stack_track[i];
        t.bound = true;
        t.identity = current_identities[i];
        t.attempt_id = g_battle_attempt_id;
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

        LogDebug("[Track] bind attempt=%d slot=%d source=%d idkind=%d cid=0x%X hex=%d alive=%d count=%d/%d action=%d spells=%d first=%d",
            t.attempt_id, i, s->source_army_slot, (int)t.identity.kind,
            t.creature_id, t.hex, t.alive ? 1 : 0,
            t.count_alive, t.count_start, (int)g_active_rules[i].action,
            (int)g_active_rules[i].spellSlotCount,
            (g_active_rules[i].spellSlotCount > 0)
                ? (int)g_active_rules[i].spellSlots[0] : -1);
    }

    g_track_side = side;
    g_track_active = bound > 0;
    LogDebug("[Track] bound side=%d stacks=%d actions=%d spells=%d",
        side, bound, configured_action, configured_spell);
}

// 刷新跟踪表：存活、位置、数量；身份变化则标记死亡并解除可用。
static void UpdateStackTracking_()
{
    if (!g_track_active || g_track_side < 0 || g_track_side > 1)
        return;
    _BattleMgr_* mgr = o_BattleMgr;
    if (!mgr) return;

    H3AutoPolicy::StableStackIdentity current_identities[21] = {};
    BuildStableIdentitiesFromBattle_(mgr, g_track_side, current_identities);

    for (int i = 0; i < 21; ++i) {
        StackTrackEntry& t = g_stack_track[i];
        if (!t.bound) continue;

        _BattleStack_* s = &mgr->stack[t.side][i];
        if (t.attempt_id != g_battle_attempt_id
            || !H3AutoPolicy::StableStackIdentityEquals(
                t.identity, current_identities[i])) {
            // 槽位被复用/清空：本绑定失效。
            if (t.alive) {
                LogWarn("[Track] identity lost slot=%d expect=0x%X got=0x%X",
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
            LogDebug("[Track] dead slot=%d cid=0x%X last_hex=%d",
                i, t.creature_id, prev_hex);
        } else if (t.alive && (prev_hex != t.hex || prev_alive != t.count_alive)) {
            // 位置/数量变化仅在调试时有用；降噪：只在数量变化时打日志。
            if (prev_alive != t.count_alive) {
                LogDebug("[Track] update slot=%d cid=0x%X hex=%d count=%d",
                    i, t.creature_id, t.hex, t.count_alive);
            }
        }
    }
}

// 当前活动单位是否仍对应跟踪表中的那支人类侧部队。
static bool ActiveStackMatchesTrack_(_BattleStack_* self)
{
    if (!self) return false;
    if (!g_track_active)
        return false;

    const int idx = self->army_slot_ix;
    if (idx < 0 || idx >= 21) return false;
    const StackTrackEntry& t = g_stack_track[idx];
    if (!t.bound || !t.alive) return false;
    if (t.attempt_id != g_battle_attempt_id) return false;
    if (self->def_group_ix != t.side) return false;
    H3AutoPolicy::StableStackIdentity current_identities[21] = {};
    BuildStableIdentitiesFromBattle_(o_BattleMgr, t.side, current_identities);
    if (!H3AutoPolicy::StableStackIdentityEquals(
            t.identity, current_identities[idx]))
        return false;
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

// ==== 战斗热键：常驻键盘钩子捕获 keydown 边沿 ====
// PollControlHotkeys_ 挂在回合控制权判定时机，战斗动画期间不被调用，
// GetAsyncKeyState 轮询会错过短按。钩子在按键消息发生时立即记录标志，
// 轮询只消费标志，因此绝不漏按；也不受轮询频率影响。
static HHOOK s_combat_kb_hook = nullptr;

static LRESULT CALLBACK CombatHotkeyKbHook_(int code, WPARAM wParam, LPARAM lParam)
{
    // bit31=1 是 keyup；bit30=1 是自动重复（按住不放），都不要。
    if (code == HC_ACTION && !(lParam & 0x80000000) && !(lParam & 0x40000000)
        && !IsPanelActive())
    {
        if ((int)wParam == cfg.toggle_manual_vk)
            g_auto_state.kb_toggle_seen = true;
        else if ((int)wParam == cfg.one_shot_manual_vk)
            g_auto_state.kb_oneshot_seen = true;
        else if ((int)wParam == cfg.open_settings_vk)
            g_auto_state.kb_open_panel_seen = true;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

static void EnsureCombatKbHook_()
{
    if (s_combat_kb_hook) return;
    HWND game_window = *reinterpret_cast<HWND*>(0x699650);
    if (!game_window) return;
    const DWORD tid = GetWindowThreadProcessId(game_window, nullptr);
    if (!tid) return;
    s_combat_kb_hook = SetWindowsHookExA(WH_KEYBOARD, CombatHotkeyKbHook_,
        g_hModule, tid);
    if (s_combat_kb_hook)
        LogInfo("[Control] combat keyboard hook installed");
}

void ShutdownCombatHotkeys()
{
    if (s_combat_kb_hook) {
        UnhookWindowsHookEx(s_combat_kb_hook);
        s_combat_kb_hook = nullptr;
    }
}

static void ClearOneShotManual_()
{
    // 只清锁定目标数据；控制权模式由调用方经 SetControlMode_/直接赋值归位。
    g_auto_state.oneshot_stack = nullptr;
    g_auto_state.oneshot_side = -1;
    g_auto_state.oneshot_slot = -1;
    g_auto_state.oneshot_creature = -1;
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
                LogDebug("[Control] status shown: %s", text);
                return;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    LogDebug("[Control] status (no dlg): %s", text);
}

static const char* ControlModeLabel_()
{
    switch (g_control) {
    case CM_ONESHOT_LOCKED: return T("hud.mode_oneshot");
    case CM_ONESHOT_WAIT:   return T("hud.mode_wait");
    case CM_MANUAL:         return T("hud.mode_manual");
    default:                return T("hud.mode_auto");
    }
}

static void RefreshControlStatusHint_()
{
    char buf[64] = {};
    _snprintf(buf, sizeof(buf) - 1, T("hud.prefix"), ControlModeLabel_());
    ShowControlStatus_(buf);
}

// 当前是否已有本插件提交/等待中的动作，不能中途打断。
static bool HasInFlightAutoAction_(_BattleMgr_* mgr)
{
    if (!mgr) return false;
    if (g_pipeline_stage == PS_SPELL_POSTED) return true;
    if (mgr->action != 0 && g_pipeline_stage == PS_HANDLED
        && mgr->active_stack == g_pipeline_stack)
        return true;
    if (g_auto_state.action_wake_stack) return true;
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
        g_control = CM_ONESHOT_WAIT;
        LogDebug("[Control] oneshot deferred (in-flight) slot=%d action=%d spell_wait=%d",
            stack->army_slot_ix, mgr->action,
            g_pipeline_stage == PS_SPELL_POSTED ? 1 : 0);
        return false;
    }

    g_control = CM_ONESHOT_LOCKED;
    g_auto_state.oneshot_stack = stack;
    g_auto_state.oneshot_side = stack->def_group_ix;
    g_auto_state.oneshot_slot = stack->army_slot_ix;
    g_auto_state.oneshot_creature = stack->creature_id;
    // 单次接管期间清掉本单位管线，避免残留游标/等待干扰人工。
    if (g_pipeline_stack == stack) {
        if (g_pipeline_stage == PS_SPELL_POSTED)
            ClearSpellWait_();
        g_pipeline_stage = PS_IDLE;
        g_pipeline_stack = nullptr;
    }
    if (g_auto_state.action_wake_stack == stack)
        g_auto_state.action_wake_stack = nullptr;

    LogDebug("[Control] oneshot armed%s side=%d slot=%d cid=0x%X",
        from_pending ? " (pending)" : "",
        g_auto_state.oneshot_side, g_auto_state.oneshot_slot,
        g_auto_state.oneshot_creature);
    RefreshControlStatusHint_();
    return true;
}

static void ArmOneShotManual_(_BattleMgr_* mgr)
{
    if (!mgr) {
        g_control = CM_ONESHOT_WAIT;
        RefreshControlStatusHint_();
        return;
    }
    if (IsTacticsPhase_(mgr) || IsHiddenBattle(mgr) || mgr->auto_combat) {
        g_control = CM_ONESHOT_WAIT;
        LogDebug("[Control] oneshot pending: tactics/hidden/auto_combat");
        RefreshControlStatusHint_();
        return;
    }
    _BattleStack_* stack = mgr->active_stack;
    if (!stack || stack->count_current <= 0 || !ActiveStackMatchesTrack_(stack)) {
        g_control = CM_ONESHOT_WAIT;
        LogWarn("[Control] oneshot pending: no matching active stack");
        RefreshControlStatusHint_();
        return;
    }
    if (!TryArmOneShotOnStack_(mgr, stack, false)) {
        g_control = CM_ONESHOT_WAIT;
        RefreshControlStatusHint_();
    }
}

static void ToggleBattleManual_()
{
    // 切到全手动时，单次接管无意义；切回自动时也清掉未完成的单次锁定。
    ClearOneShotManual_();
    SetControlMode_(g_control == CM_MANUAL ? CM_AUTO : CM_MANUAL);
}

// 回合控制权判定/消息入口调用：消费钩子记录的热键按下 + 处理待命单次接管。
static void PollControlHotkeys_(_BattleMgr_* mgr)
{
    EnsureCombatKbHook_();
    if (PanelOpen_()) {
        // 设置面板打开期间的热键交给面板自身处理，不生效。
        g_auto_state.kb_toggle_seen = false;
        g_auto_state.kb_oneshot_seen = false;
        return;
    }
    if (!IsGameWindowForegroundForHotkeys_()) {
        g_auto_state.kb_toggle_seen = false;
        g_auto_state.kb_oneshot_seen = false;
        return;
    }

    if (g_auto_state.kb_toggle_seen) {
        g_auto_state.kb_toggle_seen = false;
        ToggleBattleManual_();
    }

    // 全手动时不需要单次接管；松键也不保留待命。
    if (g_control != CM_MANUAL) {
        if (g_auto_state.kb_oneshot_seen) {
            g_auto_state.kb_oneshot_seen = false;
            if (g_control != CM_ONESHOT_LOCKED)
                ArmOneShotManual_(mgr);
        }
        // 待命：下一支可接管人类部队出现时锁定。
        if (g_control == CM_ONESHOT_WAIT && mgr) {
            _BattleStack_* stack = mgr->active_stack;
            if (stack && stack->count_current > 0
                && ActiveStackMatchesTrack_(stack)
                && !HasInFlightAutoAction_(mgr))
            {
                TryArmOneShotOnStack_(mgr, stack, true);
            }
        }
    }
    // MANUAL 分支不做事：所有切 CM_MANUAL 的路径都先清后切，
    // 残留即上游 bug，此处不做静默兜底（掩盖漏清路径）。
}

// 当前是否应把控制权留给玩家（最高优先级门）。
static bool ShouldYieldToPlayer_(_BattleMgr_* mgr)
{
    if (!mgr) return false;
    if (g_control == CM_MANUAL) return true;
    if (g_control != CM_ONESHOT_LOCKED) return false;
    _BattleStack_* stack = mgr->active_stack;
    if (!stack) return true; // 锁定中但活动单位暂不可见：仍不自动执行
    if (ActiveStackIdentityMatches_(stack,
            g_auto_state.oneshot_side, g_auto_state.oneshot_slot,
            g_auto_state.oneshot_creature))
        return true;
    // 活动单位已变且不是锁定部队：单次接管结束。
    LogDebug("[Control] oneshot expired by active change old_slot=%d new_side=%d new_slot=%d",
        g_auto_state.oneshot_slot, stack->def_group_ix, stack->army_slot_ix);
    ClearOneShotManual_();
    SetControlMode_(CM_AUTO);
    return false;
}

// 原版 0x4786B0 真正开始执行 action 时调用：确认单次接管的人工动作已消费。
int __stdcall HH_OnBattleActionExecute(HiHook* h, _BattleMgr_* This, int flags)
{
    __try {
        // action=1 是英雄施法，不结束单次接管：玩家可先施法再给该部队下命令。
        if (This && g_control == CM_ONESHOT_LOCKED
            && This->action != 0 && This->action != 1)
        {
            _BattleStack_* stack = This->active_stack;
            // 只在锁定部队本人提交动作时结束单次接管。
            // 不把“本插件提交的 last_handled”算作人工动作。
            const bool is_locked = stack
                && ActiveStackIdentityMatches_(stack,
                    g_auto_state.oneshot_side, g_auto_state.oneshot_slot,
                    g_auto_state.oneshot_creature);
            const bool is_auto_submitted = g_pipeline_stage == PS_HANDLED
                && g_pipeline_stack == stack;
            if (is_locked && !is_auto_submitted) {
                LogDebug("[Control] oneshot completed by player action=%d slot=%d",
                    This->action, stack ? stack->army_slot_ix : -1);
                ClearOneShotManual_();
                SetControlMode_(CM_AUTO);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return THISCALL_2(int, h->GetDefaultFunc(), This, flags);
}

void ResetAutoState()
{
    if (IsPanelActive())
        CloseSettingsPanel();
    g_auto_state.action_wake_stack = nullptr;
    g_pipeline_stage = PS_IDLE;   // 管线整体清（含 done/handled 残留）
    g_pipeline_stack = nullptr;
    g_auto_state.spell_wait_slot = -1;
    g_auto_state.spell_wait_key = -1;
    g_auto_state.spell_wait_frames = 0;
    g_auto_state.spell_wait_started = 0;
    g_auto_state.spell_mana_before = 0;
    g_auto_state.spell_casted_before = 0;
    // F9 全手动是用户对"本场谁执行"的显式选择；取消重打/重绑不清，
    // 否则用户切到全手动后保存设置，面板关闭即被静默切回自动。
    // 只清单次接管与施法等待等运行时数据。
    g_protect_checked_turn = -1;   // 战斗状态重置后重新判定首动保活
    g_enemy_hp_turn[0] = -1;       // 自动停止的最近两笔取样作废
    g_enemy_hp_turn[1] = -1;
    g_enemy_hp_value[0] = 0;
    g_enemy_hp_value[1] = 0;
    ClearOneShotManual_();
    // 重打/重绑不清 MANUAL（用户显式选择）；单次接管是运行时，回 AUTO。
    if (g_control == CM_ONESHOT_WAIT || g_control == CM_ONESHOT_LOCKED)
        g_control = CM_AUTO;
    g_auto_state.kb_toggle_seen = false;
    g_auto_state.kb_oneshot_seen = false;
    g_auto_state.last_status_text[0] = 0;
    // 策略是本进程内的已确认设置，战斗状态重置时保留；
    // 跟踪表是“当前战斗绑定”，进程重置时清空。
    ClearStackTracking_();
    LogInfo("Auto state reset; confirmed strategies preserved, tracking cleared.");
}

// 取消重打后战场回来：按稳定身份重排方案、清空本轮状态并强制重绑。
void EnsureStackTrackingBound()
{
    _BattleMgr_* mgr = o_BattleMgr;
    const int side = ResolveHumanSide_(mgr);
    if (!mgr || side < 0 || side > 1) return;

    H3AutoPolicy::StableStackIdentity current_identities[21] = {};
    int previous_slot_for_current[21] = {};
    BuildStableIdentitiesFromBattle_(mgr, side, current_identities);
    H3AutoPolicy::BuildStableStackSlotRemap(g_rule_identities, 21,
        current_identities, 21, previous_slot_for_current);

    AutoStackRule remapped[5][21] = {};
    const AutoStackRule default_rule = H3AutoPolicy::MakeDefaultRule();
    for (int p = 0; p < 5; ++p) {
        for (int current_slot = 0; current_slot < 21; ++current_slot) {
            const int previous_slot = previous_slot_for_current[current_slot];
            remapped[p][current_slot] = previous_slot >= 0
                ? g_profiles[p][previous_slot] : default_rule;
        }
    }
    memcpy(g_profiles, remapped, sizeof(g_profiles));
    memcpy(g_active_rules, g_profiles[g_active_profile], sizeof(g_active_rules));
    memcpy(g_rule_identities, current_identities, sizeof(g_rule_identities));
    // 首动保活字段在 AutoStackRule 内，随槽位规则整体重排，无单独处理。

    ++g_battle_attempt_id;
    ResetAutoState();
    BindStackTrackingFromBattle_();
    LogInfo("[Life] retry rebound attempt=%d side=%d with stable identity remap",
        g_battle_attempt_id, side);
}

static void ClearSpellWait_()
{
    // 撤销投递：本回合已投过键的 done 语义保留（POSTED→DONE 不回 IDLE，
    // 且锚定单位保留以拦截同回合重投）；管线整体清零由 ResetAutoState /
    // 接管清单位负责。
    if (g_pipeline_stage == PS_SPELL_POSTED)
        g_pipeline_stage = PS_SPELL_DONE;
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
        LogDebug("[Auto] action wake posted slot=%d hwnd=%p client=(%d,%d)",
            self->army_slot_ix, hwnd, pt.x, pt.y);
    } else {
        LogError("[Auto] action wake failed slot=%d hwnd=%p",
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
        LogError("[Spell] SoD_SP.dll not loaded; cannot cast quickspell digit=%d",
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
        LogError("[Spell] cannot inspect SoD_SP quickspell slot=%d base=%p",
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
    // WriteLog("[Spell] request digit=%d slot=%d spell=%d targetHex=%d targetStack=%p flags=0x%X side=%d human=%d tactics=%d hero=%p casted=%d mana=%d action=%d",
    //     digit, slot, spell_id, target_hex, target_stack, spell_flags,
    //     side, is_human, tactics,
    //     (side >= 0 && side <= 1) ? mgr->hero[side] : nullptr,
    //     GetHeroCasted_(mgr, side), GetHeroMana_(mgr, side), mgr->action);

    if (spell_id < 0 || spell_id >= 70 || (spell_flags & 1) == 0)
        LogWarn("[Spell] SoD_SP slot appears empty/invalid; still posting digit for other hooks slot=%d spell=%d flags=0x%X",
            slot, spell_id, spell_flags);

    HWND hwnd = *reinterpret_cast<HWND*>(0x699650);
    if (!hwnd) {
        LogError("[Spell] game window unavailable digit=%d spell=%d", digit, spell_id);
        return false;
    }

    const WPARAM win_vk = static_cast<WPARAM>('0' + digit);
    const LPARAM key_down = 1 | (static_cast<LPARAM>(h3vk) << 16);
    const LPARAM key_up = key_down | (static_cast<LPARAM>(1) << 30)
        | (static_cast<LPARAM>(1) << 31);
    __try {
        const BOOL down_ok = PostMessageA(hwnd, WM_KEYDOWN, win_vk, key_down);
        const BOOL up_ok = PostMessageA(hwnd, WM_KEYUP, win_vk, key_up);
        // WriteLog("[Spell] posted quick key digit=%d h3vk=%d spell=%d hwnd=%p down=%d up=%d",
        //     digit, h3vk, spell_id, hwnd, down_ok ? 1 : 0, up_ok ? 1 : 0);
        if (!down_ok || !up_ok)
            return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogError("[Spell] posting quick key crashed digit=%d spell=%d", digit, spell_id);
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

// ==== 首动保活（§3.1.1） ====
// 每回合第一次把控制权交给玩家时（含本插件接管）判一次：遍历队列
// （卡片勾选「加入保活队列」）内的己方部队，按方案级策略
// （无 / 部队全灭后 / 回合内首动 / 损失量大于恢复量）判定够格，
// 够格者中取血量最低一支，按其亡灵/活体自动选聚灵(39)/复活(38)，
// 直接 CastSpell 施放。每回合只判一次（成败不重试）；英雄本回合已施法
// 跳过；不占快捷施法、不推进任何部队的循环施法游标。

// 设置面板提交后调用：作废已判定标记，当前/下一回合重新判定。
void SyncActiveProtect()
{
    if (g_protect_checked_turn != -1) {
        g_protect_checked_turn = -1;
        LogInfo("[Protect] settings committed; player turn re-checks");
    }
}

// 设置保存后进入「停」：保存只落方案，不立即自动执行；
// 由 F9（启停打铁）启动。重复调用不重复记日志。
void PauseAutoExecution()
{
    if (g_control != CM_MANUAL) {
        ClearOneShotManual_();
        SetControlMode_(CM_MANUAL);
        LogInfo("[Control] paused after commit; press toggle hotkey to start");
    }
}

// 战斗回合号：H3CombatManager::turn（tacticsPhase 之后，H3API.hpp:19445）。
// _BattleMgr_（Compat）没有该字段，须走 H3CombatManager::Get()。
static int StackHex_(_BattleStack_* stack)
{
    if (!stack) return -1;
    const int hex = reinterpret_cast<H3CombatCreature*>(stack)->position;
    return (hex >= 0 && hex <= 186) ? hex : -1;
}

static int GetCurrentBattleTurn_(_BattleMgr_* mgr)
{
    if (!mgr) return -1;
    __try {
        if (H3CombatManager* cm = H3CombatManager::Get())
            return cm->turn;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

// 敌方当前总血量：存活数 × 满血 − 顶层已损。战争机器不计入。
static int EnemyAliveHp_(_BattleMgr_* mgr)
{
    if (!mgr) return 0;
    const int side = ResolveHumanSide_(mgr);
    if (side < 0 || side > 1) return 0;
    const int enemy = 1 - side;
    int64_t total = 0;
    for (int i = 0; i < 21; ++i) {
        _BattleStack_* st = &mgr->stack[enemy][i];
        if (st->count_current <= 0 || st->count_at_start <= 0) continue;
        if (H3AutoPolicy::IsWarMachineType(st->creature_id)) continue;
        H3AutoPolicy::TargetCandidate cand = {};
        cand.count_current = st->count_current;
        cand.count_at_start = st->count_at_start;
        cand.hit_points = st->creature.hit_points;
        cand.lost_hp = st->lost_hp;
        total += H3AutoPolicy::StackRemainingHp(cand);
    }
    if (total > 0x7FFFFFFF) total = 0x7FFFFFFF;
    return static_cast<int>(total);
}

// 预计敌方会在阈值回合内全灭时切到全手动。
// 只看最近两笔取样的掉血，不用更早的回合。
// 守卫（S5）：仅战斗中·面板关 + 自动模式下判（设计文档 §3.1.2）。
static void TryAutoStop_(_BattleMgr_* mgr)
{
    if (g_phase != BP_COMBAT_CLOSED) return;
    if (g_control != CM_AUTO) return;
    if (!mgr) return;
    const int profile = g_active_profile;
    if (profile < 0 || profile >= 5) return;
    const int threshold = g_stop_turns[profile];
    if (threshold <= 0) return;

    const int turn = GetCurrentBattleTurn_(mgr);
    if (turn < 0 || turn == g_enemy_hp_turn[1]) return;
    const int hp = EnemyAliveHp_(mgr);
    if (g_enemy_hp_turn[1] >= 0 && hp > g_enemy_hp_value[1]) {
        g_enemy_hp_turn[0] = -1;
        g_enemy_hp_value[0] = 0;
    } else {
        g_enemy_hp_turn[0] = g_enemy_hp_turn[1];
        g_enemy_hp_value[0] = g_enemy_hp_value[1];
    }
    g_enemy_hp_turn[1] = turn;
    g_enemy_hp_value[1] = hp;
    if (g_enemy_hp_turn[0] < 0) return;

    const int elapsed = g_enemy_hp_turn[1] - g_enemy_hp_turn[0];
    const int left = H3AutoPolicy::ProjectEnemyTurnsLeft(
        threshold, g_enemy_hp_value[0], hp, elapsed);
    if (left < 0 || left > threshold) return;

    g_control = CM_MANUAL; // 自动停止：等同 F9 交回玩家
    ClearOneShotManual_();
    LogInfo("[Auto] 自动停止：最近 %d 回合敌方血量 %d→%d，预计还需 %d（阈值 %d）",
        elapsed, g_enemy_hp_value[0], hp, left, threshold);
    RefreshControlStatusHint_();
}

// 守卫（S5）：仅战斗中·面板关 + 自动模式下判（设计文档 §3.1.1：
// 全手动、单次接管时不判、不施——原实现缺此守卫，本次修正）。
static bool TryProtectCast_(_BattleMgr_* mgr)
{
    if (g_phase != BP_COMBAT_CLOSED) return false;
    if (g_control != CM_AUTO) return false; // AUTO 才判保活/施法（§3.1.1）

    const int strategy = g_protect_strategy[g_active_profile];
    if (strategy == H3AutoPolicy::PS_NONE) return false;

    // 判定时机按策略分派：
    // - 回合内首动：每回合只判一次（判过即记，无论是否施法）；
    // - 部队全灭后 / 损失量大于恢复量：事件型，每次行动（控制权交玩家）
    //   都判。游戏一回合只允许施法一次：已施法/法力不足时静默跳过，
    //   不记回合标记，之后的行动继续判。
    const bool once_per_turn =
        strategy == (int)H3AutoPolicy::PS_FIRST_ACTION;
    const int turn = GetCurrentBattleTurn_(mgr);
    if (turn < 0) return false;
    if (once_per_turn) {
        if (turn == g_protect_checked_turn) return false;
        g_protect_checked_turn = turn;      // 本回合只判这一次
    }

    const int side = ResolveHumanSide_(mgr);
    if (side < 0 || side > 1) return false;
    if (GetHeroCasted_(mgr, side)) {
        if (once_per_turn)
            LogWarn("[Protect] turn=%d hero already casted; skip", turn);
        return false;
    }

    H3CombatManager* cm = H3CombatManager::Get();
    if (!cm) return false;
    H3Hero* hero = reinterpret_cast<H3Hero*>(mgr->hero[side]);
    if (!hero) return false;
    // 复活/聚灵固定耗魔 10（SoD，不随等级变化）。
    if (GetHeroMana_(mgr, side) < 10) {
        if (once_per_turn)
            LogWarn("[Protect] turn=%d mana<10; skip", turn);
        return false;
    }
    const int spell_power = cm->heroSpellPower[side];

    // 收集队列内够格候选：勾选 + 按方案策略判定。
    // 够格者中统一取血量最低（全灭者剩余 0 天然最前）。
    H3AutoPolicy::TargetCandidate cands[21] = {};
    int cand_slot[21] = {}, cand_spell[21] = {}, cand_exp[21] = {};
    int cand_count = 0;
    for (int slot = 0; slot < 21; ++slot) {
        const AutoStackRule& rule = g_active_rules[slot];
        if (!rule.protectEnable) continue;

        const StackTrackEntry& te = g_stack_track[slot];
        if (!te.bound || te.side != side) continue;
        _BattleStack_* st = &mgr->stack[side][slot];
        if (!st || st->count_at_start <= 0) continue;
        if (!CreatureInfoIndexValid_(st->creature_id)) continue;
        const bool dead = st->count_current <= 0;
        if (dead && StackHex_(st) < 0) continue;          // 没有可施法的尸体格

        // 损失口径与急救"失血数值"一致：死亡数×满血 + 顶层已损。
        // 满血取战场单位内嵌 CreatureInfo（st+0x74 再 +0x4C）。
        H3AutoPolicy::TargetCandidate cand = {};
        cand.count_current = st->count_current;
        cand.count_at_start = st->count_at_start;
        cand.hit_points     = st->creature.hit_points;
        cand.lost_hp        = st->lost_hp;
        const int wound = H3AutoPolicy::WoundValue(cand);

        // 亡灵→聚灵(39)，活体→复活(38)；按英雄当前等级算可恢复量。
        const int spell_id = P_CreatureInformation[st->creature_id].undead
            ? 39 : 38;
        const int expertise = hero->GetSpellExpertise(spell_id, cm->specialTerrain);
        if (expertise <= 0) continue;                   // 没学该法术
        const int restorable =
            H3AutoPolicy::ResurrectionRestoreHp(expertise, spell_power);

        if (!H3AutoPolicy::ProtectShouldCast(true,
                (H3AutoPolicy::ProtectStrategy)strategy,
                restorable, wound, dead))
            continue;
        cands[cand_count] = cand;
        cand_slot[cand_count] = slot;
        cand_spell[cand_count] = spell_id;
        cand_exp[cand_count] = expertise;
        ++cand_count;
    }
    const int picked = H3AutoPolicy::SelectProtectTargetIndex(cands, cand_count);
    if (picked < 0) return false;

    const int best_slot = cand_slot[picked];
    const int best_spell = cand_spell[picked];
    const int best_exp = cand_exp[picked];
    const int best_remaining =
        H3AutoPolicy::StackRemainingHp(cands[picked]);
    const int best_wound = H3AutoPolicy::WoundValue(cands[picked]);

    _BattleStack_* st = &mgr->stack[side][best_slot];
    // cast_type_012=0：单体施法（逆向语义后续验证，见设计文档 §10.4）。
    // 原版施法失败不能逃出战斗回调。
    __try {
        cm->CastSpell(best_spell, StackHex_(st), 0, -1, best_exp, spell_power);
    } __except (1) {
        return false;
    }
    return true;
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

static bool CanYieldFailedActionToPlayer_(_BattleMgr_* mgr, int creature_id)
{
    return H3AutoPolicy::CanYieldFailedActionToPlayer(creature_id,
        HeroHasSkill(mgr, SK_BALLISTICS),
        HeroHasSkill(mgr, SK_ARTILLERY),
        HeroHasSkill(mgr, SK_FIRST_AID));
}

// CommitProfiles：勾号/Enter 一次性提交全部 5 套内存方案，当前选中方案立即生效。
void CommitProfiles(int active_profile, AutoStackRule rules[5][21],
    const uint8_t protect_strategy[5], const uint16_t stop_turns[5])
{
    if (active_profile < 0 || active_profile >= 5)
        active_profile = 0;
    memcpy(g_profiles, rules, sizeof(g_profiles));
    for (int p = 0; p < 5; ++p) {
        g_protect_strategy[p] = protect_strategy ? protect_strategy[p] : 0;
        int turns = stop_turns ? stop_turns[p] : H3AutoPolicy::DEFAULT_STOP_TURNS;
        if (turns < 0) turns = 0;
        if (turns > 999) turns = 999;
        g_stop_turns[p] = static_cast<uint16_t>(turns);
    }
    g_active_profile = active_profile;

    // 身份按本场现编（含召唤物：本场内可执行，重打重排自动丢弃其规则）。
    const int side = ResolveHumanSide_(o_BattleMgr);
    BuildStableIdentitiesFromBattle_(o_BattleMgr, side, g_rule_identities);
    memcpy(g_active_rules, g_profiles[g_active_profile], sizeof(g_active_rules));
    if (g_battle_attempt_id <= 0) g_battle_attempt_id = 1;
    int spell_rules = 0;
    int action_rules = 0;
    for (int i = 0; i < 21; ++i) {
        if (g_active_rules[i].action != AA_MANUAL) ++action_rules;
        if (g_active_rules[i].spellSlotCount > 0) {
            ++spell_rules;
            LogInfo("[Auto] commit slot=%d action=%d spells=%d first=%d",
                i, (int)g_active_rules[i].action,
                (int)g_active_rules[i].spellSlotCount,
                (int)g_active_rules[i].spellSlots[0]);
        }
    }
    LogInfo("[Auto] 5 profiles committed; active profile=%d actions=%d spells=%d",
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

// 失血计算与候选评分归纯策略核心，避免单元测试和游戏执行器各算一遍。
static H3AutoPolicy::TargetCandidate TargetCandidateOf_(_BattleStack_* t)
{
    H3AutoPolicy::TargetCandidate c = {};
    if (!t) return c;
    if (!CreatureInfoIndexValid_(t->creature_id)) return c;
    c.count_current = t->count_current;
    c.count_at_start = t->count_at_start;
    c.hit_points = t->creature.hit_points;
    c.lost_hp = t->lost_hp;
    c.shots = t->creature.shots;
    c.speed = t->creature.speed;
    c.flyer = P_CreatureInformation[t->creature_id].flyer ? 1 : 0;
    return c;
}

static int WoundValueOf_(_BattleStack_* t)
{
    return t ? H3AutoPolicy::WoundValue(TargetCandidateOf_(t)) : 0;
}

static int WoundRatioKey_(_BattleStack_* t)
{
    return t ? H3AutoPolicy::WoundRatioKey(TargetCandidateOf_(t)) : 0;
}

// 通用部队选择：side_filter = 0己方 / 1敌方 / 2双方；require_wounded 用于急救。
// 远程：随机 / 远程飞兵高速优先 / 数量最多；急救：随机 / 失血比例 / 失血数值。
static _BattleStack_* SelectStackTarget_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule, int side_filter, bool require_wounded)
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
            if (!CreatureInfoIndexValid_(t->creature_id)
                || IsWarMachineCid_(t->creature_id)) continue;
            if (require_wounded
                && t->lost_hp <= 0 && t->count_current >= t->count_at_start)
                continue;
            candidates[count++] = t;
        }
    }
    if (count <= 0) return nullptr;

    H3AutoPolicy::TargetCandidate scored[42] = {};
    for (int i = 0; i < count; ++i)
        scored[i] = TargetCandidateOf_(candidates[i]);
    const int selected = H3AutoPolicy::SelectTargetIndex(
        scored, count, rule.target.selector,
        static_cast<uint32_t>(rand()));
    if (selected < 0) {
        LogWarn("[Auto] target selector rejected count=%d selector=%d side=%d wounded=%d",
            count, (int)rule.target.selector, side_filter, require_wounded ? 1 : 0);
        return nullptr;
    }
    return candidates[selected];
}

// 解析位置目标：用部队目标的位置作锚点（循环移动旧单目标兜底）。
static int ResolvePositionTarget_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule)
{
    if (!mgr || !self) return -1;
    int side_filter = 2;
    if (rule.target.side == ATS_OWN) side_filter = 0;
    else if (rule.target.side == ATS_ENEMY) side_filter = 1;
    _BattleStack_* t = SelectStackTarget_(mgr, self, rule, side_filter, false);
    if (t) return StackHex_(t);
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
    g_pipeline_stage = PS_HANDLED;   // 本回合已处理，防重复下命令
    g_pipeline_stack = self;
    LogDebug("[Auto] submit DEFEND slot=%d creature=0x%X",
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
    g_pipeline_stage = PS_HANDLED;   // 本回合已处理，防重复下命令
    g_pipeline_stack = self;
    return true;
}

// 提交远程攻击：action=7, actionParameter=目标 hex。
static bool SubmitRanged_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    _BattleStack_* target = SelectStackTarget_(mgr, self, rule, 1, false);
    if (!target) {
        LogWarn("[Auto] ranged target unavailable slot=%d cid=0x%X selector=%d",
            self->army_slot_ix, self->creature_id, (int)rule.target.selector);
        return false;
    }
    const int target_hex = StackHex_(target);
    if (target_hex < 0) {
        LogWarn("[Auto] ranged target hex invalid slot=%d target_slot=%d cid=0x%X",
            self->army_slot_ix, target->army_slot_ix, target->creature_id);
        return false;
    }
    // 与近战一致：目标格走 actionTarget；actionParameter 不承载射击落点。
    if (!WriteAction_(mgr, self, BA_SHOOT, -1, target_hex))
        return false;
    LogDebug("[Auto] submit SHOOT slot=%d -> hex=%d target_slot=%d",
        self->army_slot_ix, target_hex, target->army_slot_ix);
    return true;
}

// 判断原版是否认为当前活动部队可以走到目标 hex。
// FUN_00475DC0 是战场鼠标悬停时使用的原版判定：它会按当前活动部队、
// 障碍物、双格体型和本回合移动力计算目标状态，并刷新 accessibleSquares2。
// 返回 1/2 表示普通移动光标；其它值（尤其 0）都视为本次不能移动。
// 这里宁可放弃自动动作，也不能把一个已知不可达的目标写入 action。
static bool IsMoveTargetReachable_(_BattleMgr_* mgr, _BattleStack_* self, int hex)
{
    if (!mgr || !self || hex < 1 || hex > 185) return false;

    __try {
        H3CombatManager* cm = H3CombatManager::Get();
        if (!cm) return false;
        // 原版判定函数从 manager->activeStack 取当前部队，不能拿它去
        // 验证另一支部队，否则会把别人的可达性误套到当前配置上。
        if (cm->activeStack != reinterpret_cast<H3CombatCreature*>(self))
            return false;

        const int move_type = THISCALL_2(int, 0x475DC0, mgr, hex);
        const int access = static_cast<int>(cm->accessibleSquares2[hex]);
        const bool reachable = (move_type == 1 || move_type == 2)
            && (access & 2) != 0; // eSquareAccess::CAN_REACH
        LogDebug("[Auto] move reachability slot=%d hex=%d type=%d reachable=%d access=%d",
            self->army_slot_ix, hex, move_type, reachable ? 1 : 0, access);
        return reachable;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogError("[Auto] move reachability exception slot=%d hex=%d",
            self->army_slot_ix, hex);
        return false;
    }
}

// 提交移动：action=2, actionTarget=目标 hex。
// 循环移动：按 moveWaypoints 有序巡逻，逐点走向下一个路径点，到达后推进游标（末点回首点）。
// 无路径点时回退到旧的单目标移动（固定位置 / 靠近目标部队）。
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
        // 规范化运行时游标，但先只使用局部候选值；只有动作成功写入后才提交，
        // 这样“站在当前点、下一点不可达”时不会提前推进移动游标。
        int cur = runtime.move_cursor;
        if (cur < 0 || cur >= n) cur = 0;

        // 到点才推进（方案 A）：已站在当前点，则切到下一个点；
        // 若配置含重复点，跳过连续的脚下点。
        if (StackHex_(self) == wps[cur])
            cur = (cur + 1) % n;
        int guard = 0;
        while (wps[cur] == StackHex_(self) && guard < n) {
            cur = (cur + 1) % n;
            ++guard;
        }

        int hex = wps[cur];
        if (hex == StackHex_(self)) return false; // 所有点都在脚下
        if (!IsMoveTargetReachable_(mgr, self, hex)) {
            LogWarn("[Auto] WALK target unreachable slot=%d hex=%d cursor=%d/%d; no cursor advance",
                self->army_slot_ix, hex, cur, n);
            return false;
        }
        if (!WriteAction_(mgr, self, BA_WALK, -1, hex))
            return false;
        runtime.move_cursor = cur;
        LogDebug("[Auto] submit WALK(patrol) slot=%d -> hex=%d cursor=%d/%d",
            self->army_slot_ix, hex, cur, n);
        return true;
    }

    // 回退：旧单目标移动。
    int hex = ResolvePositionTarget_(mgr, self, rule);
    if (hex < 1 || hex > 185) return false;
    if (hex == StackHex_(self)) return false;
    if (!IsMoveTargetReachable_(mgr, self, hex)) {
        LogWarn("[Auto] WALK target unreachable slot=%d hex=%d; player/fallback path",
            self->army_slot_ix, hex);
        return false;
    }
    if (!WriteAction_(mgr, self, BA_WALK, -1, hex))
        return false;
    LogDebug("[Auto] submit WALK slot=%d -> hex=%d",
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
        if (StackHex_(t) == hex) return t;
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
        LogWarn("[Auto] melee pair invalid slot=%d cursor=%d/%d stand=%d attack=%d",
            self->army_slot_ix, cursor, count, stand_hex, attack_hex);
        return false;
    }

    _BattleStack_* enemy = FindEnemyOccupyingHex_(mgr, self, attack_hex);
    if (!enemy) {
        LogInfo("[Auto] melee attack hex=%d empty (no enemy head/tail) slot=%d",
            attack_hex, self->army_slot_ix);
        return false;
    }
    if (!IsMoveTargetReachable_(mgr, self, stand_hex)
        && StackHex_(self) != stand_hex) {
        LogWarn("[Auto] melee stand unreachable slot=%d stand=%d attack=%d",
            self->army_slot_ix, stand_hex, attack_hex);
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
    LogDebug("[Auto] submit MELEE(loop) pair=%d/%d stand=%d attack=%d enemy_slot=%d enemy_hex=%d next=%d",
        cursor, count, stand_hex, attack_hex, enemy->army_slot_ix, StackHex_(enemy),
        legacy ? 0 : runtime.melee_cursor);
    return true;
}

// 提交等待：action=8。
static bool SubmitWait_(_BattleMgr_* mgr, _BattleStack_* self)
{
    if (!WriteAction_(mgr, self, BA_WAIT, -1, -1))
        return false;
    LogDebug("[Auto] submit WAIT slot=%d", self->army_slot_ix);
    return true;
}

// 提交急救：action=11, actionParameter=己方伤员 hex。
static bool SubmitFirstAid_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    _BattleStack_* target = SelectStackTarget_(mgr, self, rule, 0, true);
    if (!target) return false;
    const int target_hex = StackHex_(target);
    if (target_hex < 0) return false;
    if (!WriteAction_(mgr, self, BA_FIRST_AID, target_hex, -1))
        return false;
    LogDebug("[Auto] submit FIRST_AID slot=%d -> hex=%d target_slot=%d",
        self->army_slot_ix, target_hex, target->army_slot_ix);
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
    if (g_phase != BP_COMBAT_CLOSED) return false; // 状态机守卫（S5.3）
    if (mgr->auto_combat) return false;
    if (IsHiddenBattle(mgr)) return false;
    if (IsTacticsPhase_(mgr)) return false;
    if (mgr->action != 0) return false;
    UpdateStackTracking_();
    _BattleStack_* self = mgr->active_stack;
    if (!self || self->count_current <= 0) return false;

    // 活动单位变化：旧单位的管线残留整体清掉（等待/完成/已处理）。
    if (g_pipeline_stack && g_pipeline_stack != self) {
        if (g_pipeline_stage == PS_SPELL_POSTED)
            ClearSpellWait_();
        g_pipeline_stage = PS_IDLE;
        g_pipeline_stack = nullptr;
    }
    if (g_auto_state.action_wake_stack
        && g_auto_state.action_wake_stack != self)
        g_auto_state.action_wake_stack = nullptr;
    if (g_pipeline_stage == PS_HANDLED && g_pipeline_stack == self)
        return false; // 本单位已处理完

    // 必须是跟踪表中仍存活、身份匹配的人类侧部队。
    if (!ActiveStackMatchesTrack_(self)) {
        if (g_pipeline_stage == PS_SPELL_POSTED)
            ClearSpellWait_();
        static void* s_last_mismatch = nullptr;
        if (s_last_mismatch != self) {
            s_last_mismatch = self;
            LogWarn("[Track] skip action: mismatch side=%d slot=%d cid=0x%X count=%d",
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
    if (want_spell && g_pipeline_stage != PS_SPELL_POSTED
        && !(g_pipeline_stage == PS_SPELL_DONE && g_pipeline_stack == self)) {
        // 本英雄本回合已施过法：跳过施法，直接进入部队动作。
        if (GetHeroCasted_(mgr, side) != 0) {
            LogWarn("[Spell] already cast this turn side=%d; skip quick key=%d",
                side, spell_key);
            // 英雄每回合只能施法一次；本部队没有实际尝试，不消费循环槽位。
            g_pipeline_stage = PS_SPELL_DONE;
            g_pipeline_stack = self;
        } else {
            g_auto_state.spell_mana_before = GetHeroMana_(mgr, side);
            g_auto_state.spell_casted_before = GetHeroCasted_(mgr, side);
            g_auto_state.spell_wait_started = GetTickCount();
            if (!TriggerQuickSpellDigit_(spell_key)) {
                LogWarn("[Spell] trigger failed digit=%d; fallthrough to unit action",
                    spell_key);
                AdvanceSpellCursor_(runtime, rule.spellSlotCount);
                g_pipeline_stage = PS_SPELL_DONE;
                g_pipeline_stack = self;
            } else {
                g_pipeline_stage = PS_SPELL_POSTED;
                g_pipeline_stack = self;
                g_auto_state.spell_wait_slot = idx;
                g_auto_state.spell_wait_key = spell_key;
                g_auto_state.spell_wait_frames = 0;
                LogDebug("[Spell] wait start slot=%d key=%d mana=%d casted=%d",
                    idx, spell_key, g_auto_state.spell_mana_before,
                    g_auto_state.spell_casted_before);
                return false; // 本帧只投键，不提交部队动作
            }
        }
    }

    // —— 阶段 2：等待施法结果 ——
    if (g_pipeline_stage == PS_SPELL_POSTED && g_pipeline_stack == self) {
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

        LogDebug("[Spell] wait end slot=%d key=%d calls=%d elapsed=%lu cast_done=%d timed_out=%d mana %d->%d casted %d->%d",
            idx, g_auto_state.spell_wait_key, g_auto_state.spell_wait_frames,
            (unsigned long)elapsed,
            cast_done ? 1 : 0, timed_out ? 1 : 0,
            g_auto_state.spell_mana_before, mana_now,
            g_auto_state.spell_casted_before, casted_now);

        // 无论成功/失败/超时都推进游标，避免同一键卡死整场。
        AdvanceSpellCursor_(runtime, rule.spellSlotCount);
        ClearSpellWait_(); // POSTED→DONE，锚定保留
        // 施法动画/选择目标期间 action 可能被占用；若仍非 0 则下帧再提交主动作。
        if (mgr->action != 0)
            return false;
    }

    // —— 阶段 3：提交部队主动作 ——
    if (!want_action) {
        // 仅循环施法、主动作为手动：施法流程结束后交回玩家。
        g_pipeline_stage = PS_HANDLED;
        g_pipeline_stack = self;
        return false;
    }

    if (!allow_unit_action) {
        RequestUnitActionWake_(self);
        return false;
    }

    const bool ok = SubmitConfiguredUnitAction_(mgr, self, rule, runtime);
    if (ok) {
        LogDebug("[Auto] action consumed wake slot=%d action=%d",
            self->army_slot_ix, mgr->action);
        g_auto_state.action_wake_stack = nullptr;
    } else if (!rule.allowDefendFallback
        && CanYieldFailedActionToPlayer_(mgr, self->creature_id)) {
        // 不允许降级且配置动作无法落地：本回合停止插件重试，保留原版
        // 人工输入路径。战争机器须先通过对应技能资格判断。
        g_pipeline_stage = PS_HANDLED;
        g_pipeline_stack = self;
        g_auto_state.action_wake_stack = nullptr;
        LogWarn("[Auto] configured action failed; yield to player slot=%d cid=0x%X action=%d",
            self->army_slot_ix, self->creature_id, (int)rule.action);
    }
    return ok;
}

// DecideTakeover：仅在“原版本会把控制权交给玩家”时被询问（HH 里 orig==0）。
// 普通部队：有配置则本插件提交动作；手动则留给玩家。
// 战争机器：才有“交回 AI / 保持原版 / 按配置执行”的特殊分支。
int DecideTakeover(_BattleMgr_* mgr)
{
    if (!mgr) return CD_KEEP_ORIGINAL;
    if (g_phase != BP_COMBAT_CLOSED) return CD_KEEP_ORIGINAL; // 状态机守卫（S5.3）
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
            LogWarn("[Track] takeover mismatch ptr=%p side=%d slot=%d cid=0x%X count=%d track_active=%d expected_bound=%d expected_alive=%d expected_side=%d expected_cid=0x%X",
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
                LogDebug("[Spell] takeover slot=%d spells=%d first=%d",
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
    if (g_phase != BP_COMBAT_CLOSED) return false; // 状态机守卫（S5.3）
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
        // 每次行动（控制权交玩家）都判保活：回合内首动每回合只判一次；
        // 部队全灭后 / 损失量大于恢复量为事件型，每次行动都判，
        // 已施法/无法施法时内部静默跳过。仅 orig==0 路径判。
        TryProtectCast_(This);
        TryAutoStop_(This);
        if (This && This->active_stack) {
            // 活动单位变化：旧单位管线残留整体清（等待/完成/已处理）。
            if (g_pipeline_stack && g_pipeline_stack != This->active_stack) {
                if (g_pipeline_stage == PS_SPELL_POSTED)
                    ClearSpellWait_();
                g_pipeline_stage = PS_IDLE;
                g_pipeline_stack = nullptr;
            }
        }

        const int decision = DecideTakeover(This);
        if (decision == CD_HAND_TO_AI)
            return 1;           // 仅战争机器特殊：交回 AI
        // CD_EXECUTE_H3AUTO / CD_KEEP_ORIGINAL：返回 0（控制权在玩家路径）。
        // 若需代发动作，在 Hook_BattleMsgProc 入口提交。
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // WriteLog("[Auto] 行动判定发生异常 code=0x%08X", GetExceptionCode());
    }
    return 0;
}
