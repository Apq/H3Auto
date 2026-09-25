// PanelGfx.inc.cpp - 面板图像原语与 PCX 资源层。
// 不依赖面板状态 s_p：Fill/DrawTxt/RGB 转换/PCX24 加载缓存/
// 离屏合成与后缓冲输出。在 H3Auto.cpp 中排在 SettingsDlg.inc.cpp 之前，
// 供 SettingsDlg 与 PanelDraw 共用。
#include "PanelLayout.hpp"

#define o_WndMgr (*reinterpret_cast<H3WindowManager**>(0x6992D0))
#define o_DDSurfaceBackBuffer (*reinterpret_cast<LPDIRECTDRAWSURFACE*>(0x6AAD28))

static void WriteLog(const char* fmt, ...);

// 与 H3BattleValueInfo 远程对比框相同：先离屏合成，再一次性写入 backbuffer。
static H3LoadedPcx16* s_panel_composite = nullptr;
static H3LoadedPcx16* s_panel_background = nullptr;
static H3LoadedPcx16* s_panel_cell = nullptr;
static H3LoadedPcx16* s_panel_ok_normal = nullptr;
static H3LoadedPcx16* s_panel_ok_pressed = nullptr;
static H3LoadedPcx16* s_panel_cancel_normal = nullptr;
static H3LoadedPcx16* s_panel_cancel_pressed = nullptr;
static H3LoadedPcx16* s_panel_button_frame = nullptr;
static H3LoadedPcx16* s_panel_grid_frame = nullptr;
static bool s_panel_background_load_failed = false;
static bool s_panel_cell_load_failed = false;
static bool s_panel_grid_frame_load_failed = false;
static bool s_panel_ok_normal_load_failed = false;
static bool s_panel_ok_pressed_load_failed = false;
static bool s_panel_cancel_normal_load_failed = false;
static bool s_panel_cancel_pressed_load_failed = false;
static bool s_panel_button_frame_load_failed = false;

static void Fill(H3LoadedPcx16* scr, int x, int y, int w, int h, int r, int g, int b)
{
    if (w <= 0 || h <= 0) return;
    scr->FillRectangle(x, y, w, h, (BYTE)r, (BYTE)g, (BYTE)b);
}

static void DrawTxt(H3LoadedPcx16* scr, H3Font* fnt, const char* text,
    int x, int y, int w, int h, INT32 color,
    eTextAlignment align = eTextAlignment::MIDDLE_CENTER)
{
    if (!fnt || !text || w <= 0 || h <= 0) return;

    // The project uses UTF-8 source files, while the Chinese game font expects
    // GBK byte sequences. ASCII can be passed through unchanged.
    bool ascii = true;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p >= 0x80) { ascii = false; break; }
    }
    if (ascii) {
        scr->TextDraw(fnt, text, x, y, w, h, (eTextColor)color, align);
        return;
    }

    wchar_t wide[256] = {};
    char gbk[512] = {};
    const int wide_len = MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, _countof(wide));
    if (wide_len > 0
        && WideCharToMultiByte(936, 0, wide, -1, gbk, sizeof(gbk), nullptr, nullptr) > 0)
    {
        scr->TextDraw(fnt, gbk, x, y, w, h, (eTextColor)color, align);
    } else {
        scr->TextDraw(fnt, text, x, y, w, h, (eTextColor)color, align);
    }
}

