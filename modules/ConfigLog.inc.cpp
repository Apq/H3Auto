// ========== 配置与策略枚举 ==========
// 纯策略核心同时被生产代码与 tests/PolicyCoreTests.cpp 使用。
#include "PolicyCore.hpp"

static AutoStackRule MakeDefaultRule_()
{
    return H3AutoPolicy::MakeDefaultRule();
}
// 5 套方案驻留内存（每个编号一份草稿/已确认方案）：切换编号时各自的
// 草稿独立保留，未存档也不丢。方案编号 1-5 同时是存档文件编号
// （H3Auto.profilesN.ini）。
// 首动保活的勾选（protectEnable）在 AutoStackRule 内随方案走；策略是方案级。
AutoStackRule g_profiles[5][21] = {};
int g_active_profile = 0;

// 当前生效方案（运行时视图 = g_profiles[g_active_profile]）
AutoStackRule g_active_rules[21] = {};

// 上次存/读档的槽位（0..4），独立文件 H3Auto.last.ini 持久化；进面板自动选中，
// 不自动读档。跨战斗保留。
int g_last_profile = 0;

// 保活策略（方案级）：部队勾选（protectEnable）在规则里，何时施救的策略随方案走。
uint8_t g_protect_strategy[5] = {};   // ProtectStrategy，默认 0=PS_NONE（无）
uint16_t g_stop_turns[5] = { H3AutoPolicy::DEFAULT_STOP_TURNS,
    H3AutoPolicy::DEFAULT_STOP_TURNS, H3AutoPolicy::DEFAULT_STOP_TURNS,
    H3AutoPolicy::DEFAULT_STOP_TURNS, H3AutoPolicy::DEFAULT_STOP_TURNS }; // 0=关闭，0..999

// 玩家接受战斗结果后清空 5 套方案（取消/重打不调用）。
// 日志在调用方 OnBattleResultAccepted 打印，避免依赖本文件后部 WriteLog。
// g_last_profile 不清：下次进面板仍选中最后存/读档编号。
void ClearConfirmedProfiles()
{
    const AutoStackRule def = MakeDefaultRule_();
    for (int p = 0; p < 5; ++p) {
        for (int s = 0; s < 21; ++s)
            g_profiles[p][s] = def;
        g_protect_strategy[p] = H3AutoPolicy::PS_NONE;
        g_stop_turns[p] = H3AutoPolicy::DEFAULT_STOP_TURNS;
    }
    g_active_profile = 0;
    for (int s = 0; s < 21; ++s)
        g_active_rules[s] = def;
}

static struct Config {
    int  disable_on_start;     // 0=不禁用（默认启用），1=禁用
    int  toggle_manual_vk;     // F9：本场自动/全手动切换
    int  one_shot_manual_vk;   // J：单次接管当前/下一支部队
    int  open_settings_vk;     // P：战斗中打开设置面板（§10.1）
} cfg;

// 常驻路径：堆上 4MB，DLL 加载时分配一次。目录可能很深，MAX_PATH 不够。
static char* g_ini_path = new char[kPathCap_];
static char* g_log_path = new char[kPathCap_];
static char* g_profiles_prefix = new char[kPathCap_]; // 每槽一文件：前缀 + 编号（1..5）
static char* g_last_profile_path = new char[kPathCap_]; // 编号记忆：H3Auto.last.ini
static wchar_t* g_log_path_w = new wchar_t[kPathCap_ / 2];

// 拼出编号 N（1..5）的存档文件全路径：H3Auto.profilesN.ini。
void ProfileSlotPath(int slot, char* buf, int buf_size)
{
    if (slot < 0 || slot >= 5) slot = 0;
    _snprintf(buf, buf_size - 1, "%s%d.ini", g_profiles_prefix, slot + 1);
    if (buf_size > 0) buf[buf_size - 1] = 0;
}

