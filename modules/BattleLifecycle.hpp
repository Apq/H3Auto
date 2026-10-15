// BattleLifecycle.hpp - 战斗生命周期事件源的对外声明。
// 实现在 BattleLifecycle.inc.cpp（H3Auto.cpp 中排在 BattleState.inc.cpp 之后）。
// 该层是状态机 S1-S8 的产生侧（BattleUI 出现/消失兜底、开面板热键消费、
// 结果窗意图观察），不依赖任何界面实现——开面板经 IH3AutoUi 契约。
#pragma once

// 每帧由 Entry 的 BltComplete 外壳调用（先于面板帧/自动化判定）。
void CheckAutoFightDialogClosed();
