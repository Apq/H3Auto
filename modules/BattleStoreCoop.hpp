#pragma once

// 动作前协作顺序的共用实现；回调签名/查找归宿主，测试不依赖游戏或patcher。
// 初始忙不得消费恢复世代或调用保存，回调失败/回调导致忙不得继续自动效果。
template<class Busy, class Restore, class Before>
bool BattleStoreAutomationAllowed_(Busy busy, Restore restore, Before before)
{
    if (busy()) return false;
    restore();
    if (!before()) return false;
    return !busy();
}
