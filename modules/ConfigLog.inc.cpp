// ========== 配置与策略枚举 ==========
// 纯策略核心同时被生产代码与 tests/PolicyCoreTests.cpp 使用。
#include "PolicyCore.hpp"

static AutoStackRule MakeDefaultRule_()
{
    return H3AutoPolicy::MakeDefaultRule();
}
// 五套仅驻留内存的已确认方案；默认全为手动。
// 首动保活字段（protectEnable/protectRatioX100）在 AutoStackRule 内随方案走。
AutoStackRule g_profiles[5][21] = {};
int g_active_profile = 0;

// 当前生效方案（运行时视图）
AutoStackRule g_active_rules[21] = {};

// 玩家接受战斗结果后清空 5 套方案（取消/重打不调用）。
// 日志在调用方 OnBattleResultAccepted 打印，避免依赖本文件后部 WriteLog。
void ClearConfirmedProfiles()
{
    const AutoStackRule def = MakeDefaultRule_();
    for (int p = 0; p < 5; ++p) {
        for (int s = 0; s < 21; ++s)
            g_profiles[p][s] = def;
    }
    g_active_profile = 0;
    for (int s = 0; s < 21; ++s)
        g_active_rules[s] = def;
}

static struct Config {
    int  disable_on_start;     // 0=不禁用（默认启用），1=禁用
    int  toggle_manual_vk;     // F9：本场自动/全手动切换
    int  one_shot_manual_vk;   // J：单次接管当前/下一支部队
} cfg;

static char g_ini_path[MAX_PATH];
static char g_log_path[MAX_PATH];
static wchar_t g_log_path_w[MAX_PATH * 2];
static HMODULE g_hModule = nullptr;
static bool g_disable_log = false;

static const int MAX_LOG_FILES_TO_KEEP = 30;
static const int MAX_LOG_FILES_TO_SCAN = 1024;

struct LogFileEntryW {
    wchar_t path[MAX_PATH * 2];
    FILETIME last_write;
};

static int __cdecl CompareLogFileEntryW(const void* a, const void* b)
{
    const LogFileEntryW* la = (const LogFileEntryW*)a;
    const LogFileEntryW* lb = (const LogFileEntryW*)b;
    int cmp = CompareFileTime(&la->last_write, &lb->last_write);
    if (cmp != 0) return cmp;
    return _wcsicmp(la->path, lb->path);
}

static char* TrimAscii(char* s)
{
    if (!s) return s;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s += 3;
    while (*s == ' ' || *s == '\t') ++s;
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
    return s;
}

static bool ReadDisableLogFromIniFileW(const wchar_t* ini_path)
{
    if (!ini_path || !ini_path[0]) return false;
    HANDLE file = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char buf[4097];
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(file, buf, sizeof(buf) - 1, &bytes_read, nullptr);
    CloseHandle(file);
    if (!ok || bytes_read == 0) return false;
    buf[bytes_read] = 0;

    bool in_logging = false;
    char* p = buf;
    while (*p) {
        char* line = p;
        while (*p && *p != '\r' && *p != '\n') ++p;
        if (*p) {
            *p++ = 0;
            if (p[-1] == '\r' && *p == '\n') ++p;
        }
        char* s = TrimAscii(line);
        if (!s || !*s || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char* close = strchr(s, ']');
            if (!close) { in_logging = false; continue; }
            *close = 0;
            char* section = TrimAscii(s + 1);
            in_logging = section && _stricmp(section, "Logging") == 0;
            continue;
        }
        if (!in_logging) continue;
        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = TrimAscii(s);
        char* value = TrimAscii(eq + 1);
        if (key && value && _stricmp(key, "DisableLog") == 0)
            return atoi(value) != 0;
    }
    // 没有 [Logging] 段 → 默认不禁用日志（DisableLog=0）
    return false;
}

