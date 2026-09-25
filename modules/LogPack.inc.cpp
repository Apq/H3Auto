// ========== 日志打包（帮助界面「打包日志」按钮） ==========
// 最近 5 个日志文件（每文件只取尾部 2MB）LZMA 压缩为 .7z（手写容器，
// LZMA SDK 源码级集成见 lzma/），以 CF_HDROP 文件式复制到剪贴板
// （同资源管理器复制文件，QQ 聊天框 Ctrl+V 直接发送），同时附
// CF_TEXT 路径。容器布局经 7-Zip 官方与 py7zr 双端验证。
// 文件名与路径均为 ASCII（日志/7z 都在 DLL 同目录，无中文坑）。

// LzmaEnc.h → 7zTypes.h 用 EXTERN_C_BEGIN 包裹到文件尾，且注释掉了
// windows.h，所以 windows.h 必须放在它之后（否则 DWORD 等落在 C 链接块内不可见）。
#include "LzmaEnc.h"
#include <windows.h>

// DROPFILES 头大小（4+8+4+4=20，x86 自然对齐无 padding）。
static const size_t kDropFilesSize_ = 20;

// CRC32（IEEE 802.3 多项式），表运行时生成一次。
static DWORD s_logpack_crc_table[256];
static bool s_logpack_crc_ready = false;

static void LogPackInitCrc_()
{
    for (DWORD i = 0; i < 256; ++i) {
        DWORD c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_logpack_crc_table[i] = c;
    }
    s_logpack_crc_ready = true;
}

static DWORD LogPackCrc_(const void* data, size_t size)
{
    if (!s_logpack_crc_ready) LogPackInitCrc_();
    DWORD crc = 0xFFFFFFFFu;
    const BYTE* p = (const BYTE*)data;
    for (size_t i = 0; i < size; ++i)
        crc = s_logpack_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}


// LZMA SDK 的内存分配回调（游戏进程内走 CRT 堆）。
static void* LzmaPackAlloc_(const ISzAllocPtr p, size_t size)
{
    (void)p;
    return malloc(size ? size : 1);
}
static void LzmaPackFree_(const ISzAllocPtr p, void* address)
{
    (void)p;
    free(address);
}
static const ISzAlloc g_lzma_pack_alloc_ = { LzmaPackAlloc_, LzmaPackFree_ };

struct LogPackEntry {
    char name[64];       // 文件名（ASCII）
    FILETIME write_time; // 最近写入时间
};

// DLL 同目录（从 g_ini_path 截掉文件名而来，ACP char）。
static void LogPackDllDir_(char* dir, int dir_size)
{
    strncpy(dir, g_ini_path, dir_size - 1);
    dir[dir_size - 1] = 0;
    char* slash = strrchr(dir, (char)92);
    if (!slash) slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    else dir[0] = 0;
}

// 找最近 max_count 个日志（按写入时间降序）。返回实际数量。
static int LogPackCollectRecent_(LogPackEntry* out, int max_count)
{
    char dir[MAX_PATH] = {};
    LogPackDllDir_(dir, sizeof(dir));
    if (!dir[0]) return 0;

    LogPackEntry all[64];
    int total = 0;
    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH] = {};
    _snprintf(pattern, sizeof(pattern) - 1, "%s\\H3Auto_*.log", dir);
    pattern[sizeof(pattern) - 1] = 0;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (total >= (int)(sizeof(all) / sizeof(all[0]))) break;
        strncpy(all[total].name, fd.cFileName, sizeof(all[total].name) - 1);
        all[total].name[sizeof(all[total].name) - 1] = 0;
        all[total].write_time = fd.ftLastWriteTime;
        ++total;
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    // 写入时间降序（简单选择排序，n≤64）。
    for (int i = 0; i < total; ++i) {
        int best = i;
        for (int j = i + 1; j < total; ++j)
            if (CompareFileTime(&all[j].write_time, &all[best].write_time) > 0)
                best = j;
        if (best != i) {
            LogPackEntry t = all[i]; all[i] = all[best]; all[best] = t;
        }
    }
    const int n = total < max_count ? total : max_count;
    for (int i = 0; i < n; ++i) out[i] = all[i];
    return n;
}

