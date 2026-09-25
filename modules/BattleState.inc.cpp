// BattleState.inc.cpp - 战场阶段状态机实现（设计文档 §15）。
// 本文件在 H3Auto.cpp 中排在 AutoExecute.inc.cpp 之后：PhaseEdgeAction_
// 调用 AutoExecute 的内部函数（ClearOneShotManual_/SetControlMode_/
// ClearSpellWait_/g_control/g_auto_state 等），同翻译单元内先定义后使用。

#include "BattleState.hpp"

static void WriteLog(const char* fmt, ...);
extern void ClearConfirmedProfiles();
extern void ResetAutoState();
extern bool IsPanelActive();
extern void CloseSettingsPanel();

BattlePhase g_phase = BP_PEACE;
DWORD g_ui_gone_grace_until = 0;

static void PhaseEdgeAction_(BattlePhase from, BattleEvent ev);

static const char* PhaseName_(BattlePhase p)
{
    switch (p) {
    case BP_PEACE:         return "PEACE";
    case BP_COMBAT_CLOSED: return "COMBAT_CLOSED";
    case BP_COMBAT_OPEN:   return "COMBAT_OPEN";
    case BP_RESULT:        return "RESULT";
    case BP_ENDED:         return "ENDED";
    default:               return "?";
    }
}

static const char* EventName_(BattleEvent e)
{
    switch (e) {
    case BE_BATTLE_UI_APPEARED:   return "UI_APPEARED";
    case BE_PANEL_OPEN_REQUESTED: return "PANEL_OPEN";
    case BE_PANEL_COMMIT:         return "PANEL_COMMIT";
    case BE_PANEL_CANCEL:         return "PANEL_CANCEL";
    case BE_RESULT_SHOWN:         return "RESULT_SHOWN";
    case BE_RESULT_RETRY:         return "RESULT_RETRY";
    case BE_RESULT_ACCEPTED:      return "RESULT_ACCEPTED";
    case BE_BATTLE_UI_GONE:       return "UI_GONE";
    default:                      return "?";
    }
}

void SetPhase_(BattlePhase next, BattleEvent ev)
{
    if (next == g_phase) return; // 幂等：同状态重复事件不算迁移
    struct PhaseEdge { BattlePhase from, to; BattleEvent ev; };
    static const PhaseEdge legal[] = {
        { BP_PEACE,         BP_COMBAT_CLOSED, BE_BATTLE_UI_APPEARED },
        { BP_PEACE,         BP_RESULT,        BE_RESULT_SHOWN },   // 快速战斗：未经战斗 UI 直接结算
        { BP_COMBAT_CLOSED, BP_COMBAT_OPEN,   BE_PANEL_OPEN_REQUESTED },
        { BP_COMBAT_OPEN,   BP_COMBAT_CLOSED, BE_PANEL_COMMIT },
        { BP_COMBAT_OPEN,   BP_COMBAT_CLOSED, BE_PANEL_CANCEL },
        { BP_COMBAT_CLOSED, BP_RESULT,        BE_RESULT_SHOWN },
        { BP_COMBAT_OPEN,   BP_RESULT,        BE_RESULT_SHOWN },
        { BP_RESULT,        BP_COMBAT_CLOSED, BE_RESULT_RETRY },
        { BP_RESULT,        BP_ENDED,         BE_RESULT_ACCEPTED },
        { BP_COMBAT_CLOSED, BP_ENDED,         BE_BATTLE_UI_GONE },
        { BP_COMBAT_OPEN,   BP_ENDED,         BE_BATTLE_UI_GONE },
        { BP_RESULT,        BP_ENDED,         BE_BATTLE_UI_GONE },  // 结算中读档/退出
        { BP_ENDED,         BP_PEACE,         BE_RESULT_ACCEPTED }, // 瞬态续转
        { BP_ENDED,         BP_PEACE,         BE_BATTLE_UI_GONE },  // 瞬态续转
    };
    bool ok = false;
    for (int i = 0; i < (int)(sizeof(legal) / sizeof(legal[0])); ++i) {
        if (legal[i].from == g_phase && legal[i].to == next
            && legal[i].ev == ev) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        WriteLog("[Phase] illegal %s -> %s (ev=%s)",
            PhaseName_(g_phase), PhaseName_(next), EventName_(ev));
        return;
    }

    WriteLog("[Phase] %s -> %s (ev=%s)",
        PhaseName_(g_phase), PhaseName_(next), EventName_(ev));
    PhaseEdgeAction_(g_phase, ev);
    g_phase = next;

    // ENDED 是瞬态：清理已做，立即续转回 PEACE（合法表含同事件的续转边）。
    if (g_phase == BP_ENDED)
        SetPhase_(BP_PEACE, ev);
}