static void CleanupOldLogFilesW(const wchar_t* log_dir, const wchar_t* log_base, const wchar_t* current_log_path)
{
    if (!log_dir || !log_dir[0] || !log_base || !log_base[0]) return;
    wchar_t pattern[MAX_PATH * 2];
    _snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%s\\%s_*.log", log_dir, log_base);

    LogFileEntryW* entries = (LogFileEntryW*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_LOG_FILES_TO_SCAN * sizeof(LogFileEntryW));
    if (!entries) return;
    int count = 0;
    bool current_found = false;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { HeapFree(GetProcessHeap(), 0, entries); return; }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (count >= MAX_LOG_FILES_TO_SCAN) break;
        _snwprintf_s(entries[count].path, _countof(entries[count].path), _TRUNCATE, L"%s\\%s", log_dir, fd.cFileName);
        entries[count].last_write = fd.ftLastWriteTime;
        if (current_log_path && _wcsicmp(entries[count].path, current_log_path) == 0) current_found = true;
        ++count;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    int keep_existing = current_found ? MAX_LOG_FILES_TO_KEEP : (MAX_LOG_FILES_TO_KEEP - 1);
    if (keep_existing < 0) keep_existing = 0;
    if (count <= keep_existing) { HeapFree(GetProcessHeap(), 0, entries); return; }

    qsort(entries, count, sizeof(entries[0]), CompareLogFileEntryW);
    int delete_count = count - keep_existing;
    for (int i = 0; i < delete_count; ++i) DeleteFileW(entries[i].path);
    HeapFree(GetProcessHeap(), 0, entries);
}

static void SetupDatedLogPathAndCleanup(HMODULE hModule)
{
    if (g_disable_log) {
        g_log_path[0] = 0;
        g_log_path_w[0] = 0;
        return;
    }

    wchar_t module_path[MAX_PATH * 2] = { 0 };
    GetModuleFileNameW(hModule, module_path, _countof(module_path));

    wchar_t dir[MAX_PATH * 2] = { 0 };
    wchar_t base[MAX_PATH * 2] = { 0 };
    const wchar_t* slash1 = wcsrchr(module_path, L'\\');
    const wchar_t* slash2 = wcsrchr(module_path, L'/');
    const wchar_t* slash = slash1 > slash2 ? slash1 : slash2;
    const wchar_t* name = slash ? slash + 1 : module_path;
    if (slash) {
        int len = (int)(slash - module_path);
        if (len >= (int)_countof(dir)) len = (int)_countof(dir) - 1;
        memcpy(dir, module_path, len * sizeof(wchar_t));
        dir[len] = 0;
    } else {
        wcscpy_s(dir, L".");
    }
    wcsncpy_s(base, name, _TRUNCATE);
    wchar_t* dot = wcsrchr(base, L'.');
    if (dot) *dot = 0;

    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf_s(g_log_path_w, _countof(g_log_path_w), _TRUNCATE,
        L"%s\\%s_%04u%02u%02u_%02u%02u%02u.log",
        dir, base, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    WideCharToMultiByte(CP_UTF8, 0, g_log_path_w, -1, g_log_path, sizeof(g_log_path), nullptr, nullptr);
    CleanupOldLogFilesW(dir, base, g_log_path_w);

    HANDLE hf = CreateFileW(g_log_path_w, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER fpos; fpos.QuadPart = 0;
        if (SetFilePointerEx(hf, fpos, &fpos, FILE_END) && fpos.QuadPart == 0) {
            DWORD wr; WriteFile(hf, "\xEF\xBB\xBF", 3, &wr, nullptr);
        }
        SYSTEMTIME st2; GetLocalTime(&st2);
        char line[128];
        int n = _snprintf(line, sizeof(line)-1, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] 日志初始化完成。\r\n",
            st2.wYear, st2.wMonth, st2.wDay, st2.wHour, st2.wMinute, st2.wSecond, st2.wMilliseconds);
        if (n > 0) { DWORD wr; WriteFile(hf, line, (DWORD)n, &wr, nullptr); }
        CloseHandle(hf);
    }
}