// 单文件参与打包的上限：只取每个日志的尾部（最新内容在尾部）。
// 此前按全量读（上限 64MB/文件），32 位游戏进程内同步分配可达数百 MB，
// UI 线程换页假死（用户实测「点击打包后卡死」）。
static const DWORD kLogPackMaxPerFile_ = 2 * 1024 * 1024;
static void LogPackPut2_(BYTE* p, unsigned v) { p[0] = (BYTE)(v & 0xFF); p[1] = (BYTE)((v >> 8) & 0xFF); }
static void LogPackPut4_(BYTE* p, unsigned v)
{
    p[0] = (BYTE)(v & 0xFF); p[1] = (BYTE)((v >> 8) & 0xFF);
    p[2] = (BYTE)((v >> 16) & 0xFF); p[3] = (BYTE)((v >> 24) & 0xFF);
}

// DOS 日期时间。
static void LogPackDosTime_(unsigned* dos_time, unsigned* dos_date)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    *dos_time = ((unsigned)st.wHour << 11) | ((unsigned)st.wMinute << 5)
        | ((unsigned)(st.wSecond / 2) & 0x1F);
    *dos_date = (((unsigned)st.wYear - 1980u) << 9) | ((unsigned)st.wMonth << 5)
        | (unsigned)st.wDay;
}


// ---------- .7z 容器（明文 header，每文件独立 folder/LZMA 流） ----------
// 布局与 7-Zip 官方 7zArcIn.c 解析严格对齐：
//  - kName 名字流：每名 UTF-16LE+终止 0，最后一个名字的终止兼任流末尾，
//    不得再补总终止（SzReadFileNames 要求读完 numFiles 个名字后 pos==size）
//  - kSubStreamsInfo 需显式 kNumUnpackStreams（每 folder 1）

// 7z number：首字节前导 1 数=额外字节数，低 (8-k) 位为高位部分。
static BYTE* LogPackPutNum_(BYTE* p, unsigned long long v)
{
    for (int k = 0; k < 8; ++k) {
        const unsigned long long hi = v >> (8 * k);
        if (hi < ((unsigned long long)0x80 >> k)) {
            if (k == 0) {
                *p++ = (BYTE)v;
            } else {
                *p++ = (BYTE)((((0xFFull << (8 - k)) & 0xFF)) | hi);
                for (int b = 0; b < k; ++b)
                    *p++ = (BYTE)((v >> (8 * b)) & 0xFF);
            }
            return p;
        }
    }
    return p; // 不可达（尺寸远小于 2^56）
}

// LZMA 压缩单块。返回分配的压缩缓冲（*out_len 为长度），失败返回 null。
static BYTE* LogPackLzma_(const BYTE* src, size_t src_len,
    size_t* out_len, BYTE props_out[LZMA_PROPS_SIZE])
{
    const size_t cap = src_len + src_len / 2 + 4096;
    BYTE* dst = new(std::nothrow) BYTE[cap];
    if (!dst) return nullptr;

    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.dictSize = 1u << 20;      // 1MB 字典：编码内存 ~10MB，32 位安全
    props.level = 5;
    props.writeEndMark = 0;
    LzmaEncProps_Normalize(&props);

    size_t props_size = LZMA_PROPS_SIZE;
    size_t dst_len = cap;
    const SRes res = LzmaEncode(dst, &dst_len, src, src_len, &props,
        props_out, &props_size, 0, nullptr, &g_lzma_pack_alloc_,
        &g_lzma_pack_alloc_);
    if (res != SZ_OK || props_size != LZMA_PROPS_SIZE) {
        delete[] dst;
        return nullptr;
    }
    *out_len = dst_len;
    return dst;
}

