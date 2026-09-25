// ========== 日志打包（帮助界面「打包日志」按钮） ==========
// 最近 5 个日志文件合并为一个 zip（STORE 不压缩），以 CF_HDROP 文件式
// 复制到剪贴板（同资源管理器复制文件，QQ 聊天框 Ctrl+V 直接发送 zip），
// 同时附 CF_TEXT 路径。纯 Win32 + 手写 zip 结构，无 zlib 依赖；文件名与
// 路径均为 ASCII（日志/zip 都在 DLL 同目录，无中文坑）。

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

// 小端写出助手。
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

// 打包入口：成功返回 true 并把 zip 完整路径写进 out_path（提示用）。
// 原因文案键（help.pack_fail 的 %s）由调用方组织；此处只回填路径/原因。
static bool PackRecentLogs_(char* out_path, int out_path_size, char* fail_reason, int reason_size)
{
    out_path[0] = 0;
    fail_reason[0] = 0;

    LogPackEntry entries[5];
    const int n = LogPackCollectRecent_(entries, 5);
    if (n <= 0) {
        _snprintf(fail_reason, reason_size - 1, "%s", T("help.pack_no_logs"));
        fail_reason[reason_size - 1] = 0;
        return false;
    }

    // 读入全部日志内容到堆（每个文件一块）。
    char dir[MAX_PATH] = {};
    LogPackDllDir_(dir, sizeof(dir));
    BYTE* datas[5] = {};
    DWORD sizes[5] = {};
    DWORD crcs[5] = {};
    for (int i = 0; i < n; ++i) {
        char path[MAX_PATH] = {};
        _snprintf(path, sizeof(path) - 1, "%s\\%s", dir, entries[i].name);
        path[sizeof(path) - 1] = 0;
        HANDLE hf = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) {
            _snprintf(fail_reason, reason_size - 1, "%s", entries[i].name);
            for (int k = 0; k < i; ++k) delete[] datas[k];
            return false;
        }
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(hf, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 64 * 1024 * 1024) {
            CloseHandle(hf);
            _snprintf(fail_reason, reason_size - 1, "%s", entries[i].name);
            for (int k = 0; k < i; ++k) delete[] datas[k];
            return false;
        }
        sizes[i] = (DWORD)sz.QuadPart;
        datas[i] = new BYTE[sizes[i]];
        DWORD got = 0;
        if (!ReadFile(hf, datas[i], sizes[i], &got, nullptr) || got != sizes[i]) {
            CloseHandle(hf);
            _snprintf(fail_reason, reason_size - 1, "%s", entries[i].name);
            for (int k = 0; k <= i; ++k) delete[] datas[k];
            return false;
        }
        CloseHandle(hf);
        crcs[i] = LogPackCrc_(datas[i], sizes[i]);
    }

    // zip 输出路径：DLL 同目录 H3Auto_logs_YYYYMMDD_HHMMSS.zip。
    SYSTEMTIME st;
    GetLocalTime(&st);
    char zip_name[48] = {};
    _snprintf(zip_name, sizeof(zip_name) - 1,
        "H3Auto_logs_%04u%02u%02u_%02u%02u%02u.zip",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    zip_name[sizeof(zip_name) - 1] = 0;
    _snprintf(out_path, out_path_size - 1, "%s\\%s", dir, zip_name);
    out_path[out_path_size - 1] = 0;

    // 组 zip（STORE）：Local 头 + 数据 → Central → EOCD。
    unsigned dos_time = 0, dos_date = 0;
    LogPackDosTime_(&dos_time, &dos_date);
    size_t names_len = 0;
    size_t data_total = 0;
    for (int i = 0; i < n; ++i) {
        names_len += strlen(entries[i].name);
        data_total += sizes[i];
    }
    const size_t total = (size_t)n * (30 + 46) + names_len + data_total + 22 + 16;
    BYTE* zip = new BYTE[total];
    BYTE* p = zip;
    DWORD offsets[5] = {};

    for (int i = 0; i < n; ++i) {
        const DWORD nlen = (DWORD)strlen(entries[i].name);
        offsets[i] = (DWORD)(p - zip);
        memcpy(p, "PK\x03\x04", 4); p += 4;
        LogPackPut2_(p, 20);            p += 2; // version needed
        LogPackPut2_(p, 0);             p += 2; // flags
        LogPackPut2_(p, 0);             p += 2; // method = STORE
        LogPackPut2_(p, dos_time);      p += 2;
        LogPackPut2_(p, dos_date);      p += 2;
        LogPackPut4_(p, crcs[i]);       p += 4;
        LogPackPut4_(p, sizes[i]);      p += 4; // compressed
        LogPackPut4_(p, sizes[i]);      p += 4; // uncompressed
        LogPackPut2_(p, nlen);          p += 2;
        LogPackPut2_(p, 0);             p += 2; // extra len
        memcpy(p, entries[i].name, nlen); p += nlen;
        memcpy(p, datas[i], sizes[i]);  p += sizes[i];
    }
    const DWORD cd_offset = (DWORD)(p - zip);
    for (int i = 0; i < n; ++i) {
        const DWORD nlen = (DWORD)strlen(entries[i].name);
        memcpy(p, "PK\x01\x02", 4); p += 4;
        LogPackPut2_(p, 20);            p += 2; // version made by
        LogPackPut2_(p, 20);            p += 2; // version needed
        LogPackPut2_(p, 0);             p += 2; // flags
        LogPackPut2_(p, 0);             p += 2; // method
        LogPackPut2_(p, dos_time);      p += 2;
        LogPackPut2_(p, dos_date);      p += 2;
        LogPackPut4_(p, crcs[i]);       p += 4;
        LogPackPut4_(p, sizes[i]);      p += 4;
        LogPackPut4_(p, sizes[i]);      p += 4;
        LogPackPut2_(p, nlen);          p += 2;
        LogPackPut2_(p, 0);             p += 2; // extra len
        LogPackPut2_(p, 0);             p += 2; // comment len
        LogPackPut2_(p, 0);             p += 2; // disk start
        LogPackPut2_(p, 0);             p += 2; // internal attrs
        LogPackPut4_(p, 0);             p += 4; // external attrs
        LogPackPut4_(p, offsets[i]);    p += 4; // local header offset
        memcpy(p, entries[i].name, nlen); p += nlen;
    }
    const DWORD cd_size = (DWORD)(p - zip) - cd_offset;
    memcpy(p, "PK\x05\x06", 4); p += 4;
    LogPackPut2_(p, 0);         p += 2; // this disk
    LogPackPut2_(p, 0);         p += 2; // cd start disk
    LogPackPut2_(p, (unsigned)n); p += 2;
    LogPackPut2_(p, (unsigned)n); p += 2;
    LogPackPut4_(p, cd_size);   p += 4;
    LogPackPut4_(p, cd_offset); p += 4;
    LogPackPut2_(p, 0);         p += 2; // comment len

    const DWORD zip_len = (DWORD)(p - zip);
    bool ok = false;
    HANDLE hz = CreateFileA(out_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hz != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        ok = WriteFile(hz, zip, zip_len, &wrote, nullptr) && wrote == zip_len;
        CloseHandle(hz);
    }
    for (int i = 0; i < n; ++i) delete[] datas[i];
    delete[] zip;

    if (!ok) {
        _snprintf(fail_reason, reason_size - 1, "%s", zip_name);
        return false;
    }
    if (!LogPackCopyToClipboard_(out_path)) {
        // zip 已生成，只是剪贴板被占用：路径已写 out_path，调用方可提示手动复制。
        _snprintf(fail_reason, reason_size - 1, "%s", T("help.pack_clipboard_fail"));
        return false;
    }
    LogInfo("[LogPack] 已打包 %d 个日志 → %s（zip 已文件式复制到剪贴板）", n, zip_name);
    return true;
}