// 存/读档成功后记忆槽位：更新内存值并写 H3Auto.last.ini（跨战斗/跨场保留）。
void RememberProfileSlot(int slot)
{
    if (slot < 0 || slot >= 5) return;
    g_last_profile = slot;
    char buf[8] = {};
    _snprintf(buf, sizeof(buf) - 1, "%d", slot + 1);
    FILE* fp = nullptr;
    wchar_t* wpath = Utf8ToWideAlloc_(g_last_profile_path);
    if (wpath && _wfopen_s(&fp, wpath, L"wb") == 0 && fp) {
        fwrite(buf, 1, strlen(buf), fp);
        fclose(fp);
    }
    delete[] wpath;
}
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
    // 宽字符路径转 UTF-8 后走统一解析（IniReadIntUtf8 内部按 UTF-8 路径 _wfopen）。
    if (!ini_path || !ini_path[0]) return false;
    char* path = new(std::nothrow) char[kPathCap_];
    if (!path) return false;
    WideCharToMultiByte(CP_UTF8, 0, ini_path, -1, path, kPathCap_, nullptr, nullptr);
    const bool disabled = IniReadIntUtf8(path, "Logging", "DisableLog", 0) != 0;
    delete[] path;
    return disabled;
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

    const int cap = kPathCap_ / 2;
    wchar_t* module_path = new wchar_t[cap]();
    wchar_t* dir = new wchar_t[cap]();
    wchar_t* base = new wchar_t[cap]();
    GetModuleFileNameW(hModule, module_path, cap);

    const wchar_t* slash1 = wcsrchr(module_path, L'\\');
    const wchar_t* slash2 = wcsrchr(module_path, L'/');
    const wchar_t* slash = slash1 > slash2 ? slash1 : slash2;
    const wchar_t* name = slash ? slash + 1 : module_path;
    if (slash) {
        int len = (int)(slash - module_path);
        if (len >= cap) len = cap - 1;
        memcpy(dir, module_path, len * sizeof(wchar_t));
        dir[len] = 0;
    } else {
        wcscpy_s(dir, cap, L".");
    }
    wcsncpy_s(base, cap, name, _TRUNCATE);
    wchar_t* dot = wcsrchr(base, L'.');
    if (dot) *dot = 0;

    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf_s(g_log_path_w, kPathCap_ / 2, _TRUNCATE,
        L"%s\\%s_%04u%02u%02u_%02u%02u%02u.log",
        dir, base, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    WideCharToMultiByte(CP_UTF8, 0, g_log_path_w, -1, g_log_path, kPathCap_, nullptr, nullptr);
    CleanupOldLogFilesW(dir, base, g_log_path_w);
    delete[] module_path; delete[] dir; delete[] base;

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

// 日志级别（常见五级）。MinLevel 以下不落盘；DisableLog 仍最高优先。
enum LogLevel {
    LOG_TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO  = 2,
    LOG_WARN  = 3,
    LOG_ERROR = 4,
};
static int g_log_level = LOG_INFO; // 由 [Logging] MinLevel 覆盖（默认 info）

// 级别名（写日志行前缀用，固定小写）。
static const char* LogLevelName_(int level)
{
    switch (level) {
    case LOG_TRACE: return "trace";
    case LOG_DEBUG: return "debug";
    case LOG_INFO:  return "info";
    case LOG_WARN:  return "warn";
    default:        return "error";
    }
}

// 级别名解析（trace/debug/info/warning/warn/error，不区分大小写；坏值回 info）。
static int ParseLogLevel_(const char* name)
{
    if (!name || !name[0]) return LOG_INFO;
    if (_stricmp(name, "trace") == 0) return LOG_TRACE;
    if (_stricmp(name, "debug") == 0) return LOG_DEBUG;
    if (_stricmp(name, "info") == 0) return LOG_INFO;
    if (_stricmp(name, "warn") == 0 || _stricmp(name, "warning") == 0) return LOG_WARN;
    if (_stricmp(name, "error") == 0) return LOG_ERROR;
    return LOG_INFO;
}

// 分级日志前向声明（定义在本文件后部；ReadConfig 等早期调用点使用）。
static void LogTrace(const char* fmt, ...);
static void LogDebug(const char* fmt, ...);
static void LogInfo(const char* fmt, ...);
static void LogWarn(const char* fmt, ...);
static void LogError(const char* fmt, ...);

static bool IsAllowedOneShotVk_(int vk)
{
    return (vk >= 'A' && vk <= 'Z' && vk != 'E');
}

// 打开设置面板键（S8）：单个字母，排除原版战斗键 A C D E H I L O Q R S T W
// 与本插件已用键（F9 启停、J 单次接管、1-0 施法）。
static bool IsAllowedOpenPanelVk_(int vk)
{
    if (vk < 'A' || vk > 'Z') return false;
    switch (vk) {
    case 'A': case 'C': case 'D': case 'E': case 'H': case 'I':
    case 'L': case 'O': case 'Q': case 'R': case 'S': case 'T':
    case 'W': case 'J':
        return false;
    default:
        return true;
    }
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

// 方案存档：DLL 同目录，每个编号一个独立文件（H3Auto.profiles1.ini .. profiles5.ini，
// 文本一行，格式见 PolicyCore H3AP4）。
// 保存/加载的是面板草稿，不改变当前生效方案，也不暂停自动执行。
// g_profiles_prefix 在 Entry 的 DllMain 里初始化。

// 成功返回 true。文件不存在或内容损坏返回 false（草稿保持原样）。
// SEH 保护只能包纯 C 代码，所以编解码与写文件单独成函数。
static bool SaveProfileStoreRaw_(const int army_types[21],
    const int army_counts[21], const AutoStackRule rules[21],
    uint8_t strategy, uint16_t stop_turns, int slot)
{
    char* text = new char[32 * 1024];
    const int n = H3AutoPolicy::EncodeProfileStoreText(army_types,
        army_counts, strategy, rules, stop_turns, text, 32 * 1024);
    bool ok = false;
    if (n > 0) {
        char* path = new(std::nothrow) char[kPathCap_];
        if (!path) { delete[] text; return false; }
        ProfileSlotPath(slot, path, kPathCap_);
        FILE* fp = nullptr;
        wchar_t* wpath = Utf8ToWideAlloc_(path);
        delete[] path;
        if (wpath && _wfopen_s(&fp, wpath, L"wb") == 0 && fp) {
            ok = fwrite(text, 1, n, fp) == static_cast<size_t>(n);
            fclose(fp);
        }
        delete[] wpath;
    }
    delete[] text;
    return ok;
}

static bool SaveProfileStore_(const int army_types[21],
    const int army_counts[21], const AutoStackRule rules[21],
    uint8_t strategy, uint16_t stop_turns, int slot)
{
    bool ok = false;
    DWORD code = 0;
    void* fault = nullptr;
    __try {
        ok = SaveProfileStoreRaw_(army_types, army_counts, rules,
            strategy, stop_turns, slot);
    } __except (code = GetExceptionCode(),
                fault = (GetExceptionInformation())->ExceptionRecord->ExceptionAddress,
                EXCEPTION_EXECUTE_HANDLER) {
        LogDebug("[Panel] 保存方案时发生异常 code=0x%08X at=%p", code, fault);
        ok = false;
    }
    return ok;
}

// 读档：读选中编号的独立文件（槽号 0..4，越界取 0）。
static bool LoadProfileStore_(int army_types[21], int army_counts[21],
    AutoStackRule out_rules[21], uint8_t* strategy, uint16_t* stop_turns,
    int slot)
{
    char* path = new(std::nothrow) char[kPathCap_];
    if (!path) return false;
    ProfileSlotPath(slot, path, kPathCap_);
    FILE* fp = nullptr;
    wchar_t* wpath = Utf8ToWideAlloc_(path);
    delete[] path;
    if (!wpath) return false;
    if (_wfopen_s(&fp, wpath, L"rb") != 0 || !fp) { delete[] wpath; return false; }
    delete[] wpath;
    char* text = new char[32 * 1024];
    const size_t n = fread(text, 1, 32 * 1024 - 1, fp);
    const int truncated = fgetc(fp) != EOF;
    fclose(fp);
    bool ok = false;
    if (n > 0 && !truncated) {
        text[n] = 0;
        ok = H3AutoPolicy::DecodeProfileStoreText(text, army_types,
            army_counts, strategy, out_rules, stop_turns);
    }
    delete[] text;
    return ok;
}

static void ReadConfig()
{
    const char* f = g_ini_path;
    cfg.disable_on_start = IniReadIntUtf8(f, "General", "DisableOnStart", 0);
    cfg.disable_on_start = ClampInt(cfg.disable_on_start, 0, 1);

    // 日志级别：[Logging] MinLevel（trace/debug/info/warn/error，坏值回 info）。
    {
        char lv[16] = {};
        IniReadUtf8(f, "Logging", "MinLevel", "info", lv, sizeof(lv));
        g_log_level = ParseLogLevel_(lv);
    }

    // 上次存/读档的槽位（面板自动选中该编号，不自动读档）。
    // 从独立文件 H3Auto.last.ini 读（一行数字 1..5），无文件/坏值默认 1。
    {
        int last = 1;
        FILE* fp = nullptr;
        wchar_t* wpath = Utf8ToWideAlloc_(g_last_profile_path);
        if (wpath && _wfopen_s(&fp, wpath, L"rb") == 0 && fp) {
            char buf[8] = {};
            const size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
            fclose(fp);
            if (n > 0) {
                buf[n] = 0;
                last = atoi(buf);
            }
        }
        delete[] wpath;
        g_last_profile = ClampInt(last, 1, 5) - 1;
    }

    char toggle_buf[64] = {};
    char oneshot_buf[64] = {};
    char openpanel_buf[64] = {};
    IniReadUtf8(f, "Hotkeys", "ToggleManual", "F9",
        toggle_buf, sizeof(toggle_buf));
    IniReadUtf8(f, "Hotkeys", "OneShotManual", "J",
        oneshot_buf, sizeof(oneshot_buf));
    IniReadUtf8(f, "Hotkeys", "OpenSettings", "P",
        openpanel_buf, sizeof(openpanel_buf));
    cfg.toggle_manual_vk = ParseHotkeyVk_(toggle_buf, VK_F9, false);
    cfg.one_shot_manual_vk = ParseHotkeyVk_(oneshot_buf, 'J', true);
    if (!IsAllowedOneShotVk_(cfg.one_shot_manual_vk)) {
        LogWarn("配置警告：OneShotManual 只允许单个字母，已回退到 J");
        cfg.one_shot_manual_vk = 'J';
    }
    cfg.open_settings_vk = ParseHotkeyVk_(openpanel_buf, 'P', true);
    if (!IsAllowedOpenPanelVk_(cfg.open_settings_vk)
        || cfg.open_settings_vk == cfg.one_shot_manual_vk) {
        LogWarn("配置警告：OpenSettings 键位非法或与 OneShotManual 冲突，已回退到 P");
        cfg.open_settings_vk = 'P';
    }

    LogInfo("配置加载：DisableOnStart=%d ToggleManual=0x%X OneShotManual=0x%X OpenSettings=0x%X",
        cfg.disable_on_start, cfg.toggle_manual_vk, cfg.one_shot_manual_vk,
        cfg.open_settings_vk);
}

static const char* HotkeyDisplayName_(int vk, char* buf, int buf_size)
{
    if (!buf || buf_size <= 0) return "";
    if (vk >= VK_F1 && vk <= VK_F12) {
        snprintf(buf, buf_size, "F%d", vk - VK_F1 + 1);
        return buf;
    }
    if (vk >= 'A' && vk <= 'Z') {
        snprintf(buf, buf_size, "%c", vk);
        return buf;
    }
    if (vk >= '0' && vk <= '9') {
        snprintf(buf, buf_size, "%c", vk);
        return buf;
    }
    if (vk == VK_LCONTROL) return "左Ctrl";
    if (vk == VK_RCONTROL) return "右Ctrl";
    if (vk == VK_CONTROL) return "Ctrl";
    snprintf(buf, buf_size, "0x%X", vk);
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

static void WriteLogLv(int level, const char* fmt, ...)
{
    if (g_disable_log) return;
    if (level < g_log_level) return;
    char line[1024];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int off = _snprintf(line, sizeof(line) - 1, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%s] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        LogLevelName_(level));
    if (off < 0) off = 0;
    if (off >= (int)sizeof(line)) off = (int)sizeof(line) - 1;

    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line + off, sizeof(line) - off - 1, fmt, ap);
    va_end(ap);
    line[sizeof(line) - 1] = 0;
    AppendUtf8LogLine(line);
}

// 分级入口：配置/状态=INFO，匹配与执行细节=DEBUG，配置回退=WARN，异常/初始化失败=ERROR。
// 统一先把格式化结果落到本地缓冲，再经 WriteLogLv 补时间戳/级别前缀（避免 va_list 转发）。
#define H3AUTO_LOG_BODY_(level)                                          \
    do {                                                                 \
        if (g_disable_log || (level) < g_log_level) break;               \
        char line[1024];                                                 \
        va_list ap; va_start(ap, fmt);                                   \
        _vsnprintf(line, sizeof(line) - 1, fmt, ap);                     \
        va_end(ap);                                                      \
        line[sizeof(line) - 1] = 0;                                      \
        WriteLogLv(level, "%s", line);                                   \
    } while (0)

static void LogTrace(const char* fmt, ...) { H3AUTO_LOG_BODY_(LOG_TRACE); }
static void LogDebug(const char* fmt, ...) { H3AUTO_LOG_BODY_(LOG_DEBUG); }
static void LogInfo(const char* fmt, ...)  { H3AUTO_LOG_BODY_(LOG_INFO); }
static void LogWarn(const char* fmt, ...)  { H3AUTO_LOG_BODY_(LOG_WARN); }
static void LogError(const char* fmt, ...) { H3AUTO_LOG_BODY_(LOG_ERROR); }