// 复制到剪贴板：CF_HDROP（Explorer 式文件复制，QQ 聊天框 Ctrl+V 直接
// 发送 zip 文件）+ CF_TEXT（地址栏/记事本可粘贴路径）。失败重试，被占用常见。
static bool LogPackCopyToClipboard_(const char* path)
{
    const size_t len = strlen(path);
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (!OpenClipboard(nullptr)) {
            Sleep(30);
            continue;
        }
        bool ok = EmptyClipboard();
        // CF_HDROP：DROPFILES 头 + 双零结尾的宽字符路径列表。
        if (ok) {
            wchar_t wpath[MAX_PATH] = {};
            MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);
            const size_t wlen = wcslen(wpath);
            const size_t bytes = kDropFilesSize_ + (wlen + 2) * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (mem) {
                BYTE* raw = (BYTE*)GlobalLock(mem);
                if (raw) {
                    // DROPFILES 头（shellapi.h 的 guard 在此工具链下不可靠，布局固定 20 字节）
                    memset(raw, 0, kDropFilesSize_);
                    *(DWORD*)(raw + 0) = kDropFilesSize_;   // pFiles：文件列表偏移
                    *(DWORD*)(raw + 16) = 1;                // fWide：宽字符
                    wchar_t* dst = (wchar_t*)(raw + kDropFilesSize_);
                    memcpy(dst, wpath, (wlen + 1) * sizeof(wchar_t));
                    dst[wlen + 1] = 0; // 列表结尾的额外空字符
                    GlobalUnlock(mem);
                    ok = SetClipboardData(CF_HDROP, mem) != nullptr;
                    if (!ok) GlobalFree(mem); // 成功后归剪贴板所有
                } else {
                    GlobalFree(mem);
                    ok = false;
                }
            } else {
                ok = false;
            }
        }
        if (ok) {
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
            if (mem) {
                char* dst = (char*)GlobalLock(mem);
                if (dst) {
                    memcpy(dst, path, len + 1);
                    GlobalUnlock(mem);
                    if (SetClipboardData(CF_TEXT, mem) == nullptr) GlobalFree(mem);
                } else {
                    GlobalFree(mem);
                }
            }
        }
        CloseClipboard();
        return ok;
    }
    return false;
}

