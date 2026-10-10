#pragma once

namespace H3AutoPolicy {

// Down/up ownership for a native overlay: scrolling cancels activation but keeps
// ownership of the eventual release. The same policy is used by production UI.
struct MagicChannelGesture {
    int held; // 0 none, 1 add, 2 dropdown option, 3 close overlay
    int row;
    int option;
    bool cancelled;
};
inline void BeginMagicChannelGesture(MagicChannelGesture* state, int held, int row, int option) {
    if (!state) return;
    state->held = held; state->row = row; state->option = option; state->cancelled = false;
}
inline void CancelMagicChannelGesture(MagicChannelGesture* state) {
    if (state && state->held) state->cancelled = true;
}
inline bool FinishMagicChannelGesture(MagicChannelGesture* state, int held, int row, int option) {
    if (!state) return false;
    const bool activate = !state->cancelled && state->held == held
        && state->row == row && state->option == option;
    *state = {};
    return activate;
}
inline int MagicChannelVisibleRows(int top, int bottom, int row_height) {
    if (row_height <= 0 || bottom <= top) return 0;
    return (bottom - top) / row_height;
}
inline int ClampMagicChannelFirst(int first, int total, int visible) {
    int last = total > visible ? total - visible : 0;
    if (first < 0) return 0;
    return first > last ? last : first;
}
inline int MagicChannelVisibleIndex(int first, int visible_row, int total) {
    if (first < 0 || visible_row < 0 || first >= total || visible_row >= total - first) return -1;
    return first + visible_row;
}
} // namespace H3AutoPolicy