static int ClampInt(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static void WriteLog(const char* fmt, ...);  // 前向声明

static bool IsAllowedOneShotVk_(int vk)
{
    return (vk >= 'A' && vk <= 'Z' && vk != 'E');
}

static int ParseHotkeyVk_(const char* text, int default_vk, bool letter_only)
{
    if (!text) return default_vk;
    char buf[64] = {};
    strncpy(buf, text, sizeof(buf) - 1);
    char* s = TrimAscii(buf);
    if (!s || !*s) return default_vk;

    // 支持直接数字虚拟键码，例如 122=F11。0x7B。122。
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        char* end = nullptr;
        const long v = strtol(s, &end, 16);
        if (end && end != s && v > 0 && v < 256) return (int)v;
    }
    bool all_digit = true;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9') { all_digit = false; break; }
    }
    if (all_digit) {
        const int v = atoi(s);
        if (v > 0 && v < 256) return v;
    }

    // 常用名称：F1..F12 / LControl / RControl / Control / Ctrl。
    if (_stricmp(s, "LControl") == 0 || _stricmp(s, "LCtrl") == 0
        || _stricmp(s, "LeftControl") == 0 || _stricmp(s, "LeftCtrl") == 0)
        return VK_LCONTROL;
    if (_stricmp(s, "RControl") == 0 || _stricmp(s, "RCtrl") == 0
        || _stricmp(s, "RightControl") == 0 || _stricmp(s, "RightCtrl") == 0)
        return VK_RCONTROL;
    if (_stricmp(s, "Control") == 0 || _stricmp(s, "Ctrl") == 0)
        return VK_CONTROL;
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9') {
        int n = atoi(s + 1);
        if (n >= 1 && n <= 12) return VK_F1 + (n - 1);
    }
    if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))
        && s[1] == 0)
        return 'A' + (s[0] & ~0x20) - 'A';
    if (letter_only) return default_vk;
    return default_vk;
}

static void ReadConfig()
{
    const char* f = g_ini_path;
    cfg.disable_on_start = GetPrivateProfileIntA("General", "DisableOnStart", 0, f);
    cfg.disable_on_start = ClampInt(cfg.disable_on_start, 0, 1);

    char toggle_buf[64] = {};
    char oneshot_buf[64] = {};
    GetPrivateProfileStringA("Hotkeys", "ToggleManual", "F9",
        toggle_buf, sizeof(toggle_buf), f);
    GetPrivateProfileStringA("Hotkeys", "OneShotManual", "J",
        oneshot_buf, sizeof(oneshot_buf), f);
    cfg.toggle_manual_vk = ParseHotkeyVk_(toggle_buf, VK_F9, false);
    cfg.one_shot_manual_vk = ParseHotkeyVk_(oneshot_buf, 'J', true);
    if (!IsAllowedOneShotVk_(cfg.one_shot_manual_vk)) {
        WriteLog("配置警告：OneShotManual 只允许单个字母，已回退到 J");
        cfg.one_shot_manual_vk = 'J';
    }

    WriteLog("配置加载：DisableOnStart=%d ToggleManual=0x%X OneShotManual=0x%X",
        cfg.disable_on_start, cfg.toggle_manual_vk, cfg.one_shot_manual_vk);
}

static const char* HotkeyDisplayName_(int vk)
{
    static char buf[16];
    if (vk >= VK_F1 && vk <= VK_F12) {
        snprintf(buf, sizeof(buf), "F%d", vk - VK_F1 + 1);
        return buf;
    }
    if (vk >= 'A' && vk <= 'Z') {
        snprintf(buf, sizeof(buf), "%c", vk);
        return buf;
    }
    if (vk >= '0' && vk <= '9') {
        snprintf(buf, sizeof(buf), "%c", vk);
        return buf;
    }
    if (vk == VK_LCONTROL) return "左Ctrl";
    if (vk == VK_RCONTROL) return "右Ctrl";
    if (vk == VK_CONTROL) return "Ctrl";
    snprintf(buf, sizeof(buf), "0x%X", vk);
    return buf;
}

// ========== 日志输出 ==========

static void AppendUtf8LogLine(const char* text)
{
    if (g_disable_log) return;
    if (!g_log_path_w[0]) return;
    HANDLE h = CreateFileW(g_log_path_w, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (SetFilePointerEx(h, pos, &pos, FILE_END) && pos.QuadPart == 0) {
        DWORD written = 0;
        const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        WriteFile(h, bom, 3, &written, nullptr);
    }
    DWORD written = 0;
    WriteFile(h, text, (DWORD)strlen(text), &written, nullptr);
    WriteFile(h, "\r\n", 2, &written, nullptr);
    CloseHandle(h);
}

static void WriteLog(const char* fmt, ...)
{
    if (g_disable_log) return;
    char line[1024];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int off = _snprintf(line, sizeof(line) - 1, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (off < 0) off = 0;
    if (off >= (int)sizeof(line)) off = (int)sizeof(line) - 1;

    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line + off, sizeof(line) - off - 1, fmt, ap);
    va_end(ap);
    line[sizeof(line) - 1] = 0;
    AppendUtf8LogLine(line);
}