// 状态机边动作（§15）。在合法迁移确认后、g_phase 赋值前执行；
// from=迁移前状态。ENDED→PEACE 瞬态续转边不执行任何动作——清理已在
// 进入 ENDED 的主边做过，重复执行依赖边动作幂等是侥幸而非结构保证。
static void PhaseEdgeAction_(BattlePhase from, BattleEvent ev)
{
    if (from == BP_ENDED)
        return; // 续转边：无动作

    if (ev == BE_PANEL_OPEN_REQUESTED) {
        // 打开面板=强制停自动执行（§15.3：面板开时不得存在在跑的子流程）。
        // 切停走与 F9 相同的清理；已停则不动。
        if (g_control != CM_MANUAL) {
            ClearOneShotManual_();
            SetControlMode_(CM_MANUAL);
            WriteLog("[Control] 面板打开：切换为全手动");
        }
        // GetTickCount 相对超时挂在面板关闭后会瞬间误超时，清空而非冻结。
        ClearSpellWait_();
        return;
    }

    if (ev == BE_RESULT_SHOWN) {
        // 面板开时战斗推进到结算（如敌方清场）：静默关面板，草稿丢弃。
        if (IsPanelActive()) {
            WriteLog("[Phase] 结算出现：静默关闭设置面板");
            CloseSettingsPanel();
        }
        return;
    }

    if (ev == BE_RESULT_RETRY) {
        // 取消/重打：重排身份+重绑+清运行时（EnsureStackTrackingBound 内含）；
        // CM 保留——全手动是玩家显式选择，重打不清（设计文档 §10.1）。
        g_ui_gone_grace_until = GetTickCount() + 3000;
        EnsureStackTrackingBound();
        return;
    }

    if (ev == BE_RESULT_ACCEPTED || ev == BE_BATTLE_UI_GONE) {
        // 战斗终了（ENDED→PEACE 续转边不重复）：钩子捕获的待消费热键全部丢弃，
        // PollControlHotkeys_ 在 PEACE 不运行，不清会横跨两场战斗。
        g_auto_state.kb_toggle_seen = false;
        g_auto_state.kb_oneshot_seen = false;
        g_auto_state.kb_open_panel_seen = false;
    }

    if (ev == BE_RESULT_ACCEPTED) {
        // 接受：清方案+清运行时+跟踪（OnBattleResultAccepted 内含）；
        // 决策③：跨场不残留全手动，下一场恢复自动。
        ClearConfirmedProfiles();
        ResetAutoState();
        SetControlMode_(CM_AUTO);
        return;
    }

    if (ev == BE_BATTLE_UI_GONE) {
        // 兜底（读档/中途退出）：清运行时+跟踪+面板（ResetAutoState 内含静默关面板）；
        // 决策②：5 套方案保留；CM 重置同接受。
        ResetAutoState();
        SetControlMode_(CM_AUTO);
        return;
    }
}

// 玩家点结果窗「确定/接受」：清空 5 套方案 + 运行时状态。
// 「取消/重打」不得调用本函数。
// 重构后由状态机 BE_RESULT_ACCEPTED 边动作取代（PhaseEdgeAction_），保留外壳兼容。
void OnBattleResultAccepted()
{
    ClearConfirmedProfiles();
    ResetAutoState();
    WriteLog("[Life] battle result accepted: profiles+runtime cleared");
}
