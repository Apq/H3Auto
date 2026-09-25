// BattleState.hpp - 战场阶段状态机的类型与对外声明（设计文档 §15）。
// 实现在 BattleState.inc.cpp（H3Auto.cpp 中排在 AutoExecute.inc.cpp 之后，
// 便于边动作调用 AutoExecute 内部函数）。
#pragma once

enum BattlePhase {
    BP_PEACE,          // 战斗前
    BP_COMBAT_CLOSED,  // 战斗中·面板关
    BP_COMBAT_OPEN,    // 战斗中·面板开
    BP_RESULT,         // 战斗结算
    BP_ENDED,          // 战斗结束（瞬态：进入即清理后续转 BP_PEACE）
};

enum BattleEvent {
    BE_BATTLE_UI_APPEARED,   // 战斗 UI 出现（BltComplete 每帧扫描）
    BE_PANEL_OPEN_REQUESTED, // 右键自动战斗 / P 键（面板打开成功后）
    BE_PANEL_COMMIT,         // 面板「确定」
    BE_PANEL_CANCEL,         // 面板「取消」/ESC
    BE_RESULT_SHOWN,         // CPResult 结果窗出现
    BE_RESULT_RETRY,         // 结算→取消/重打
    BE_RESULT_ACCEPTED,      // 结算→接受
    BE_BATTLE_UI_GONE,       // 战斗 UI 消失且未经结算（兜底）
};

extern BattlePhase g_phase;
// 兜底边（BE_BATTLE_UI_GONE）宽限：重打销毁重建战斗 UI 常超 3 帧，
// RETRY 边置 3 秒宽限，期间 UI 消失不视为「未经结算消失」（重构步骤 S4.2）。
extern DWORD g_ui_gone_grace_until;

// 唯一迁移出口：合法边查表 + 边动作 + 日志；非法组合记日志保持原状。
void SetPhase_(BattlePhase next, BattleEvent ev);
