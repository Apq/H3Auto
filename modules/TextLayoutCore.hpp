#pragma once

namespace H3AutoPolicy {

struct TextVerticalRect {
    int y;
    int height;
};

// Preserve top/center/bottom anchoring while admitting at least one font line.
inline TextVerticalRect FitTextVerticalRect(int y, int height, int font_height,
    unsigned alignment)
{
    if (height <= 0 || font_height <= height) return { y, height };
    if (alignment & 4u) y += (height - font_height) / 2;
    else if (alignment & 8u) y += height - font_height;
    return { y, font_height };
}

inline int FontAwareRowHeight(int design_height, int font_height, int padding = 0)
{
    const int minimum = font_height > 0 ? font_height + padding : design_height;
    return design_height > minimum ? design_height : minimum;
}

inline int MetadataPortraitHeight(int card_height, int label_height,
    int portrait_top, int portrait_height)
{
    const int available = card_height - 4 - 2 * label_height - 1 - portrait_top - 2;
    if (available <= 0) return 0;
    return available < portrait_height ? available : portrait_height;
}

inline int VisibleTextRows(int available_height, int row_height, int count)
{
    if (available_height <= 0 || row_height <= 0 || count <= 0) return 0;
    const int capacity = available_height / row_height;
    return count < capacity ? count : capacity;
}

} // namespace H3AutoPolicy
