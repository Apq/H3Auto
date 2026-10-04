// AutoExecute.inc.cpp
// Automated battle execution module

static void LogInfo(const char* fmt, ...);  // 分级前向声明（LogWarn/LogError 等见 ConfigLog）

extern void CommitProfiles(int active_profile, AutoStackRule rules[5][21],
    const uint16_t stop_turns[5],
    const SummonProfileFields summon[5],
    const StatusProfileFields status[5], bool* out_store_added);
extern void ClearConfirmedProfiles();
extern AutoStackRule g_profiles[5][21];
extern AutoStackRule g_active_rules[21];
extern int  g_active_profile;
extern int  g_last_profile;
extern uint16_t g_stop_turns[5];
extern SummonProfileFields g_summon[5];
extern StatusProfileFields g_status[5];
extern bool IsPanelActive();
extern void CloseSettingsPanel();

// 战场阶段状态机（enum/g_phase/SetPhase_）：声明在 BattleState.hpp，
// 实现在 BattleState.inc.cpp（本文件之后 include）。
#include "BattleState.hpp"

// 定义在本文件后部：SetControlMode_ 边动作需要，先声明（C2065 预防）。
static void RefreshControlStatusHint_();
// 当前轮到行动的部队（按 current_mon_side/index，不用 active_stack）。
static _BattleStack_* CurrentTurnStack_(_BattleMgr_* mgr);

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
// 本激活窗口内是否已写入过主动作（SubmitDefend_/WriteAction_）。
// 士气高涨：同支部队消化完命令后立即再激活（无其它部队插入、活动单位
// 不变），PS_HANDLED 残留会挡住第二次行动；用「已落地 + action 已清 +
// 控制权判定点再次询问同一单位」识别这是新的一次行动机会。
static bool g_pipeline_action_landed = false;
static int g_pipeline_landed_turn = -1;   // 命令落地时的战场回合号
// 同单位再激活的回合号相同 = 士气额外行动（循环语义作用于回合：
// 循环移动→防御、循环近战→重打本回合组合不推进游标）；
// 回合号前进 = 新回合（极端情况：场上只剩一支部队，回合背靠背）。
static bool g_pipeline_morale_extra = false;
static int g_pipeline_morale_turn = -1;

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
    int   count_start;   // 本场首次见到时的开战数量（F9/确定都不改）
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
// 自动停止：最近两笔敌方血量。两笔都有效才外推，更早的不参与。
static int g_enemy_hp_turn[2] = { -1, -1 };
static int g_enemy_hp_value[2] = {};
// 召唤通道运行态：自动模式本场锁定的元素下标（0..3，-1 未锁定）。
// 首次成功施放后锁定，战斗结束/重打/状态重置清空（ResetAutoState）。
static int g_summon_locked_spell = -1;

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