// 打包入口：成功返回 true 并把 .7z 完整路径写进 out_path（提示用）。
// 原因文案键（help.pack_fail 的 %s）由调用方组织；此处只回填路径/原因。
static bool PackRecentLogs_(char* out_path, int out_path_size, char* fail_reason, int reason_size)
{
    out_path[0] = 0;
    fail_reason[0] = 0;

    // 5 槽：最近 4 个日志 + 1 个发送说明（QQ 号写进包内 txt，解压即可复制；
    // 剪贴板继续只放 .7z 文件本身，不被文本挤掉）。
    LogPackEntry entries[5];
    const int nLogs = LogPackCollectRecent_(entries, 4);
    if (nLogs <= 0) {
        _snprintf(fail_reason, reason_size - 1, "%s", T("help.pack_no_logs"));
        fail_reason[reason_size - 1] = 0;
        return false;
    }

    char dir[MAX_PATH] = {};
    LogPackDllDir_(dir, sizeof(dir));

    // 读入每个日志的尾部（≤2MB）并 LZMA 压缩。
    BYTE* datas[5] = {};
    BYTE* packs[5] = {};
    size_t pack_lens[5] = {};
    DWORD unpack_sizes[5] = {};
    DWORD crcs[5] = {};
    BYTE props[LZMA_PROPS_SIZE] = {};
    int done = 0;
    for (int i = 0; i < nLogs; ++i) {
        char path[MAX_PATH] = {};
        _snprintf(path, sizeof(path) - 1, "%s\\%s", dir, entries[i].name);
        path[sizeof(path) - 1] = 0;
        HANDLE hf = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) {
            _snprintf(fail_reason, reason_size - 1, "%s", entries[i].name);
            goto bail;
        }
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(hf, &sz) || sz.QuadPart <= 0) {
            CloseHandle(hf);
            _snprintf(fail_reason, reason_size - 1, "%s", entries[i].name);
            goto bail;
        }
        // 超过上限只取尾部（最新内容在尾部），总量封顶 ~10MB。
        const LONGLONG full = sz.QuadPart;
        unpack_sizes[i] = full > (LONGLONG)kLogPackMaxPerFile_
            ? kLogPackMaxPerFile_ : (DWORD)full;
        if (full > (LONGLONG)unpack_sizes[i]) {
            LARGE_INTEGER skip;
            skip.QuadPart = full - (LONGLONG)unpack_sizes[i];
            if (!SetFilePointerEx(hf, skip, nullptr, FILE_BEGIN)) {
                CloseHandle(hf);
                _snprintf(fail_reason, reason_size - 1, "%s", entries[i].name);
                goto bail;
            }
        }
        datas[i] = new(std::nothrow) BYTE[unpack_sizes[i] ? unpack_sizes[i] : 1];
        if (!datas[i]) {
            CloseHandle(hf);
            _snprintf(fail_reason, reason_size - 1, "%s", entries[i].name);
            goto bail;
        }
        DWORD got = 0;
        if (!ReadFile(hf, datas[i], unpack_sizes[i], &got, nullptr) || got != unpack_sizes[i]) {
            CloseHandle(hf);
            _snprintf(fail_reason, reason_size - 1, "%s", entries[i].name);
            goto bail;
        }
        CloseHandle(hf);
        crcs[i] = LogPackCrc_(datas[i], unpack_sizes[i]);

        packs[i] = LogPackLzma_(datas[i], unpack_sizes[i], &pack_lens[i], props);
        if (!packs[i]) {
            _snprintf(fail_reason, reason_size - 1, "%s", entries[i].name);
            goto bail;
        }
        done = i + 1;
    }
    // 发送说明：UTF-8 带 BOM 的 txt（记事本双击即正确显示，QQ 号可复制）。
    {
        char note[512];
        _snprintf(note, sizeof(note) - 1, "%s", T("help.pack_note"));
        note[sizeof(note) - 1] = 0;
        const int note_len = (int)strlen(note);
        const int total_len = 3 + note_len;
        BYTE* nd = new(std::nothrow) BYTE[total_len];
        if (!nd) { _snprintf(fail_reason, reason_size - 1, "alloc"); goto bail; }
        nd[0] = 0xEF; nd[1] = 0xBB; nd[2] = 0xBF;
        memcpy(nd + 3, note, note_len);
        const int idx = nLogs;
        strncpy(entries[idx].name, "readme.txt", sizeof(entries[idx].name) - 1);
        datas[idx] = nd;
        unpack_sizes[idx] = (DWORD)total_len;
        crcs[idx] = LogPackCrc_(nd, total_len);
        packs[idx] = LogPackLzma_(nd, total_len, &pack_lens[idx], props);
        if (!packs[idx]) {
            _snprintf(fail_reason, reason_size - 1, "readme");
            goto bail;
        }
        done = idx + 1;
    }
    {
        // ---- 组 .7z 容器：签名头(32) + pack 数据 + 明文 header ----
        const int n = done;
        size_t pack_total = 0;
        size_t names_bytes = 0;
        for (int i = 0; i < n; ++i) {
            pack_total += pack_lens[i];
            names_bytes += (strlen(entries[i].name) + 1) * 2; // UTF-16LE + 终止
        }
        const size_t header_cap = 128 + (size_t)n * (16 + LZMA_PROPS_SIZE + 24)
            + names_bytes + 16;
        BYTE* header = new(std::nothrow) BYTE[header_cap];
        BYTE* blob = new(std::nothrow) BYTE[32 + pack_total + header_cap];
        if (!header || !blob) {
            delete[] header;
            delete[] blob;
            _snprintf(fail_reason, reason_size - 1, "alloc");
            goto bail;
        }

        BYTE* h = header;
        *h++ = 0x01;                                   // kHeader
        *h++ = 0x04;                                   // kMainStreamsInfo
        *h++ = 0x06;                                   // kPackInfo
        h = LogPackPutNum_(h, 0);                      // PackPos
        h = LogPackPutNum_(h, (unsigned long long)n);  // NumPackStreams
        *h++ = 0x09;                                   // kSize
        for (int i = 0; i < n; ++i) h = LogPackPutNum_(h, pack_lens[i]);
        *h++ = 0x00;                                   // kEnd (PackInfo)
        *h++ = 0x07;                                   // kUnpackInfo
        *h++ = 0x0B;                                   // kFolder
        h = LogPackPutNum_(h, (unsigned long long)n);  // NumFolders
        *h++ = 0x00;                                   // External=0
        for (int i = 0; i < n; ++i) {
            *h++ = 0x01;                               // NumCoders=1
            *h++ = 0x23;                               // flags: idSize=3 + hasAttrs
            *h++ = 0x03; *h++ = 0x01; *h++ = 0x01;     // LZMA codec id
            *h++ = (BYTE)LZMA_PROPS_SIZE;              // PropsSize=5
            memcpy(h, props, LZMA_PROPS_SIZE); h += LZMA_PROPS_SIZE;
        }
        *h++ = 0x0C;                                   // kCodersUnpackSize
        for (int i = 0; i < n; ++i) h = LogPackPutNum_(h, unpack_sizes[i]);
        *h++ = 0x0A;                                   // kCRC
        *h++ = 0x01;                                   // AllAreDefined
        for (int i = 0; i < n; ++i) {
            memcpy(h, &crcs[i], 4); h += 4;
        }
        *h++ = 0x00;                                   // kEnd (UnpackInfo)
        *h++ = 0x08;                                   // kSubStreamsInfo
        *h++ = 0x0D;                                   // kNumUnpackStreams
        for (int i = 0; i < n; ++i) *h++ = 0x01;       // 每 folder 1 子流
        *h++ = 0x00;                                   // kEnd (SubStreamsInfo)
        *h++ = 0x00;                                   // kEnd (StreamsInfo)
        *h++ = 0x05;                                   // kFilesInfo
        h = LogPackPutNum_(h, (unsigned long long)n);  // NumFiles
        *h++ = 0x11;                                   // kName
        h = LogPackPutNum_(h, 1 + names_bytes);        // property size（External+名字流）
        *h++ = 0x00;                                   // External=0
        for (int i = 0; i < n; ++i) {
            wchar_t wname[64] = {};
            MultiByteToWideChar(CP_ACP, 0, entries[i].name, -1, wname, 64);
            for (const wchar_t* w = wname; *w; ++w) {
                memcpy(h, w, 2); h += 2;
            }
            *h++ = 0x00; *h++ = 0x00;                  // 名字终止（最后一个兼任流末尾）
        }
        *h++ = 0x00;                                   // kEnd (FilesInfo)
        *h++ = 0x00;                                   // kEnd (Header)
        const size_t header_len = (size_t)(h - header);

        // pack 数据紧跟签名头，header 接在 pack 数据之后。
        BYTE* p = blob + 32;
        for (int i = 0; i < n; ++i) {
            memcpy(p, packs[i], pack_lens[i]);
            p += pack_lens[i];
        }
        memcpy(p, header, header_len);
        // 签名头：6B 签名 + 版本 0.4 + StartHeaderCRC(4) + offset/size/headerCRC(20)。
        BYTE* sig = blob;
        memcpy(sig, "7z\xBC\xAF\x27\x1C", 6);
        sig[6] = 0; sig[7] = 4;
        // StartHeader：NextHeaderOffset(8) + NextHeaderSize(8) + NextHeaderCRC(4)，
        // 共 20 字节小端；其 CRC 直接对这 20 字节计算（传数组会被按元素语义误解）。
        unsigned long long v;
        v = (unsigned long long)pack_total;      memcpy(sig + 12, &v, 8);
        v = (unsigned long long)header_len;      memcpy(sig + 20, &v, 8);
        const DWORD hdr_crc = LogPackCrc_(header, header_len);
        memcpy(sig + 28, &hdr_crc, 4);
        const DWORD sh_crc = LogPackCrc_(sig + 12, 20);
        memcpy(sig + 8, &sh_crc, 4);

        SYSTEMTIME st;
        GetLocalTime(&st);
        char zip_name[48] = {};
        _snprintf(zip_name, sizeof(zip_name) - 1,
            "H3Auto_logs_%04u%02u%02u_%02u%02u%02u.7z",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        zip_name[sizeof(zip_name) - 1] = 0;
        _snprintf(out_path, out_path_size - 1, "%s\\%s", dir, zip_name);
        out_path[out_path_size - 1] = 0;

        const DWORD blob_len = (DWORD)(32 + pack_total + header_len);
        bool ok = false;
        HANDLE hz = CreateFileA(out_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hz != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            ok = WriteFile(hz, blob, blob_len, &wrote, nullptr) && wrote == blob_len;
            CloseHandle(hz);
        }
        delete[] header;
        delete[] blob;

        if (!ok) {
            _snprintf(fail_reason, reason_size - 1, "%s", zip_name);
            goto bail;
        }
        if (!LogPackCopyToClipboard_(out_path)) {
            // 7z 已生成，只是剪贴板被占用：路径已写 out_path，调用方可提示手动复制。
            _snprintf(fail_reason, reason_size - 1, "%s", T("help.pack_clipboard_fail"));
            goto bail;
        }
        LogInfo("[LogPack] 已打包 %d 个日志（LZMA）→ %s（已文件式复制到剪贴板）",
            done, zip_name);
        for (int i = 0; i < done; ++i) delete[] datas[i];
        for (int i = 0; i < done; ++i) if (packs[i]) delete[] packs[i];
        return true;
    }

bail:
    for (int i = 0; i < done; ++i) delete[] datas[i];
    for (int i = 0; i < done; ++i) if (packs[i]) delete[] packs[i];
    return false;
}