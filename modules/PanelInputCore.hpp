#pragma once

namespace H3AutoPolicy {

// Compare owned pointers before reading a tracked item: dialog addresses can be reused.
template<typename Item>
inline bool PanelOwnsTrackedItem(const void* current_owner, const void* tracked_owner,
    Item* const* items, unsigned count, const Item* tracked_item)
{
    if (!current_owner || current_owner != tracked_owner || !items
        || !tracked_item || count > 65536)
        return false;
    for (unsigned i = 0; i < count; ++i) {
        if (items[i] == tracked_item)
            return true;
    }
    return false;
}

} // namespace H3AutoPolicy