static WORD PanelRGB888To565_(int r, int g, int b)
{
    return (WORD)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

static DWORD PanelRGB565To8888_(WORD color)
{
    const int r = ((color >> 11) & 0x1F) << 3;
    const int g = ((color >> 5) & 0x3F) << 2;
    const int b = (color & 0x1F) << 3;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static WORD PanelRGB8888To565_(DWORD color)
{
    return PanelRGB888To565_((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

static H3LoadedPcx16* LoadPanelPcx24_(const char* asset_name, int expected_width,
    int expected_height, H3LoadedPcx16*& cache, bool& load_failed,
    bool allow_shorter_height)
{
    if (cache || load_failed)
        return cache;

    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_hModule, path, _countof(path));
    char* slash = strrchr(path, '\\');
    if (!slash) {
        load_failed = true;
        return nullptr;
    }
    const size_t remaining = _countof(path) - static_cast<size_t>(slash + 1 - path);
    strcpy_s(slash + 1, remaining, "img\\");
    strcat_s(path, asset_name);

    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) {
        WriteLog("[Panel] PCX 资源加载失败：%s", path);
        load_failed = true;
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size < 128) {
        fclose(file);
        load_failed = true;
        return nullptr;
    }

    BYTE* encoded = static_cast<BYTE*>(malloc(file_size));
    if (!encoded || fread(encoded, 1, file_size, file) != static_cast<size_t>(file_size)) {
        if (encoded) free(encoded);
        fclose(file);
        load_failed = true;
        return nullptr;
    }
    fclose(file);

    const int bits_per_plane = encoded[3];
    const int xmin = *reinterpret_cast<WORD*>(encoded + 4);
    const int ymin = *reinterpret_cast<WORD*>(encoded + 6);
    const int xmax = *reinterpret_cast<WORD*>(encoded + 8);
    const int ymax = *reinterpret_cast<WORD*>(encoded + 10);
    const int plane_count = encoded[65];
    const int bytes_per_line = *reinterpret_cast<WORD*>(encoded + 66);
    const int width = xmax - xmin + 1;
    const int height = ymax - ymin + 1;

    if (encoded[0] != 0x0A || encoded[2] != 1 || bits_per_plane != 8
        || plane_count != 3 || width != expected_width
        || (height != expected_height
            && !(allow_shorter_height && height < expected_height))
        || bytes_per_line < width)
    {
        WriteLog("[Panel] %s 格式不符 w=%d h=%d bpp=%d planes=%d bpl=%d。",
            asset_name, width, height, bits_per_plane, plane_count, bytes_per_line);
        free(encoded);
        load_failed = true;
        return nullptr;
    }

    const size_t raw_size = static_cast<size_t>(bytes_per_line) * plane_count * height;
    BYTE* raw = static_cast<BYTE*>(malloc(raw_size));
    if (!raw) {
        free(encoded);
        load_failed = true;
        return nullptr;
    }

    size_t source_pos = 128;
    size_t output_pos = 0;
    while (output_pos < raw_size && source_pos < static_cast<size_t>(file_size)) {
        const BYTE marker = encoded[source_pos++];
        if ((marker & 0xC0) == 0xC0) {
            const int count = marker & 0x3F;
            if (source_pos >= static_cast<size_t>(file_size)) break;
            const BYTE value = encoded[source_pos++];
            for (int i = 0; i < count && output_pos < raw_size; ++i)
                raw[output_pos++] = value;
        } else {
            raw[output_pos++] = marker;
        }
    }
    free(encoded);

    if (output_pos != raw_size) {
        WriteLog("[Panel] %s 解码不完整 decoded=%u expected=%u。",
            asset_name, static_cast<unsigned>(output_pos), static_cast<unsigned>(raw_size));
        free(raw);
        load_failed = true;
        return nullptr;
    }

    cache = H3LoadedPcx16::Create(width, height);
    if (!cache || !cache->buffer) {
        if (cache) cache->Destroy();
        cache = nullptr;
        free(raw);
        load_failed = true;
        return nullptr;
    }

    const bool output_32_bit = H3BitMode::Get() == 4;
    for (int y = 0; y < height; ++y) {
        const BYTE* planes = raw + static_cast<size_t>(y) * bytes_per_line * plane_count;
        const BYTE* red = planes;
        const BYTE* green = planes + bytes_per_line;
        const BYTE* blue = planes + bytes_per_line * 2;
        BYTE* row = cache->buffer + y * cache->scanlineSize;
        if (output_32_bit) {
            DWORD* pixels = reinterpret_cast<DWORD*>(row);
            for (int x = 0; x < width; ++x)
                pixels[x] = 0xFF000000u | (red[x] << 16) | (green[x] << 8) | blue[x];
        } else {
            WORD* pixels = reinterpret_cast<WORD*>(row);
            for (int x = 0; x < width; ++x)
                pixels[x] = PanelRGB888To565_(red[x], green[x], blue[x]);
        }
    }
    free(raw);
    return cache;
}

static H3LoadedPcx16* LoadPanelBackground_()
{
    return LoadPanelPcx24_("HA_bg.pcx", PANEL_W, PANEL_H,
        s_panel_background, s_panel_background_load_failed, false);
}

static H3LoadedPcx16* LoadPanelCell_()
{
    return LoadPanelPcx24_("HA_cell.pcx", CELL_W, CELL_H,
        s_panel_cell, s_panel_cell_load_failed, false);
}

static H3LoadedPcx16* LoadPanelGridFrame_()
{
    return LoadPanelPcx24_("HA_grid_frame.pcx", GRID_FRAME_W, GRID_FRAME_H,
        s_panel_grid_frame, s_panel_grid_frame_load_failed, false);
}

static bool CopyPanelBackground_(H3LoadedPcx16* destination)
{
    H3LoadedPcx16* background = LoadPanelBackground_();
    if (!background || !destination || !background->buffer || !destination->buffer)
        return false;

    const int row_bytes = background->scanlineSize < destination->scanlineSize
        ? background->scanlineSize : destination->scanlineSize;
    const int copy_h = background->height < PANEL_H
        ? background->height : PANEL_H;
    for (int y = 0; y < copy_h; ++y) {
        memcpy(destination->buffer + y * destination->scanlineSize,
            background->buffer + y * background->scanlineSize, row_bytes);
    }
    return true;
}

static void DrawPanelCell_(H3LoadedPcx16* destination, int dst_x, int dst_y)
{
    H3LoadedPcx16* cell = LoadPanelCell_();
    if (!cell || !cell->buffer || !destination || !destination->buffer)
        return;

    // HA_cell.pcx 现在是统一金色 2px 边框 + 精确青色键内部，
    // 与其它边框资源一致，直接用精确青色键判定即可
    // （32-bit 0x0000FFFF / 16-bit 0x7FDF），不再需要宽松的 red-dominant 判定。
    const bool mode_32_bit = H3BitMode::Get() == 4;
    for (int y = 0; y < CELL_H; ++y) {
        BYTE* dst_row = destination->buffer + (dst_y + y) * destination->scanlineSize;
        const BYTE* src_row = cell->buffer + y * cell->scanlineSize;
        if (mode_32_bit) {
            DWORD* dst = reinterpret_cast<DWORD*>(dst_row) + dst_x;
            const DWORD* src = reinterpret_cast<const DWORD*>(src_row);
            for (int x = 0; x < CELL_W; ++x) {
                const DWORD color = src[x];
                if ((color & 0x00FFFFFFu) == 0x0000FFFFu) continue; // 精确青色键
                dst[x] = color;
            }
        } else {
            WORD* dst = reinterpret_cast<WORD*>(dst_row) + dst_x;
            const WORD* src = reinterpret_cast<const WORD*>(src_row);
            for (int x = 0; x < CELL_W; ++x) {
                const WORD color = src[x];
                if (color == 0x7FDF) continue; // 精确青色键
                dst[x] = color;
            }
        }
    }
}

static void DrawPanelTriangle_(H3LoadedPcx16* destination, int center_x, int top,
    bool points_down, int red, int green, int blue)
{
    for (int row = 0; row < 5; ++row) {
        const int half_width = points_down ? 4 - row : row;
        const int y = top + row;
        Fill(destination, center_x - half_width, y,
            half_width * 2 + 1, 1, red, green, blue);
    }
}

static void DrawTransparentPcx_(H3LoadedPcx16* source,
    H3LoadedPcx16* destination, int dst_x, int dst_y)
{
    if (!source || !source->buffer || !destination || !destination->buffer) return;
    const bool mode_32_bit = H3BitMode::Get() == 4;
    // 格子缓冲区是我们自己用精确清屏色（16-bit 0x7FDF / 32-bit 0xFF00FFFF）
    // 清空的，所以合成时只跳过这个精确值，不能用金框那套宽松的
    // green>red||blue>red 判定，否则图标/文字里的冷色像素会被误抠。
    for (int y = 0; y < source->height; ++y) {
        const BYTE* src_row = source->buffer + y * source->scanlineSize;
        BYTE* dst_row = destination->buffer + (dst_y + y) * destination->scanlineSize;
        for (int x = 0; x < source->width; ++x) {
            if (mode_32_bit) {
                const DWORD color = reinterpret_cast<const DWORD*>(src_row)[x];
                if ((color & 0x00FFFFFFu) == 0x0000FFFFu) continue; // 精确青色键
                reinterpret_cast<DWORD*>(dst_row)[dst_x + x] = color;
            } else {
                const WORD color = reinterpret_cast<const WORD*>(src_row)[x];
                if (color == 0x7FDF) continue; // 精确青色键
                reinterpret_cast<WORD*>(dst_row)[dst_x + x] = color;
            }
        }
    }
}

// 不透明区块拷贝：把 source 的一个矩形原样拷到 destination（不做任何键色跳过）。
// 用于图标区——TwCrPort 头像自带实心背景，冷色像素若走 DrawTransparentPcx_
// 的 green>red||blue>red 判定会被误当透明抠掉，透出面板底图。这里直接不透明贴。
static void BlitOpaqueRegion_(H3LoadedPcx16* source, H3LoadedPcx16* destination,
    int src_x, int src_y, int w, int h, int dst_x, int dst_y)
{
    if (!source || !source->buffer || !destination || !destination->buffer) return;
    const bool mode_32_bit = H3BitMode::Get() == 4;
    for (int y = 0; y < h; ++y) {
        const int sy = src_y + y;
        const int dy = dst_y + y;
        if (sy < 0 || sy >= source->height) continue;
        if (dy < 0 || dy >= destination->height) continue;
        const BYTE* src_row = source->buffer + sy * source->scanlineSize;
        BYTE* dst_row = destination->buffer + dy * destination->scanlineSize;
        for (int x = 0; x < w; ++x) {
            const int sx = src_x + x;
            const int dx = dst_x + x;
            if (sx < 0 || sx >= source->width) continue;
            if (dx < 0 || dx >= destination->width) continue;
            if (mode_32_bit) {
                reinterpret_cast<DWORD*>(dst_row)[dx] =
                    reinterpret_cast<const DWORD*>(src_row)[sx];
            } else {
                reinterpret_cast<WORD*>(dst_row)[dx] =
                    reinterpret_cast<const WORD*>(src_row)[sx];
            }
        }
    }
}

static void EnsurePanelButtonPcxResources_()
{
    LoadPanelPcx24_("HA_button_frame.pcx", BTN_FRAME_W, BTN_FRAME_H,
        s_panel_button_frame, s_panel_button_frame_load_failed, false);
    LoadPanelPcx24_("HA_ok_normal.pcx", BTN_W, BTN_H,
        s_panel_ok_normal, s_panel_ok_normal_load_failed, false);
    LoadPanelPcx24_("HA_ok_pressed.pcx", BTN_W, BTN_H,
        s_panel_ok_pressed, s_panel_ok_pressed_load_failed, false);
    LoadPanelPcx24_("HA_cancel_normal.pcx", BTN_W, BTN_H,
        s_panel_cancel_normal, s_panel_cancel_normal_load_failed, false);
    LoadPanelPcx24_("HA_cancel_pressed.pcx", BTN_W, BTN_H,
        s_panel_cancel_pressed, s_panel_cancel_pressed_load_failed, false);
}

static int GetPanelBackBufferBpp_()
{
    if (!o_DDSurfaceBackBuffer)
        return H3BitMode::Get() == 4 ? 32 : 16;

    DDPIXELFORMAT format = {};
    format.dwSize = sizeof(format);
    if (SUCCEEDED(o_DDSurfaceBackBuffer->GetPixelFormat(&format))
        && (format.dwRGBBitCount == 16 || format.dwRGBBitCount == 32))
    {
        return static_cast<int>(format.dwRGBBitCount);
    }
    return H3BitMode::Get() == 4 ? 32 : 16;
}

static bool DrawPanelCompositeToBackBuffer_(H3LoadedPcx16* source, int dst_x, int dst_y)
{
    if (!source || !source->buffer || !o_DDSurfaceBackBuffer)
        return false;

    __try {
        DDSURFACEDESC desc = {};
        desc.dwSize = sizeof(desc);
        const HRESULT lock_result = o_DDSurfaceBackBuffer->Lock(
            nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr);
        if (FAILED(lock_result) || !desc.lpSurface)
            return false;

        const int dst_bpp = GetPanelBackBufferBpp_();
        int dst_w = static_cast<int>(desc.dwWidth);
        int dst_h = static_cast<int>(desc.dwHeight);
        if (dst_w <= 0 && o_WndMgr && o_WndMgr->screenPcx16)
            dst_w = o_WndMgr->screenPcx16->width;
        if (dst_h <= 0 && o_WndMgr && o_WndMgr->screenPcx16)
            dst_h = o_WndMgr->screenPcx16->height;

        int src_x = 0;
        int src_y = 0;
        int copy_w = source->width;
        int copy_h = source->height;
        if (dst_x < 0) { src_x = -dst_x; copy_w += dst_x; dst_x = 0; }
        if (dst_y < 0) { src_y = -dst_y; copy_h += dst_y; dst_y = 0; }
        if (dst_x + copy_w > dst_w) copy_w = dst_w - dst_x;
        if (dst_y + copy_h > dst_h) copy_h = dst_h - dst_y;

        const bool source_is_32_bit = H3BitMode::Get() == 4;
        if (copy_w > 0 && copy_h > 0) {
            for (int y = 0; y < copy_h; ++y) {
                BYTE* src_row = source->buffer + (src_y + y) * source->scanlineSize;
                BYTE* dst_row = static_cast<BYTE*>(desc.lpSurface)
                    + (dst_y + y) * desc.lPitch;

                if (dst_bpp == 32) {
                    BYTE* dst = dst_row + dst_x * 4;
                    if (source_is_32_bit) {
                        const DWORD* src = reinterpret_cast<const DWORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            const DWORD color = src[x];
                            dst[x * 4 + 0] = static_cast<BYTE>(color);
                            dst[x * 4 + 1] = static_cast<BYTE>(color >> 8);
                            dst[x * 4 + 2] = static_cast<BYTE>(color >> 16);
                        }
                    } else {
                        const WORD* src = reinterpret_cast<const WORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            const DWORD color = PanelRGB565To8888_(src[x]);
                            dst[x * 4 + 0] = static_cast<BYTE>(color);
                            dst[x * 4 + 1] = static_cast<BYTE>(color >> 8);
                            dst[x * 4 + 2] = static_cast<BYTE>(color >> 16);
                        }
                    }
                } else {
                    WORD* dst = reinterpret_cast<WORD*>(dst_row) + dst_x;
                    if (source_is_32_bit) {
                        const DWORD* src = reinterpret_cast<const DWORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x)
                            dst[x] = PanelRGB8888To565_(src[x]);
                    } else {
                        const WORD* src = reinterpret_cast<const WORD*>(src_row) + src_x;
                        memcpy(dst, src, copy_w * sizeof(WORD));
                    }
                }
            }
        }

        o_DDSurfaceBackBuffer->Unlock(nullptr);
        return copy_w > 0 && copy_h > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static H3LoadedPcx16* EnsurePanelComposite_()
{
    if (s_panel_composite
        && s_panel_composite->width == PANEL_W
        && s_panel_composite->height == PANEL_H)
    {
        return s_panel_composite;
    }

    if (s_panel_composite)
        s_panel_composite->Destroy();
    s_panel_composite = H3LoadedPcx16::Create(PANEL_W, PANEL_H);
    return s_panel_composite;
}

static void ReleasePanelComposite_()
{
    if (s_panel_composite) {
        s_panel_composite->Destroy();
        s_panel_composite = nullptr;
    }
    if (s_panel_background) {
        s_panel_background->Destroy();
        s_panel_background = nullptr;
    }
    if (s_panel_cell) {
        s_panel_cell->Destroy();
        s_panel_cell = nullptr;
    }
    if (s_panel_grid_frame) {
        s_panel_grid_frame->Destroy();
        s_panel_grid_frame = nullptr;
    }
    H3LoadedPcx16** button_resources[] = {
        &s_panel_ok_normal, &s_panel_ok_pressed,
        &s_panel_cancel_normal, &s_panel_cancel_pressed,
        &s_panel_button_frame
    };
    for (int i = 0; i < 5; ++i) {
        if (*button_resources[i]) {
            (*button_resources[i])->Destroy();
            *button_resources[i] = nullptr;
        }
    }
    s_panel_background_load_failed = false;
    s_panel_cell_load_failed = false;
    s_panel_grid_frame_load_failed = false;
    s_panel_ok_normal_load_failed = false;
    s_panel_ok_pressed_load_failed = false;
    s_panel_cancel_normal_load_failed = false;
    s_panel_cancel_pressed_load_failed = false;
    s_panel_button_frame_load_failed = false;
}

// 第一格（站立格）的临时标示。使用原版 CCellShd 蓝色格子资源。
// HD 最终显示走 DirectDraw 后缓冲：只写 drawBuffer/screenPcx16 不够，
// 必须把 45x52 小图直接 blit 到后缓冲；退出拾取时用战场重绘撤销。
static bool BlitPcx16ToBackBuffer_(H3LoadedPcx16* source, int dst_x, int dst_y)
{
    if (!source || !source->buffer || !o_DDSurfaceBackBuffer)
        return false;
    __try {
        DDSURFACEDESC desc = {};
        desc.dwSize = sizeof(desc);
        const HRESULT lock_result = o_DDSurfaceBackBuffer->Lock(
            nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr);
        if (FAILED(lock_result) || !desc.lpSurface)
            return false;

        const int dst_bpp = GetPanelBackBufferBpp_();
        int dst_w = static_cast<int>(desc.dwWidth);
        int dst_h = static_cast<int>(desc.dwHeight);
        if (dst_w <= 0 && o_WndMgr && o_WndMgr->screenPcx16)
            dst_w = o_WndMgr->screenPcx16->width;
        if (dst_h <= 0 && o_WndMgr && o_WndMgr->screenPcx16)
            dst_h = o_WndMgr->screenPcx16->height;

        int src_x = 0;
        int src_y = 0;
        int copy_w = source->width;
        int copy_h = source->height;
        if (dst_x < 0) { src_x = -dst_x; copy_w += dst_x; dst_x = 0; }
        if (dst_y < 0) { src_y = -dst_y; copy_h += dst_y; dst_y = 0; }
        if (dst_x + copy_w > dst_w) copy_w = dst_w - dst_x;
        if (dst_y + copy_h > dst_h) copy_h = dst_h - dst_y;

        const bool source_is_32_bit = H3BitMode::Get() == 4;
        if (copy_w > 0 && copy_h > 0) {
            for (int y = 0; y < copy_h; ++y) {
                BYTE* src_row = source->buffer + (src_y + y) * source->scanlineSize;
                BYTE* dst_row = static_cast<BYTE*>(desc.lpSurface)
                    + (dst_y + y) * desc.lPitch;
                if (dst_bpp == 32) {
                    BYTE* dst = dst_row + dst_x * 4;
                    if (source_is_32_bit) {
                        const DWORD* src = reinterpret_cast<const DWORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            const DWORD color = src[x];
                            // 透明键：纯青/近似青跳过，保留蓝色标示。
                            if ((color & 0x00FFFFFFu) == 0x0000FFFFu) continue;
                            dst[x * 4 + 0] = static_cast<BYTE>(color);
                            dst[x * 4 + 1] = static_cast<BYTE>(color >> 8);
                            dst[x * 4 + 2] = static_cast<BYTE>(color >> 16);
                        }
                    } else {
                        const WORD* src = reinterpret_cast<const WORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            const WORD c16 = src[x];
                            if (c16 == 0x7FDF) continue;
                            const DWORD color = PanelRGB565To8888_(c16);
                            dst[x * 4 + 0] = static_cast<BYTE>(color);
                            dst[x * 4 + 1] = static_cast<BYTE>(color >> 8);
                            dst[x * 4 + 2] = static_cast<BYTE>(color >> 16);
                        }
                    }
                } else {
                    WORD* dst = reinterpret_cast<WORD*>(dst_row) + dst_x;
                    if (source_is_32_bit) {
                        const DWORD* src = reinterpret_cast<const DWORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            const DWORD color = src[x];
                            if ((color & 0x00FFFFFFu) == 0x0000FFFFu) continue;
                            dst[x] = PanelRGB8888To565_(color);
                        }
                    } else {
                        const WORD* src = reinterpret_cast<const WORD*>(src_row) + src_x;
                        for (int x = 0; x < copy_w; ++x) {
                            if (src[x] == 0x7FDF) continue;
                            dst[x] = src[x];
                        }
                    }
                }
            }
        }
        o_DDSurfaceBackBuffer->Unlock(nullptr);
        return copy_w > 0 && copy_h > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