// 损失量基准：英雄头像 Alt+右键显示的战前军队数量（H3Hero::army，
// 读档关联用的同一份）。战斗中不写回，F9 重打和按确定都不改它。
// 召唤物、克隆、战争机器没有这份数量，退回战场数量。
static int ProtectBaselineCount_(_BattleMgr_* mgr, int side, _BattleStack_* s)
{
    if (!s) return 0;
    const int army = s->source_army_slot;
    if (mgr && side >= 0 && side <= 1 && mgr->hero[side]
        && army >= 0 && army <= 6 && s->clone_id <= 0) {
        const int pre = mgr->hero[side]->army_count[army];
        if (pre > 0) return pre;
    }
    return (s->count_at_start > 0) ? s->count_at_start : s->count_current;
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
        t.count_start = ProtectBaselineCount_(mgr, side, s);
        t.alive = s->count_current > 0;
        t.move_cursor = 0;
        t.melee_cursor = 0;
        t.spell_cursor = 0;
        // 绑定时刻已在场的召唤物（重开面板再勾号的场景）：
        // 召唤启用时直接套用共享规则。
        if (g_summon[g_active_profile].enabled
            && current_identities[i].kind == H3AutoPolicy::STACK_ID_SUMMON
            && H3AutoPolicy::IsSummonedElemental(s->creature_id)) {
            g_active_rules[i] = g_summon[g_active_profile].summon_rule;
        }
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

    // —— 动态补绑召唤物（§2.3）——
    // 首次出现的四系元素（身份 STACK_ID_SUMMON + 槽未绑定）在本场内接管：
    // 绑定跟踪条目并套用召唤共享规则（游标归零）。克隆物（类型不在四系）
    // 不绑、保持手动。仅召唤启用（enabled）时补绑。
    if (g_summon[g_active_profile].enabled) {
        for (int i = 0; i < 21; ++i) {
            if (g_stack_track[i].bound) continue;
            const H3AutoPolicy::StableStackIdentity& id = current_identities[i];
            if (id.kind != H3AutoPolicy::STACK_ID_SUMMON) continue;
            _BattleStack_* s = &mgr->stack[g_track_side][i];
            if (s->count_current <= 0 || s->count_at_start <= 0) continue;
            if (!H3AutoPolicy::IsSummonedElemental(s->creature_id)) continue;

            StackTrackEntry& t = g_stack_track[i];
            t.bound = true;
            t.identity = id;
            t.attempt_id = g_battle_attempt_id;
            t.side = g_track_side;
            t.slot = i;
            t.creature_id = s->creature_id;
            t.hex = s->hex_ix;
            t.count_alive = s->count_current;
            t.count_start = s->count_at_start;
            t.alive = true;
            t.move_cursor = 0;
            t.melee_cursor = 0;
            t.spell_cursor = 0;
            g_active_rules[i] = g_summon[g_active_profile].summon_rule;
            g_track_active = true;
            LogInfo("[Summon] 补绑召唤物 slot=%d cid=0x%X hex=%d count=%d action=%d",
                i, s->creature_id, s->hex_ix, s->count_current,
                (int)g_summon[g_active_profile].summon_rule.action);
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
        if ((int)wParam == cfg.toggle_manual_vk) {
            g_auto_state.kb_toggle_seen = true;
            // 边沿日志：定位「打一回合就停」是不是第二次 F9 按下造成的。
            // 只在按下边沿记（bit30/bit31 已过滤自动重复与抬起），每次物理
            // 按键最多一行。
            LogDebug("[Control] 捕获启停热键按下 vk=0x%X", (int)wParam);
        }
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
        g_pipeline_action_landed = false;
        g_pipeline_morale_extra = false;
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
        LogDebug("[Control] 消费启停热键：%s -> %s",
            ControlModeName_(g_control),
            ControlModeName_(g_control == CM_MANUAL ? CM_AUTO : CM_MANUAL));
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
    g_pipeline_action_landed = false;
    g_pipeline_morale_extra = false;
    g_auto_state.spell_wait_slot = -1;
    g_auto_state.spell_wait_key = -1;
    g_auto_state.spell_wait_frames = 0;
    g_auto_state.spell_wait_started = 0;
    g_auto_state.spell_mana_before = 0;
    g_auto_state.spell_casted_before = 0;
    // F9 全手动是用户对"本场谁执行"的显式选择；取消重打/重绑不清，
    // 否则用户切到全手动后保存设置，面板关闭即被静默切回自动。
    // 只清单次接管与施法等待等运行时数据。
    g_enemy_hp_turn[0] = -1;       // 自动停止的最近两笔取样作废
    g_enemy_hp_turn[1] = -1;
    g_enemy_hp_value[0] = 0;
    g_enemy_hp_value[1] = 0;
    g_summon_locked_spell = -1;    // 召唤元素锁定只在本场内有效
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
    // 保活方式字段在 AutoStackRule 内，随槽位规则整体重排，无单独处理。

    ++g_battle_attempt_id;
    ResetAutoState();
    BindStackTrackingFromBattle_();
    LogInfo("[Life] retry rebound attempt=%d side=%d with stable identity remap",
        g_battle_attempt_id, side);
}

// ======================================================================
// 战斗内容指纹（§17）。指纹定义见 PolicyCore ComputeBattleFingerprint：
// 重打 / S&L / 重启后同一场战斗同值。存储与按档恢复见 ConfigLog
// 战斗存档库与 SettingsDlg 存档下拉框；这里只负责「何时算」。
// ======================================================================
static unsigned long long g_battle_fp = 0;
static bool g_battle_fp_valid = false;

// 战斗触发点（被攻击方冒险坐标，z 区分地上/地下）推导：
// 1) 守方英雄格（守方有英雄时直接可用，攻守英雄对撞/守城皆准）；
// 2) 人类英雄的**计划目的地** dest 与其当前位置相邻（切比雪夫 ≤1 同层）
//    → dest=被攻击对象格：主动打野怪/攻城/访问触发战斗都成立，且同一
//    目标从不同方向攻击 dest 相同（不用攻击发起格——方向无关才是稳定键）；
// 3) 兜底人类英雄格：被野怪/敌英雄主动撞击时 dest 是残留的旧目的地
//    （不与英雄相邻），战斗触发点=英雄自身格。
// 重打 / S&L / 重启后三种路径都回到同一格 → 指纹稳定。
// （官方 H3CombatManager::mapitem @0x53BC 存战斗发生格对象，但其
// GetCoordinates 是 H3API.dll 导入，本项目裸结构取不到坐标，弃用。）
static void ResolveBattleMapCoord_(_BattleMgr_* mgr, int side,
    int* out_x, int* out_y, int* out_z)
{
    *out_x = *out_y = *out_z = 0;
    if (!mgr) return;
    if (mgr->hero[1]) {
        *out_x = mgr->hero[1]->x;
        *out_y = mgr->hero[1]->y;
        *out_z = mgr->hero[1]->z;
        return;
    }
    _Hero_* human = (side == 0 || side == 1) ? mgr->hero[side] : nullptr;
    if (!human) return;
    const int dx = human->dest_x - human->x;
    const int dy = human->dest_y - human->y;
    const int dz = human->dest_z - human->z;
    if (dz == 0 && dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1
        && (dx != 0 || dy != 0)) {
        *out_x = human->dest_x;
        *out_y = human->dest_y;
        *out_z = human->dest_z;
        return;
    }
    *out_x = human->x;
    *out_y = human->y;
    *out_z = human->z;
}

// 新战斗 UI 出现（BE_BATTLE_UI_APPEARED）：算指纹 + 空态时查库自动恢复。
// 重打走 BE_RESULT_RETRY 边不进这里——指纹与内存方案都不动。
void OnBattleAppearedFingerprint_()
{
    _BattleMgr_* mgr = o_BattleMgr;
    H3AutoPolicy::BattleFingerprintInput in = {};
    if (mgr) {
        for (int s = 0; s < 2; ++s) {
            in.hero_id[s] = mgr->hero[s] ? mgr->hero[s]->id : -1;
            for (int i = 0; i < 21; ++i) {
                in.side_types[s][i] = mgr->stack[s][i].creature_id;
                in.side_counts[s][i] = mgr->stack[s][i].count_at_start;
            }
        }
        in.terrain = mgr->land_type;
        in.siege_kind = mgr->siege_kind;
        ResolveBattleMapCoord_(mgr, ResolveHumanSide_(mgr),
            &in.map_x, &in.map_y, &in.map_z);
    }
    g_battle_fp = H3AutoPolicy::ComputeBattleFingerprint(in);
    g_battle_fp_valid = mgr != nullptr;
    LogInfo("[Battle] 指纹=0x%016llX 触发点=(%d,%d,%d) 地形=%d 攻城=%d 英雄=(%d,%d)",
        g_battle_fp, in.map_x, in.map_y, in.map_z, in.terrain, in.siege_kind,
        in.hero_id[0], in.hero_id[1]);
}

// 当前战斗指纹（SettingsDlg 存档下拉框用）；未算得返回 0。
unsigned long long GetBattleFingerprint()
{
    return g_battle_fp_valid ? g_battle_fp : 0;
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
    LogDebug("[Spell] request digit=%d slot=%d spell=%d targetHex=%d targetStack=%p flags=0x%X side=%d human=%d tactics=%d hero=%p casted=%d mana=%d action=%d",
        digit, slot, spell_id, target_hex, target_stack, spell_flags,
        side, is_human, tactics,
        (side >= 0 && side <= 1) ? mgr->hero[side] : nullptr,
        GetHeroCasted_(mgr, side), GetHeroMana_(mgr, side), mgr->action);

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
        LogDebug("[Spell] posted quick key digit=%d h3vk=%d spell=%d hwnd=%p down=%d up=%d",
            digit, h3vk, spell_id, hwnd, down_ok ? 1 : 0, up_ok ? 1 : 0);
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

// ==== 保活复活（§3.1.1） ====
// 每次把控制权交给玩家时判一次：遍历己方部队（按队保活方式，默认不保活），
// 每队按自己的保活方式（剩余数量 ≤ 阈值 / 损失量大于恢复量）判定够格，
// 够格者中取血量最低一支，按其亡灵/活体自动选聚灵(39)/复活(38)，
// 直接 CastSpell 施放。英雄本回合已施法跳过；不占快捷施法、
// 不推进任何部队的循环施法游标。复活无人可救且召唤启用（enabled）
// 时走召唤兜底（SummonChannel_）。

// 设置面板提交后调用：当前/下一回合重新判定（事件型判定无需作废标记，
// 保留空函数供提交路径调用，日志指明配置已生效）。
void SyncActiveProtect()
{
    LogInfo("[Protect] settings committed; next control hand-in re-checks");
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

// ==== 召唤通道（保活复活的兜底，§2.3）====
// 事件型判定：每次控制权交玩家都判（无回合标记）；英雄已施法则跳过
// （TryProtectCast_ 共享守卫已查）。法力消耗与召唤量常量表为暂定口径，
// 上机逆向确认后修订（§5 清单项）。
static const int kSummonManaCostProvisional = 15; // SoD 四系召唤法力消耗（暂定）

// 召唤量（个）= 力量 × 等级系数（基础 2 / 高级 3 / 专家 4；暂定，§5）。
// 只影响自动模式的元素排序，不参与判定。
static int SummonCountAmount_(int expertise, int spell_power)
{
    if (expertise <= 0 || spell_power <= 0) return 0;
    const int mult = expertise >= 3 ? 4 : expertise == 2 ? 3 : 2;
    return spell_power * mult;
}

// 己方存活统计喂纯函数：存活队数 + 血量合计。
// 队数口径 = 存活的本体部队（军队槽 0..6）+ 存活的召唤物（四系元素，
// 开战时不存在、count_at_start 常为 0，不能用它过滤），不含战争机器与克隆。
// 身份口径与 MakeStableStackIdentity 一致：source_army_slot 0..6 = 本体，
// 其余非战争机器非克隆 = 召唤物。
static void OwnSideForceStats_(_BattleMgr_* mgr, int side,
    int* out_alive, int* out_hp)
{
    if (out_alive) *out_alive = 0;
    if (out_hp) *out_hp = 0;
    if (!mgr || side < 0 || side > 1) return;
    H3AutoPolicy::TargetCandidate alive[21] = {};
    int n = 0;
    for (int i = 0; i < 21; ++i) {
        _BattleStack_* st = &mgr->stack[side][i];
        if (st->count_current <= 0) continue;
        if (!CreatureInfoIndexValid_(st->creature_id)) continue;
        if (H3AutoPolicy::IsWarMachineType(st->creature_id)) continue;
        if (st->clone_id > 0) continue;                 // 克隆/镜像不计入
        const bool army = st->source_army_slot >= 0
            && st->source_army_slot < 7;
        const bool summon = !army
            && H3AutoPolicy::IsSummonedElemental(st->creature_id);
        if (!army && !summon) continue;
        H3AutoPolicy::TargetCandidate& c = alive[n++];
        c.count_current = st->count_current;
        c.count_at_start = (st->count_at_start > 0)
            ? st->count_at_start : st->count_current;
        c.hit_points = st->creature.hit_points;
        c.lost_hp = st->lost_hp;
        LogDebug("[Summon] stack slot=%d cid=0x%X src=%d clone=%d cnt=%d start=%d %s",
            i, st->creature_id, st->source_army_slot, st->clone_id,
            st->count_current, st->count_at_start, summon ? "summon" : "army");
    }
    if (out_alive) *out_alive = H3AutoPolicy::CountAliveSideStacks(alive, n);
    if (out_hp) *out_hp = H3AutoPolicy::SumSideRemainingHp(alive, n);
}

static bool SummonChannel_(_BattleMgr_* mgr, int side,
    H3CombatManager* cm, H3Hero* hero, int spell_power)
{
    const int profile = g_active_profile;
    if (profile < 0 || profile >= 5) return false;
    const SummonProfileFields& sf = g_summon[profile];

    int alive = 0, hp_total = 0;
    OwnSideForceStats_(mgr, side, &alive, &hp_total);

    // 已学法术与召唤量（元素下标 0..3 = 气/水/火/土）。
    int amounts[H3AutoPolicy::SUMMON_ELEMENT_COUNT] = {};
    bool learned[H3AutoPolicy::SUMMON_ELEMENT_COUNT] = {};
    for (int i = 0; i < H3AutoPolicy::SUMMON_ELEMENT_COUNT; ++i) {
        const int exp = hero->GetSpellExpertise(
            H3AutoPolicy::kSummonSpellIds[i], cm->specialTerrain);
        learned[i] = exp > 0;
        amounts[i] = SummonCountAmount_(exp, spell_power);
    }
    const int pick = H3AutoPolicy::PickSummonSpell(
        amounts, learned, sf.spell_pick, g_summon_locked_spell);
    LogDebug("[Summon] check alive=%d/th%d hp=%d/th%d pick_cfg=%d pick=%d locked=%d mana=%d",
        alive, sf.count_th, hp_total, sf.hp_th, sf.spell_pick, pick,
        g_summon_locked_spell, GetHeroMana_(mgr, side));
    if (pick < 0) {
        LogDebug("[Summon] no available spell (cfg=%d learned=%d%d%d%d)",
            sf.spell_pick, learned[0] ? 1 : 0, learned[1] ? 1 : 0,
            learned[2] ? 1 : 0, learned[3] ? 1 : 0);
        return false;
    }
    const int spell_id = H3AutoPolicy::kSummonSpellIds[pick];
    const int expertise = hero->GetSpellExpertise(spell_id, cm->specialTerrain);
    if (!H3AutoPolicy::SummonShouldCast(alive, sf.count_th, hp_total,
            sf.hp_th, sf.cond_combine, GetHeroMana_(mgr, side),
            kSummonManaCostProvisional,
            GetHeroCasted_(mgr, side) != 0, expertise > 0))
        return false;   // 时机不满足：静默跳过，不记失败（§0）

    // 召唤锚定格：己方第一支存活部队所在格。召唤法术 target hex
    // 实参语义待上机验证（§5），先按保活通道同参对照原版施放。
    int anchor_hex = -1;
    for (int i = 0; i < 21 && anchor_hex < 0; ++i) {
        _BattleStack_* st = &mgr->stack[side][i];
        if (st->count_current > 0)
            anchor_hex = StackHex_(st);
    }
    if (anchor_hex < 0) anchor_hex = 0;

    const int mana_before = GetHeroMana_(mgr, side);
    const int casted_before = GetHeroCasted_(mgr, side);
    __try {
        LogDebug("[Summon] cast spell=%d hex=%d exp=%d power=%d alive=%d/%d hp=%d/%d",
            spell_id, anchor_hex, expertise, spell_power,
            alive, sf.count_th, hp_total, sf.hp_th);
        cm->CastSpell(spell_id, anchor_hex, 0, -1, expertise, spell_power);
    } __except (1) {
        LogWarn("[Summon] cast exception code=0x%08X spell=%d",
            GetExceptionCode(), spell_id);
        return false;
    }
    // 成功（法力扣减或施法标志翻转）才记锁定；槽位占满等静默失败不记。
    const bool cast_ok =
        GetHeroMana_(mgr, side) < mana_before
        || (casted_before == 0 && GetHeroCasted_(mgr, side) != 0);
    if (!cast_ok) {
        LogWarn("[Summon] cast not taken effect spell=%d mana=%d->%d casted=%d",
            spell_id, mana_before, GetHeroMana_(mgr, side),
            GetHeroCasted_(mgr, side));
        return false;
    }
    if (sf.spell_pick == 0 && g_summon_locked_spell < 0)
        g_summon_locked_spell = pick;   // 自动模式：首次成功即本场锁定
    LogInfo("[Summon] 召唤成功 spell=%d 元素下标=%d 锚格=%d (方案%d 队数<%d 血量≤%d)",
        spell_id, pick, anchor_hex, profile + 1, sf.count_th, sf.hp_th);
    return true;
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

    // —— 第二停止条件：敌方英雄魔力 ≤ 阈值（方案级，默认 6，0..32767）——
    // 勾选 stop_enemy_mana 且敌方有英雄、有魔法书才判；与停止回合数
    // OR 组合，任一触发即切手动。无书英雄魔力常为 0，不判书会开战即误停。
    // 每次控制权交还玩家都判（与停止回合数同评估点，不受回合取样约束）。
    if (g_summon[profile].stop_enemy_mana) {
        int mana_th = g_summon[profile].stop_mana_th;
        if (mana_th < 0) mana_th = 0;
        if (mana_th > 32767) mana_th = 32767;
        const int side = ResolveHumanSide_(mgr);
        if (side >= 0 && side <= 1 && mgr->hero[1 - side]) {
            bool has_book = false;
            __try {
                // 魔法书 = 0 号宝物（DoesWearArtifact @0x4E2C90，Compat 自带）。
                has_book = mgr->hero[1 - side]->DoesWearArtifact(0) != 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                has_book = false;
            }
            const int enemy_mana = GetHeroMana_(mgr, 1 - side);
            LogDebug("[AutoStop] enemy mana check book=%d mana=%d th=%d",
                has_book ? 1 : 0, enemy_mana, mana_th);
            if (H3AutoPolicy::ShouldStopOnEnemyMana(1, true, has_book,
                    enemy_mana, mana_th)) {
                // 走 SetControlMode_ 而非裸赋值：裸赋值不打日志，
                // 「自动打一回合就停」时无法从日志定位是谁切的模式。
                SetControlMode_(CM_MANUAL);
                ClearOneShotManual_();
                LogInfo("[AutoStop] enemy mana low: 敌方魔力 %d ≤ %d，切回手动",
                    enemy_mana, mana_th);
                RefreshControlStatusHint_();
                return;
            }
        }
    }

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
    LogDebug("[AutoStop] sample turn=%d hp=%d prev=(%d,%d) threshold=%d",
        turn, hp, g_enemy_hp_turn[0], g_enemy_hp_value[0], threshold);
    if (g_enemy_hp_turn[0] < 0) return;

    const int elapsed = g_enemy_hp_turn[1] - g_enemy_hp_turn[0];
    const int left = H3AutoPolicy::ProjectEnemyTurnsLeft(
        threshold, g_enemy_hp_value[0], hp, elapsed);
    LogDebug("[AutoStop] judge elapsed=%d base=%d cur=%d left=%d",
        elapsed, g_enemy_hp_value[0], hp, left);
    if (left >= 0 && left <= threshold) {
        // 走 SetControlMode_ 而非裸赋值（同上：模式切换必须留日志）。
        SetControlMode_(CM_MANUAL);
        ClearOneShotManual_();
        LogInfo("[Auto] 自动停止：最近 %d 回合敌方血量 %d→%d，预计还需 %d（阈值 %d）",
            elapsed, g_enemy_hp_value[0], hp, left, threshold);
        RefreshControlStatusHint_();
        return;
    }
}

// 守卫（S5）：仅战斗中·面板关 + 自动模式下判（设计文档 §3.1.1：
// 全手动、单次接管时不判、不施——原实现缺此守卫，本次修正）。
// 保活与召唤是同一条施法通道（§3.1.1/§2.3）：保活优先——每次行动
// （还有施法次数时）先按各队 ProtectMode 判复活；无人可救且召唤已启用
// （g_summon[p].enabled）再走召唤兜底。一回合只施一次法，两边天然互斥。
static bool TryMaintainStatus_(_BattleMgr_* mgr, int side,
    H3CombatManager* cm, H3Hero* hero, int spell_power);

static bool TryProtectCast_(_BattleMgr_* mgr)
{
    if (g_phase != BP_COMBAT_CLOSED) return false;
    if (g_control != CM_AUTO) return false; // AUTO 才判保活/施法（§3.1.1）

    const int turn = GetCurrentBattleTurn_(mgr);
    if (turn < 0) return false;

    const int side = ResolveHumanSide_(mgr);
    if (side < 0 || side > 1) return false;
    if (GetHeroCasted_(mgr, side)) return false;

    H3CombatManager* cm = H3CombatManager::Get();
    if (!cm) return false;
    // 魔法通道只在玩家方有英雄时启用。无英雄不能施法，保活、补状态和召唤都不判。
    H3Hero* hero = reinterpret_cast<H3Hero*>(mgr->hero[side]);
    if (!hero) return false;
    const int spell_power = cm->heroSpellPower[side];

    // 复活固定耗魔 10（SoD，不随等级变化）；召唤法术耗魔由 SummonChannel_
    // 自查。法力 <10 时复活必不能施，但先收集候选再统一判（日志完整）。
    const int mana = GetHeroMana_(mgr, side);

    // 收集己方部队（按队保活方式，选「不保活」的队永不入选；战争机器与召唤物克隆槽不参与）
    // 按各队自己的方式判定。够格者中统一取血量最低（全灭者剩余 0 天然最前）。
    H3AutoPolicy::TargetCandidate cands[21] = {};
    int cand_slot[21] = {}, cand_spell[21] = {}, cand_exp[21] = {};
    int cand_restore[21] = {};
    int cand_count = 0;
    for (int slot = 0; slot < 21; ++slot) {
        const AutoStackRule& rule = g_active_rules[slot];

        const StackTrackEntry& te = g_stack_track[slot];
        if (!te.bound || te.side != side) continue;
        _BattleStack_* st = &mgr->stack[side][slot];
        if (!st) continue;
        const int baseline = ProtectBaselineCount_(mgr, side, st);
        if (baseline <= 0) continue;
        if (!CreatureInfoIndexValid_(st->creature_id)) continue;
        if (H3AutoPolicy::IsWarMachineType(st->creature_id)) continue;
        const bool dead = st->count_current <= 0;
        if (dead && StackHex_(st) < 0) continue;          // 没有可施法的尸体格

        // 损失口径与急救"失血数值"一致：死亡数×满血 + 顶层已损。
        // 死亡数从英雄战前军队数量算，不用 F9 重打后或按确定时的数量。
        H3AutoPolicy::TargetCandidate cand = {};
        cand.count_current = st->count_current;
        cand.count_at_start = baseline;
        cand.hit_points     = st->creature.hit_points;
        cand.lost_hp        = st->lost_hp;
        const int wound = H3AutoPolicy::WoundValue(cand);
        // 队列槽位快照（debug）：还原「该救不救」判定的完整输入。
        LogDebug("[Protect] queue slot=%d mode=%d cnt=%d start=%d origin=%d wound=%d dead=%d rem=%d th=%d",
            slot, (int)rule.protectMode, st->count_current, st->count_at_start, baseline,
            wound, dead ? 1 : 0, H3AutoPolicy::StackRemainingHp(cand),
            rule.protectCountBelow);

        // 亡灵→聚灵(39)，活体→复活(38)；按英雄当前等级算可恢复量。
        const int spell_id = P_CreatureInformation[st->creature_id].undead
            ? 39 : 38;
        const int expertise = hero->GetSpellExpertise(spell_id, cm->specialTerrain);
        if (expertise <= 0) {
            LogDebug("[Protect] skip no-expertise slot=%d spell=%d",
                slot, spell_id);
            continue;                                   // 没学该法术
        }
        int spell_base = -1;
        int spell_effect = -1;
        __try {
            BYTE* spell_table = *reinterpret_cast<BYTE**>(0x687FA8);
            if (spell_table && spell_id >= 0 && spell_id < 81) {
                BYTE* spell = spell_table + spell_id * 0x88;
                spell_effect = *reinterpret_cast<int*>(spell + 0x30);
                spell_base = *reinterpret_cast<int*>(spell + 0x34
                    + expertise * 4);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        int restorable = (spell_base >= 0 && spell_effect >= 0)
            ? H3AutoPolicy::ResurrectionRestoreHp(
                spell_base, spell_effect, expertise, spell_power)
            : H3AutoPolicy::ResurrectionRestoreHp(expertise, spell_power);
        // 复活与聚灵都要加英雄法术特长（0x4E6260），无特长返回 0。
        // 等级取全局生物表 +4，战场内嵌生物信息从血量起、不含等级。
        int creature_level = 0;
        if (CreatureInfoIndexValid_(st->creature_id))
            creature_level = P_CreatureInformation[st->creature_id].level;
        int specialty = 0;
        __try {
            specialty = mgr->hero[side]->GetSpell_Specialisation_Bonuses(
                spell_id, creature_level, restorable);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (specialty > 0 && restorable <= 0x7FFFFFFF - specialty)
            restorable += specialty;

        if (!H3AutoPolicy::ProtectShouldCast(
                (H3AutoPolicy::ProtectMode)rule.protectMode,
                restorable, wound,
                st->count_current, rule.protectCountBelow))
            continue;
        cands[cand_count] = cand;
        cand_slot[cand_count] = slot;
        cand_spell[cand_count] = spell_id;
        cand_exp[cand_count] = expertise;
        cand_restore[cand_count] = restorable;
        ++cand_count;
    }
    const int picked = H3AutoPolicy::SelectProtectTargetIndex(cands, cand_count);
    if (picked < 0) {
        LogDebug("[Protect] no qualified target turn=%d cands=%d summon=%d",
            turn, cand_count, g_summon[g_active_profile].enabled ? 1 : 0);
        // 复活无人可救 → 召唤兜底（启用时才判；法力/已学/时机自查）。
        if (TryMaintainStatus_(mgr, side, cm, hero, spell_power))
            return true;
        if (!g_summon[g_active_profile].enabled) return false;
        return SummonChannel_(mgr, side, cm, hero, spell_power);
    }
    if (mana < 10) return false;                       // 有人该救但法力不够
    // 候选明细（debug）：定位“该救不救/救错对象”类问题。
    for (int i = 0; i < cand_count; ++i)
        LogDebug("[Protect] cand[%d/%d] slot=%d spell=%d exp=%d restore=%d wound=%d rem=%d dead=%d cnt=%d th=%d",
            i, cand_count, cand_slot[i], cand_spell[i], cand_exp[i],
            cand_restore[i],
            H3AutoPolicy::WoundValue(cands[i]),
            H3AutoPolicy::StackRemainingHp(cands[i]),
            cands[i].count_current <= 0 ? 1 : 0,
            cands[i].count_current,
            g_active_rules[cand_slot[i]].protectCountBelow);

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
        LogDebug("[Protect] cast spell=%d slot=%d hex=%d exp=%d power=%d rem=%d wound=%d",
            best_spell, best_slot, StackHex_(st), best_exp, spell_power,
            best_remaining, best_wound);
        cm->CastSpell(best_spell, StackHex_(st), 0, -1, best_exp, spell_power);
    } __except (1) {
        LogDebug("[Protect] cast exception code=0x%08X spell=%d slot=%d",
            GetExceptionCode(), best_spell, best_slot);
        return false;
    }
    return true;
}


static bool TryMaintainStatus_(_BattleMgr_* mgr, int side,
    H3CombatManager* cm, H3Hero* hero, int spell_power)
{
    if (!mgr || !cm || !hero || side < 0 || side > 1) return false;
    const int enemy_side = side ^ 1;
    auto can_receive = [](_BattleStack_* st, int spell) -> bool {
        if (!st || spell <= 0) return false;
        BOOL8 ok = 0;
        __try {
            ok = FASTCALL_2(BOOL8, 0x4477A0, spell, st);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return ok != 0;
    };
    const StatusProfileFields& status = g_status[g_active_profile];
    int durations[H3AutoPolicy::kStatusSlotCapacity] = {};
    int best_slot = -1;
    int best_index = -1;
    int best_duration = 99;
    for (int slot = 0; slot < 21; ++slot) {
        _BattleStack_* st = &mgr->stack[side][slot];
        if (!st || st->count_current <= 0) continue;
        for (int i = 0; i < status.slot_count; ++i) {
            const int spell = status.slots[i];
            // 减速打敌方，不拿己方部队的持续时间参与增益选择。
            durations[i] = (spell > 0 && spell < 81
                    && spell != H3AutoPolicy::kSlowSpellId
                    && can_receive(st, spell))
                ? st->active_spell_duration[spell] : -1;
        }
        const int index = H3AutoPolicy::ChooseBuffToRefresh(
            durations, status.slot_count);
        if (index >= 0 && durations[index] < best_duration) {
            best_duration = durations[index];
            best_index = index;
            best_slot = slot;
        }
    }
    int spell_id = best_index >= 0 ? status.slots[best_index] : -1;
    (void)best_slot;
    if (spell_id < 0) {
        bool want_slow = false;
        for (int i = 0; i < status.slot_count; ++i)
            want_slow = want_slow || status.slots[i] == H3AutoPolicy::kSlowSpellId;
        int expertise = 0;
        __try {
            expertise = hero->GetSpellExpertise(H3AutoPolicy::kSlowSpellId,
                cm->specialTerrain);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        int enemy_durations[21] = {};
        int enemy_hexes[21] = {};
        for (int slot = 0; slot < 21; ++slot) {
            _BattleStack_* st = &mgr->stack[enemy_side][slot];
            enemy_durations[slot] = (want_slow && st && st->count_current > 0
                    && can_receive(st, H3AutoPolicy::kSlowSpellId))
                ? st->active_spell_duration[H3AutoPolicy::kSlowSpellId] : -1;
            enemy_hexes[slot] = 0;
        }
        const H3AutoPolicy::StatusMaintainChoice slow =
            H3AutoPolicy::ChooseSlowTarget(
                expertise >= 3, enemy_durations, enemy_hexes, 21);
        spell_id = slow.spell_id;
        if (expertise < 3) return false;
    }
    // 保持状态列表里的法术都是全体魔法，不指定目标格。
    if (spell_id < 0) return false;
    int expertise = 0;
    __try {
        expertise = hero->GetSpellExpertise(spell_id, cm->specialTerrain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (expertise <= 0) return false;
    __try {
        cm->CastSpell(spell_id, -1, 1, -1, expertise, spell_power);
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
// 召唤通道参数（启用/阈值/法术选择/召唤物共享规则/敌方法力停）随方案一起提交，
// 共享规则在此再过一次 NormalizeSummonRule（草稿来源不可信原则）。
void CommitProfiles(int active_profile, AutoStackRule rules[5][21],
    const uint16_t stop_turns[5],
    const SummonProfileFields summon[5],
    const StatusProfileFields status[5], bool* out_store_added)
{
    if (out_store_added) *out_store_added = false;
    if (active_profile < 0 || active_profile >= 5)
        active_profile = 0;
    memcpy(g_profiles, rules, sizeof(g_profiles));
    for (int p = 0; p < 5; ++p) {
        int turns = stop_turns ? stop_turns[p] : H3AutoPolicy::DEFAULT_STOP_TURNS;
        if (turns < 0) turns = 0;
        if (turns > 999) turns = 999;
        g_stop_turns[p] = static_cast<uint16_t>(turns);
        SummonProfileFields fields = summon
            ? summon[p] : H3AutoPolicy::MakeDefaultSummonFields();
        fields.enabled = fields.enabled ? 1 : 0;
        if (fields.count_th < 2) fields.count_th = 2; // 最小即默认 2
        if (fields.count_th > 21) fields.count_th = 21;
        if (fields.hp_th < 0) fields.hp_th = 0;
        if (fields.spell_pick < 0
            || fields.spell_pick > H3AutoPolicy::SUMMON_ELEMENT_COUNT)
            fields.spell_pick = 0;
        fields.cond_combine =
            (fields.cond_combine == H3AutoPolicy::SUMMON_COMBINE_OR)
            ? H3AutoPolicy::SUMMON_COMBINE_OR
            : H3AutoPolicy::SUMMON_COMBINE_AND;
        fields.stop_enemy_mana = fields.stop_enemy_mana ? 1 : 0;
        if (fields.stop_mana_th < 0) fields.stop_mana_th = 0;
        if (fields.stop_mana_th > 32767) fields.stop_mana_th = 32767;
        H3AutoPolicy::NormalizeSummonRule(&fields.summon_rule);
        g_summon[p] = fields;
        StatusProfileFields st = status
            ? status[p] : H3AutoPolicy::MakeDefaultStatusFields();
        if (st.slot_count < 0) st.slot_count = 0;
        if (st.slot_count > H3AutoPolicy::kStatusSlotCapacity)
            st.slot_count = H3AutoPolicy::kStatusSlotCapacity;
        st.slow = st.slow ? 1 : 0;
        g_status[p] = st;
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
    // 生效方案召唤/停止参数留痕（测试核对：默认勾选+阈值 6+召唤物默认防御）。
    {
        const SummonProfileFields& sf = g_summon[g_active_profile];
        LogInfo("[Auto] active cfg: stop_turns=%d "
            "mana_stop=%d mana_th=%d summon(on=%d spell=%d combine=%d "
            "count<%d hp<=%d act=%d)",
            (int)g_stop_turns[g_active_profile],
            (int)sf.stop_enemy_mana, sf.stop_mana_th,
            (int)sf.enabled, (int)sf.spell_pick, sf.cond_combine,
            sf.count_th, sf.hp_th, (int)sf.summon_rule.action);
    }
    // 提交后立即绑定本场部队身份；后续执行依赖跟踪校验。
    BindStackTrackingFromBattle_();
    // 智能存档（§17）：每次「确定」把 5 套方案追加进本场战斗存档库
    //（<指纹>.json）；与最后一条内容相同则不新增；每场保留最近 30 条。
    if (g_battle_fp_valid) {
        bool skipped = false;
        if (AppendBattleStoreRecord(g_battle_fp,
                (const AutoStackRule(*)[21])g_profiles,
                g_stop_turns, g_summon, g_status, g_active_profile, &skipped)) {
            LogInfo("[BattleStore] %s（active=%d）",
                skipped ? "内容未变，不新增存档" : "已存档", g_active_profile + 1);
            if (!skipped && out_store_added) *out_store_added = true;
            PruneBattleStore();
        } else {
            LogDebug("[BattleStore] 存档失败（文件写入失败）");
        }
    }
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
    // 远程按生物类型固有标志，不用当前弹药（弹药打光 shots 归零，但类型不变）。
    c.ranged = P_CreatureInformation[t->creature_id].shooter ? 1 : 0;
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
    if (count <= 0) {
        LogDebug("[Target] no candidates selector=%d side=%d wounded=%d",
            (int)rule.target.selector, side_filter, require_wounded ? 1 : 0);
        return nullptr;
    }

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
    // 候选明细与选中结果（debug）：定位“选错目标/不选目标”类问题。
    for (int i = 0; i < count; ++i)
        LogDebug("[Target] cand[%d/%d] side=%d slot=%d cid=0x%X cnt=%d/%d hp=%d rem=%d spd=%d shots=%d ranged=%d flyer=%d wound=%d",
            i, count, candidates[i]->def_group_ix, candidates[i]->army_slot_ix,
            candidates[i]->creature_id, candidates[i]->count_current,
            candidates[i]->count_at_start, scored[i].hit_points,
            H3AutoPolicy::StackRemainingHp(scored[i]), scored[i].speed,
            scored[i].shots, scored[i].ranged, scored[i].flyer,
            H3AutoPolicy::WoundValue(scored[i]));
    LogDebug("[Target] select action=%d selector=%d -> slot=%d cid=0x%X hex=%d",
        (int)rule.action, (int)rule.target.selector,
        candidates[selected]->army_slot_ix, candidates[selected]->creature_id,
        StackHex_(candidates[selected]));
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
    g_pipeline_action_landed = true; // 命令已落地（士气再行动判据）
    g_pipeline_landed_turn = GetCurrentBattleTurn_(mgr);
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
    g_pipeline_action_landed = true; // 命令已落地（士气再行动判据）
    g_pipeline_landed_turn = GetCurrentBattleTurn_(mgr);
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

// 召唤物移动：散开（分级拉开）与随机移动（共享规则专属动作）。
// 可达格用原版悬停判定（0x475DC0）逐格核对，与移动/近战同口径；
// 散开两档无解 → 原地防御（降级链固定终于防御，与勾选无关）；
// 随机移动无可达格 → 交调用方按「允许降级为防御」处理。
// 缓冲 static 化：游戏回调线程栈小（技能条目：大缓冲不进栈）。
static bool SubmitSummonMove_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule)
{
    if (!mgr || !self) return false;

    static int cands[H3AutoPolicy::BATTLEFIELD_HEXES];
    int n = 0;
    for (int hex = 1; hex <= 185
        && n < H3AutoPolicy::BATTLEFIELD_HEXES; ++hex) {
        if (IsMoveTargetReachable_(mgr, self, hex))
            cands[n++] = hex;
    }
    // 己方其它存活部队位置（含战争机器；宽体只记头格，距离近似）。
    static int own[H3AutoPolicy::BATTLEFIELD_HEXES];
    int own_n = 0;
    for (int i = 0; i < 21; ++i) {
        _BattleStack_* s = &mgr->stack[self->def_group_ix][i];
        if (s == self || s->count_current <= 0 || s->count_at_start <= 0)
            continue;
        const int h = StackHex_(s);
        if (h >= 0 && own_n < H3AutoPolicy::BATTLEFIELD_HEXES)
            own[own_n++] = h;
    }
    const int cur = StackHex_(self);
    const int target = H3AutoPolicy::ChooseSummonMoveHex(
        rule.action, cands, n, cur, own, own_n,
        static_cast<uint32_t>(GetTickCount()));
    if (target < 0) {
        if (rule.action == AA_SCATTER)
            return SubmitDefend_(mgr, self);
        return false;   // 随机移动：无可达格 → 上层降级/交回玩家
    }
    if (!WriteAction_(mgr, self, BA_WALK, -1, target))
        return false;
    LogDebug("[Summon] submit %s slot=%d -> hex=%d cands=%d",
        rule.action == AA_SCATTER ? "SCATTER" : "RANDOM_MOVE",
        self->army_slot_ix, target, n);
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
    const AutoStackRule& rule, StackTrackEntry& runtime, int repeat_pair = -1)
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
    // 士气额外行动：重打指定组合（本回合刚执行过的），不推进游标。
    const bool repeat = !legacy && repeat_pair >= 0 && repeat_pair < count;
    if (repeat) cursor = repeat_pair;

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
    // 士气重打不推进：循环作用于回合，下回合继续原顺序。
    if (!legacy && count > 0 && !repeat)
        runtime.melee_cursor = (cursor + 1) % count;
    LogDebug("[Auto] submit MELEE(loop%s) pair=%d/%d stand=%d attack=%d enemy_slot=%d enemy_hex=%d next=%d",
        repeat ? ",repeat" : "", cursor, count, stand_hex, attack_hex,
        enemy->army_slot_ix, StackHex_(enemy),
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

    // —— 士气高涨的额外行动：循环语义作用于回合，不作用于同回合的额外行动 ——
    // 循环移动：不动（原地防御）；路径游标已在本回合正常推进，下回合走下一点。
    // 循环近战：重打本回合刚执行过的组合，游标不往前推进（下回合继续原顺序）。
    // 其余动作（远程/急救/防御/召唤物走位）无回合游标，自然再执行一次。
    const bool morale_extra = g_pipeline_morale_extra
        && g_pipeline_morale_turn == GetCurrentBattleTurn_(mgr);
    if (morale_extra && rule.action == AA_MOVE) {
        LogInfo("[Auto] morale extra: MOVE -> DEFEND slot=%d",
            self->army_slot_ix);
        return SubmitDefend_(mgr, self);
    }
    int melee_repeat_pair = -1;
    if (morale_extra && rule.action == AA_MELEE_ATTACK
        && rule.target.meleePairCount > 0) {
        int pair_count = rule.target.meleePairCount;
        if (pair_count > MELEE_PAIR_CAPACITY) pair_count = MELEE_PAIR_CAPACITY;
        // 游标在本回合成功提交后已 +1；取回退一格 = 本回合刚打过的组合。
        melee_repeat_pair = (runtime.melee_cursor - 1 + pair_count) % pair_count;
        LogInfo("[Auto] morale extra: MELEE repeat pair=%d/%d slot=%d",
            melee_repeat_pair, pair_count, self->army_slot_ix);
    }

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
        return fallback_or_fail(
            SubmitMelee_(mgr, self, rule, runtime, melee_repeat_pair));
    case AA_RANGED_ATTACK:
        return fallback_or_fail(SubmitRanged_(mgr, self, rule));
    case AA_FIRST_AID:
        if (cid != WM_FIRST_AID) return false;
        return SubmitFirstAid_(mgr, self, rule); // 帐篷不降级
    case AA_SCATTER:
    case AA_RANDOM_MOVE:
        // 召唤共享规则专属动作（普通部队下拉不出这两个值；
        // NormalizeSummonRule 保证只有召唤规则能存进来）。
        return fallback_or_fail(SubmitSummonMove_(mgr, self, rule));
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
    if (mgr->action != 0) {
        LogDebug("[Auto] 提交跳过：已有动作 action=%d", mgr->action);
        return false;
    }
    UpdateStackTracking_();
    // 用当前轮到的部队而不是 active_stack：后者在自动模式下不被游戏刷新
    // （见 CurrentTurnStack_ 注释），只剩一队时会一直指向空/上一支。
    _BattleStack_* self = CurrentTurnStack_(mgr);
    if (!self) {
        LogDebug("[Auto] 提交跳过：无当前部队 turn=%d/%d",
            mgr->current_mon_side, mgr->current_mon_index);
        return false;
    }

    // 活动单位变化：旧单位的管线残留整体清掉（等待/完成/已处理）。
    if (g_pipeline_stack && g_pipeline_stack != self) {
        if (g_pipeline_stage == PS_SPELL_POSTED)
            ClearSpellWait_();
        g_pipeline_stage = PS_IDLE;
        g_pipeline_stack = nullptr;
        g_pipeline_action_landed = false;
        g_pipeline_morale_extra = false;
    }
    if (g_auto_state.action_wake_stack
        && g_auto_state.action_wake_stack != self)
        g_auto_state.action_wake_stack = nullptr;
    if (g_pipeline_stage == PS_HANDLED && g_pipeline_stack == self) {
        static void* s_skip_logged = nullptr;
        if (s_skip_logged != self) {
            s_skip_logged = self;
            LogDebug("[Auto] 提交跳过：本部队已处理 slot=%d", self->army_slot_ix);
        }
        return false; // 本单位已处理完
    }

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
    // 分发快照：定位“该动不动/走错分支”类问题（排查期 info，确认后降 debug）。
    LogDebug("[Auto] dispatch slot=%d cid=0x%X action=%d selector=%d fallback=%d spells=%d cursor=%d key=%d stage=%d",
        idx, self->creature_id, (int)rule.action, (int)rule.target.selector,
        rule.allowDefendFallback ? 1 : 0, rule.spellSlotCount,
        runtime.spell_cursor, spell_key, (int)g_pipeline_stage);

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

// 当前轮到行动的部队。游戏自己判定「该不该把控制权交给玩家」
// （FUN_004744d0）用的就是 current_mon_side/current_mon_index
// （0x132B8/0x132BC），active_stack（0x132C8）要等 FUN_004773f0 才从它
// 复制过去，而那个复制只在「判定为交给玩家」之后发生。自动模式下游戏
// 不走那条路，active_stack 会停在空或上一支部队——只剩一队时表现为
// 打完一回合就再也不动。所以接管与提交一律以这两个下标为准。
static _BattleStack_* CurrentTurnStack_(_BattleMgr_* mgr)
{
    if (!mgr) return nullptr;
    // active_stack 是游戏正在高亮、等待输入的那支（FUN_004773f0 在「交给
    // 玩家」时设置）。它有效时最可信，优先用它。
    if (mgr->active_stack && mgr->active_stack->count_current > 0)
        return mgr->active_stack;
    // 自动模式下游戏不走「交给玩家」，active_stack 会空着或停在上一支。
    // 退回 current_mon_side/index——这是游戏自己判定轮到谁时用的下标。
    const int side = mgr->current_mon_side;
    const int idx = mgr->current_mon_index;
    if (side < 0 || side > 1 || idx < 0 || idx >= 21) return nullptr;
    _BattleStack_* s = &mgr->stack[side][idx];
    if (s->count_current <= 0) return nullptr;
    return s;
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

    _BattleStack_* stack = CurrentTurnStack_(mgr);
    if (!stack) return CD_KEEP_ORIGINAL;

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
        // info 留痕：判定为「插件执行」后，确认消息入口确实被调用、
        // 以及提交结果。排查「decision=2 之后再无动作」用，确认后可降级。
        const bool submitted = TrySubmitConfiguredAction_(mgr, allow_unit_action);
        // 每帧都进，按结果去重，只在状态变化时记一行。
        static int s_entry_sig = -1;
        const int sig = (allow_unit_action ? 1 : 0) | (submitted ? 2 : 0)
            | (mgr->action << 8);
        if (s_entry_sig != sig) {
            s_entry_sig = sig;
            LogDebug("[Auto] 执行入口 allow=%d submitted=%d action=%d",
                allow_unit_action ? 1 : 0, submitted ? 1 : 0, mgr->action);
        }
        return submitted;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogInfo("[Auto] 执行入口异常 code=0x%08X", GetExceptionCode());
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
    if (orig != 0) {
        // 游戏 API 判定不把控制权交给玩家（自带“自动战斗”开关开着、
        // 蛊惑、或 AI 侧行动）。debug 留痕：区分“钩子没被游戏调”与
        // “orig!=0 被游戏拒绝”，定位“按 F9 切 AUTO 后无接管”类问题。
        LogDebug("[Takeover] orig=%d game-keeps-control control=%d phase=%d",
            orig, (int)g_control, (int)g_phase);
        return orig;            // 非“交给玩家”路径：不介入
    }
    __try {
        PollControlHotkeys_(This);
        // 每次行动（控制权交玩家）都判保活（事件型，无回合标记）：
        // 复活按各队方式（剩余数量≤/损失量大于恢复量），召唤按阈值，
        // 已施法/无法施法时内部静默跳过。仅 orig==0 路径判。
        TryProtectCast_(This);
        TryAutoStop_(This);
        // 管线复位不依赖 active_stack：自动模式下游戏不走「交给玩家」，
        // active_stack 会一直空着（日志 active=-1/-1），放在它的判空里
        // 复位就永远不发生——场上只剩一支部队时，打完一回合管线停在
        // PS_HANDLED，之后每帧都「本部队已处理」直接跳过。
        // 回合号前进 = 新回合，无条件复位。
        if (This && g_pipeline_stage == PS_HANDLED && g_pipeline_action_landed
            && This->action == 0) {
            const int turn_now = GetCurrentBattleTurn_(This);
            if (turn_now >= 0 && g_pipeline_landed_turn >= 0
                && turn_now != g_pipeline_landed_turn) {
                LogDebug("[Auto] 新回合复位管线 turn %d->%d",
                    g_pipeline_landed_turn, turn_now);
                g_pipeline_stage = PS_IDLE;
                g_pipeline_action_landed = false;
                g_pipeline_morale_extra = false;
            } else {
                // 每帧都会进这里，按回合去重，否则日志每秒上千行。
                static int s_hold_logged_turn = -2;
                if (s_hold_logged_turn != turn_now) {
                    s_hold_logged_turn = turn_now;
                    LogDebug("[Auto] 管线保持已处理 turn_now=%d landed_turn=%d action=%d",
                        turn_now, g_pipeline_landed_turn, This->action);
                }
            }
        }
        if (This && This->active_stack) {
            // 活动单位变化：旧单位管线残留整体清（等待/完成/已处理）。
            if (g_pipeline_stack && g_pipeline_stack != This->active_stack) {
                if (g_pipeline_stage == PS_SPELL_POSTED)
                    ClearSpellWait_();
                g_pipeline_stage = PS_IDLE;
                g_pipeline_stack = nullptr;
                g_pipeline_action_landed = false;
                g_pipeline_morale_extra = false;
            } else if (g_pipeline_stage == PS_HANDLED
                && g_pipeline_stack == This->active_stack
                && g_pipeline_action_landed && This->action == 0) {
                // 同支部队消化完上一条命令后再次获得行动机会。本判定点
                // 只在真正的行动时机发生（战斗动画期间不被调用），且要求
                // 命令已落地、action 已被执行器清回 0——即这是全新的一次
                // 行动机会，而非执行窗口内的重复询问。
                // 回合号相同 = 士气高涨的额外行动（带 g_pipeline_morale_extra
                // 标记，循环移动/近战按「循环作用于回合」特殊处理）；
                // 回合号前进 = 新回合（极端情况：场上只剩一支部队，
                // 回合背靠背、无其它部队插入触发不了单位变化复位）。
                // 快捷施法由 hero_casted 挡住，士气额外行动不会二施。
                const int turn = GetCurrentBattleTurn_(This);
                const bool morale_extra =
                    turn >= 0 && g_pipeline_landed_turn == turn;
                g_pipeline_stage = PS_IDLE;
                g_pipeline_action_landed = false;
                g_pipeline_morale_extra = morale_extra;
                g_pipeline_morale_turn = turn;
                if (morale_extra)
                    LogInfo("[Auto] morale re-action: reset pipeline slot=%d cid=0x%X",
                        This->active_stack->army_slot_ix,
                        This->active_stack->creature_id);
                else
                    LogDebug("[Auto] same stack new turn: reset pipeline slot=%d turn=%d",
                        This->active_stack->army_slot_ix, turn);
            }
        }

        const int decision = DecideTakeover(This);
        // 判定快照（debug）：decision 2=插件执行 1=交AI 0=保持原版。
        // turn=当前轮到的部队（current_mon_side/index），active=active_stack。
        // 两者不一致就是「只剩一队不动」的特征：游戏不刷新 active_stack。
        {
            _BattleStack_* ds = CurrentTurnStack_(This);
            _BattleStack_* as = This->active_stack;
            const int dslot = ds ? ds->army_slot_ix : -1;
            LogDebug("[Takeover] decision=%d turn=%d/%d cid=0x%X cnt=%d active=%d/%d rule_action=%d control=%d",
                decision, This->current_mon_side, This->current_mon_index,
                ds ? ds->creature_id : -1, ds ? ds->count_current : -1,
                as ? as->def_group_ix : -1, as ? as->army_slot_ix : -1,
                (dslot >= 0 && dslot < 21)
                    ? (int)g_active_rules[dslot].action : -1,
                (int)g_control);
        }
        if (decision == CD_HAND_TO_AI)
            return 1;           // 仅战争机器特殊：交回 AI
        // CD_EXECUTE_H3AUTO / CD_KEEP_ORIGINAL：返回 0（控制权在玩家路径）。
        // 若需代发动作，在 Hook_BattleMsgProc 入口提交。
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogDebug("[Auto] 行动判定发生异常 code=0x%08X", GetExceptionCode());
    }
    return 0;
}
