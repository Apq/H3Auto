// Production MagicChannelInputCore is exercised here (no duplicated UI policy).
#include "../modules/MagicChannelInputCore.hpp"
static void TestMagicChannelInput() {
    using namespace H3AutoPolicy;
    Check(MagicChannelVisibleRows(344, 448, 26) == 4, "attack rows fit design viewport");
    Check(MagicChannelVisibleRows(374, 448, 32) == 2, "large font reduces visible rows");
    Check(MagicChannelVisibleRows(448, 448, 26) == 0, "empty viewport has no rows");
    Check(ClampMagicChannelFirst(999, 9, 4) == 5, "attack first clamps to final plus row");
    Check(ClampMagicChannelFirst(-1, 0, 4) == 0, "empty scroll clamps to zero");
    Check(MagicChannelVisibleIndex(5, 3, 9) == 8, "last plus row maps absolute index");
    Check(MagicChannelVisibleIndex(5, 4, 9) == -1, "out of list is not an item");
    MagicChannelGesture gesture = {};
    Check(!FinishMagicChannelGesture(&gesture, 1, 0, -1), "release without down cannot append");
    BeginMagicChannelGesture(&gesture, 1, 3, -1);
    Check(FinishMagicChannelGesture(&gesture, 1, 3, -1), "matching click appends once");
    Check(!FinishMagicChannelGesture(&gesture, 1, 3, -1), "second release cannot append again");
    BeginMagicChannelGesture(&gesture, 1, 3, -1);
    CancelMagicChannelGesture(&gesture);
    Check(gesture.held == 1 && gesture.cancelled, "scroll keeps ownership of delayed release");
    Check(!FinishMagicChannelGesture(&gesture, 1, 3, -1), "scroll cancels add activation");
    BeginMagicChannelGesture(&gesture, 2, 1, 4);
    Check(!FinishMagicChannelGesture(&gesture, 2, 1, 5), "drag to another option cannot select");
    BeginMagicChannelGesture(&gesture, 2, 1, 4);
    CancelMagicChannelGesture(&gesture);
    Check(!FinishMagicChannelGesture(&gesture, 2, 1, 4), "scrolled option cannot select on release");
    BeginMagicChannelGesture(&gesture, 3, -1, -1);
    Check(!FinishMagicChannelGesture(&gesture, 1, 3, -1), "overlay close release cannot activate plus");
    BeginMagicChannelGesture(&gesture, 1, 3, -1);
    CancelMagicChannelGesture(&gesture);
    BeginMagicChannelGesture(&gesture, 1, 2, -1);
    Check(FinishMagicChannelGesture(&gesture, 1, 2, -1), "new down clears cancelled gesture");
}
