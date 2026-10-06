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
// 保活方式（protectMode，三选一，默认不保活）在 AutoStackRule 内随方案走。
AutoStackRule g_profiles[5][21] = {};
int g_active_profile = 0;

// 当前生效方案（运行时视图 = g_profiles[g_active_profile]）
AutoStackRule g_active_rules[21] = {};

// 上面的 = {} 是全零而非 MakeDefaultRule_() 默认（剩≤阈值 2 等）：
// 启动后、首次 ClearConfirmedProfiles / 存读档之前打开面板，「剩≤」
// 会显示 0 且点勾号后固化。同 TU 内定义顺序保证本初始化器在两个数组
// 之后执行（进入任何钩子前），回填与 ClearConfirmedProfiles 同款默认。
namespace {
struct DefaultRulesInit_ {
    DefaultRulesInit_()
    {
        const AutoStackRule def = MakeDefaultRule_();
        for (int p = 0; p < 5; ++p)
            for (int s = 0; s < 21; ++s)
                g_profiles[p][s] = def;
        for (int s = 0; s < 21; ++s)
            g_active_rules[s] = def;
    }
} g_default_rules_init_;
}

// 上次勾号生效的槽位（0..4），user.ini [General] LastProfile 持久化；进面板自动选中，
// 不自动读档。跨战斗保留。
int g_last_profile = 0;

// 保活与召唤是同一条施法通道（保活优先，召唤兜底），无方案级通道选择。
// 保活按队三选一（不保活/剩余数量/损失量，默认不保活）挂在每条规则上
// （protectMode），随方案走；召唤是否启用在 g_summon[p].enabled（默认不启用）。
uint16_t g_stop_turns[5] = { H3AutoPolicy::DEFAULT_STOP_TURNS,
    H3AutoPolicy::DEFAULT_STOP_TURNS, H3AutoPolicy::DEFAULT_STOP_TURNS,
    H3AutoPolicy::DEFAULT_STOP_TURNS, H3AutoPolicy::DEFAULT_STOP_TURNS }; // 0=关闭，0..999

// 召唤通道配置（方案级，随 H3AP9 方案存档）：enabled=是否启用召唤兜底；
// 阈值/法术选择/召唤物共享规则在此；stop_enemy_mana+stop_mana_th 是自动停止
// 第二条件（勾选后敌方英雄魔力 ≤ 阈值（默认 6，0..32767）时整场切回手动，
// 与 g_stop_turns OR 组合）。
StatusProfileFields g_status[5] = {
    H3AutoPolicy::MakeDefaultStatusFields(),
    H3AutoPolicy::MakeDefaultStatusFields(),
    H3AutoPolicy::MakeDefaultStatusFields(),
    H3AutoPolicy::MakeDefaultStatusFields(),
    H3AutoPolicy::MakeDefaultStatusFields(),
};

ForceFieldProfileFields g_forcefield[5] = {
    H3AutoPolicy::MakeDefaultForceFieldFields(),
    H3AutoPolicy::MakeDefaultForceFieldFields(),
    H3AutoPolicy::MakeDefaultForceFieldFields(),
    H3AutoPolicy::MakeDefaultForceFieldFields(),
    H3AutoPolicy::MakeDefaultForceFieldFields(),
};

SummonProfileFields g_summon[5] = {
    H3AutoPolicy::MakeDefaultSummonFields(),
    H3AutoPolicy::MakeDefaultSummonFields(),
    H3AutoPolicy::MakeDefaultSummonFields(),
    H3AutoPolicy::MakeDefaultSummonFields(),
    H3AutoPolicy::MakeDefaultSummonFields(),
};

// 玩家接受战斗结果后清空 5 套方案（取消/重打不调用）。
// 日志在调用方 OnBattleResultAccepted 打印，避免依赖本文件后部 WriteLog。
// g_last_profile 不清：下次进面板仍选中最后存/读档编号。
// 面板草稿（SettingsDlg 的 ResetPanelDrafts，前向声明如下）同步清空：
// 生效方案与草稿不一致会让下一场「确定」复活上一场部队的旧规则。
void ResetPanelDrafts();
void ClearConfirmedProfiles()
{
    const AutoStackRule def = MakeDefaultRule_();
    const SummonProfileFields summon_def = H3AutoPolicy::MakeDefaultSummonFields();
    const StatusProfileFields status_def = H3AutoPolicy::MakeDefaultStatusFields();
    const ForceFieldProfileFields forcefield_def =
        H3AutoPolicy::MakeDefaultForceFieldFields();
    for (int p = 0; p < 5; ++p) {
        for (int s = 0; s < 21; ++s)
            g_profiles[p][s] = def;
        g_stop_turns[p] = H3AutoPolicy::DEFAULT_STOP_TURNS;
        g_summon[p] = summon_def;
        g_status[p] = status_def;
        g_forcefield[p] = forcefield_def;
    }
    g_active_profile = 0;
    for (int s = 0; s < 21; ++s)
        g_active_rules[s] = def;
    ResetPanelDrafts();
}

static struct Config {
    int  disable_on_start;     // 0=不禁用（默认启用），1=禁用
    int  toggle_manual_vk;     // F9：本场自动/全手动切换
    int  one_shot_manual_vk;   // J：单次接管当前/下一支部队
    int  open_settings_vk;     // P：战斗中打开设置面板（§10.1）
} cfg;

// 常驻路径：堆上 4MB，DLL 加载时分配一次。目录可能很深，MAX_PATH 不够。
// 配置分两层（同目录）：default = 出厂默认随包分发；user = 玩家改动层。
static char* g_ini_path = new char[kPathCap_];        // H3Auto.default.ini
static char* g_user_ini_path = new char[kPathCap_](); // H3Auto.user.ini（可不存在）
static char* g_log_path = new char[kPathCap_];
static char* g_profiles_prefix = new char[kPathCap_]; // 每槽一文件：前缀 + 编号（1..5）
static wchar_t* g_log_path_w = new wchar_t[kPathCap_ / 2];

// 分层读取：先默认层 H3Auto.default.ini，再叠加玩家层 H3Auto.user.ini
//（键存在且非空才覆盖）。两层都未命中 → fallback。
static bool IniReadUtf8Layered(const char* section, const char* key,
    const char* fallback, char* out, int out_size)
{
    const bool hit_default =
        IniReadUtf8(g_ini_path, section, key, fallback, out, out_size);
    char user_v[512] = {};
    if (IniReadUtf8(g_user_ini_path, section, key, "", user_v,
            (int)sizeof(user_v))
        && user_v[0]) {
        const int n = (int)strlen(user_v) < out_size - 1
            ? (int)strlen(user_v) : out_size - 1;
        memcpy(out, user_v, n);
        out[n] = 0;
        return true;
    }
    return hit_default;
}

static int IniReadIntUtf8Layered(const char* section, const char* key,
    int fallback)
{
    char buf[32] = {};
    IniReadUtf8Layered(section, key, "", buf, (int)sizeof(buf));
    return buf[0] ? atoi(buf) : fallback;
}

// 勾号生效时记忆槽位：更新内存值并写 H3Auto.user.ini [General] LastProfile（玩家层）。
void RememberProfileSlot(int slot)
{
    if (slot < 0 || slot >= 5) return;
    g_last_profile = slot;
    char buf[8] = {};
    _snprintf(buf, sizeof(buf) - 1, "%d", slot + 1);
    IniWriteKeyUtf8(g_user_ini_path, "General", "LastProfile", buf);
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

static bool ReadDisableLogFromIniFiles()
{
    // 启动早期（写第一条日志前）：默认层 + 玩家层叠加。
    return IniReadIntUtf8Layered("Logging", "DisableLog", 0) != 0;
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

// ======================================================================
// 战斗存档库（§17 智能存读档）：<游戏根>\Games\<地图名>\Auto\ 下，每场
// 战斗（指纹）一个文件 <16位十六进制指纹>.json，同场多次「确定」各存
// 一条，按时间升序排列，只保留最近 kBattlesKeep_ 条（旧的丢弃）。JSON：
//   {"version":4,"battle":"<hex16>","entries":[
//     {"time":"...","active":1..5,"profiles":[5 个方案对象]}]}
// 方案对象按字段展开：stopTurns、summon、army[]、status、forcefield。
// forcefield.anchor_hex 必须是两整数数组；只接受 v4 和对应战斗指纹。
// 数组只写实际数量。旧版本与 H3AP 数字串一律拒绝，视为没有存档。
// 跨文件再按 mtime LRU 保留最近 kBattlesKeep_ 场。
//
// 目录规则（2026-10-06 对齐 H3BattleStore）：地图名读 HD 游戏名链
//   *(char**)(*(DWORD*)0x699538 + 0x1fb40)（热座=玩家输入、遭遇战=地图名；
//   NULL/空白 → "Unnamed"；字符清洗同 HD FUN_010e2e80：拒绝 ../: 、
//   控制字符与 <>"|?*），每次现读现拼不缓存（用户拍板）。链不可读退
//   Games\Auto（不带地图子目录）；找不到 Games 根退 DLL 同目录（几乎
//   不可达兜底）。旧位置（DLL 同目录）的旧文件不迁移：读不到=无存档。
// ======================================================================
static const int kBattlesKeep_ = 30;

// DLL 所在目录（UTF-8，末尾不带反斜杠）。g_profiles_prefix 是
// <目录>\H3Auto.profiles 前缀，截掉文件名即目录。
static void DllDirUtf8_(char* out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    char* dir = new(std::nothrow) char[kPathCap_]();
    if (!dir) return;
    strncpy(dir, g_profiles_prefix, kPathCap_ - 1);
    dir[kPathCap_ - 1] = 0;
    char* slash = strrchr(dir, '\\');
    if (slash) *slash = 0; else dir[0] = 0;
    if (dir[0]) {
        strncpy(out, dir, cap - 1);
        out[cap - 1] = 0;
    }
    delete[] dir;
}

// 从 DLL 目录逐级向上找名为 Games 的目录（最多 8 级），输出 UTF-8 路径。
// HD 的原版存档也在 <游戏根>\Games（UI.Ext.ScenarioMgr.Folders=1 时按
// 地图名分子目录），沿用同一棵树。宽字符探测：路径含中文。
static bool FindGamesRootUtf8_(char* out, int cap)
{
    if (!out || cap <= 0) return false;
    out[0] = 0;
    wchar_t* wpath = new(std::nothrow) wchar_t[kPathCap_ / 2]();
    wchar_t* candidate = new(std::nothrow) wchar_t[kPathCap_ / 2]();
    if (!wpath || !candidate) { delete[] wpath; delete[] candidate; return false; }
    bool ok = false;
    if (GetModuleFileNameW(g_hModule, wpath, kPathCap_ / 2)) {
        for (int level = 0; level < 8 && !ok; ++level) {
            wchar_t* slash = wcsrchr(wpath, L'\\');
            if (!slash) slash = wcsrchr(wpath, L'/');
            if (!slash || slash == wpath) break;
            *slash = 0;
            _snwprintf(candidate, kPathCap_ / 2, L"%s\\Games", wpath);
            candidate[kPathCap_ / 2 - 1] = 0;
            const DWORD attrs = GetFileAttributesW(candidate);
            if (attrs != INVALID_FILE_ATTRIBUTES
                && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                const int chars = WideCharToMultiByte(CP_UTF8, 0,
                    candidate, -1, out, cap - 1, nullptr, nullptr);
                if (chars > 0) { out[chars] = 0; ok = true; }
            }
        }
    }
    delete[] wpath;
    delete[] candidate;
    return ok;
}

// 游戏名链（HD 语义）→ 存档子目录名（UTF-8）。返回 true=可信；
// false=链不可读（调用方退回不带地图名的 Games\Auto）。
// 规则与 H3BattleStore Entry.inc.cpp ReadSaveFolderName_ 同源。
static bool ReadSaveFolderNameUtf8_(char* out, int cap)
{
    if (!out || cap <= 0) return false;
    out[0] = 0;
    if (IsBadReadPtr((void*)0x699538, 4)) return false;
    const DWORD base = *(DWORD*)0x699538;
    if (!base || IsBadReadPtr((void*)(base + 0x1fb40), 4)) return false;
    const char* text = *(const char**)(base + 0x1fb40);
    if (!text) {
        strncpy(out, "Unnamed", cap - 1);
        out[cap - 1] = 0;
        return true;
    }
    if (IsBadStringPtrA(text, 260)) return false;
    char ansi[260];
    lstrcpynA(ansi, text, sizeof(ansi));
    // 去首尾空白
    char* end = ansi + strlen(ansi);
    while (end > ansi && (unsigned char)end[-1] <= ' ') *--end = 0;
    const char* begin = ansi;
    while (*begin && (unsigned char)*begin <= ' ') ++begin;
    if (!*begin) {
        strncpy(out, "Unnamed", cap - 1);
        out[cap - 1] = 0;
        return true;
    }
    if (strstr(begin, "..") || strchr(begin, ':')) return false;
    for (const char* c = begin; *c; ++c) {
        const unsigned char uc = (unsigned char)*c;
        if (uc < 0x20 || strchr("<>\"|?*", *c)) return false;
    }
    wchar_t wide[130];
    const int wchars = MultiByteToWideChar(CP_ACP, 0, begin, -1, wide, 129);
    if (wchars <= 1) return false;
    const int chars = WideCharToMultiByte(CP_UTF8, 0, wide, wchars, out,
        cap - 1, nullptr, nullptr);
    if (chars <= 1) return false;
    out[chars] = 0;
    return true;
}

// 战斗存档目录（UTF-8，末尾不带反斜杠）：
// <游戏根>\Games\<地图名>\Auto；链不可读 → Games\Auto；无 Games 根 →
// DLL 同目录（旧位置兜底）。
static void BattleStoreDirUtf8_(char* out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    char* games = new(std::nothrow) char[kPathCap_]();
    char* folder = new(std::nothrow) char[520]();
    if (!games || !folder || !FindGamesRootUtf8_(games, kPathCap_)) {
        delete[] games;
        delete[] folder;
        DllDirUtf8_(out, cap);
        return;
    }
    if (ReadSaveFolderNameUtf8_(folder, 520)) {
        _snprintf(out, cap, "%s\\%s\\Auto", games, folder);
    } else {
        static bool warned = false;
        if (!warned) {
            LogWarn("[BattleStore] 游戏名链(*(DWORD*)0x699538 + 0x1fb40)不可读，使用 Games\\Auto");
            warned = true;
        }
        _snprintf(out, cap, "%s\\Auto", games);
    }
    out[cap - 1] = 0;
    delete[] games;
    delete[] folder;
}

// 逐级建目录（已存在/失败忽略，尽力而为）。utf8_dir 末尾不带反斜杠。
// Games\<地图名> 通常已由 HD 存档创建，Auto 子目录一般要新建。
static void EnsureDirectoryUtf8_(const char* utf8_dir)
{
    if (!utf8_dir || !utf8_dir[0]) return;
    wchar_t* wide = Utf8ToWideAlloc_(utf8_dir);
    if (!wide) return;
    for (wchar_t* p = wide + 1; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            const wchar_t saved = *p;
            *p = 0;
            CreateDirectoryW(wide, nullptr); // 失败=已存在/不可建，忽略
            *p = saved;
        }
    }
    CreateDirectoryW(wide, nullptr);
    delete[] wide;
}

static void BattleStorePath_(unsigned long long fp, char* out, int cap)
{
    // 目录缓冲堆分配：kPathCap_ = 4MB，放栈上必炸游戏线程（~1MB）。
    char* dir = new(std::nothrow) char[kPathCap_]();
    if (!dir || !out || cap <= 0) {
        if (out && cap > 0) out[0] = 0;
        delete[] dir;
        return;
    }
    BattleStoreDirUtf8_(dir, kPathCap_);
    EnsureDirectoryUtf8_(dir);
    _snprintf(out, cap, "%s\\%016llX.json", dir, fp);
    out[cap - 1] = 0;
    delete[] dir;
}

static void NowStamp_(char* out, int cap)
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    _snprintf(out, cap, "%04d%02d%02d-%02d%02d%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (cap > 0) out[cap - 1] = 0;
}

// 读文件全文（UTF-8/ASCII）；失败返回 nullptr。
static char* ReadTextFileAlloc_(const char* path)
{
    FILE* fpf = nullptr;
    wchar_t* wpath = Utf8ToWideAlloc_(path);
    if (!wpath) return nullptr;
    if (_wfopen_s(&fpf, wpath, L"rb") != 0 || !fpf) { delete[] wpath; return nullptr; }
    delete[] wpath;
    fseek(fpf, 0, SEEK_END);
    const long size = ftell(fpf);
    fseek(fpf, 0, SEEK_SET);
    if (size <= 0 || size > 8 * 1024 * 1024) { fclose(fpf); return nullptr; }
    char* text = new char[size + 1];
    const size_t n = fread(text, 1, static_cast<size_t>(size), fpf);
    fclose(fpf);
    text[n] = 0;
    return text;
}

static bool WriteTextFile_(const char* path, const char* text, int len)
{
    FILE* fpf = nullptr;
    wchar_t* wpath = Utf8ToWideAlloc_(path);
    if (!wpath) return false;
    bool ok = false;
    if (_wfopen_s(&fpf, wpath, L"wb") == 0 && fpf) {
        ok = fwrite(text, 1, static_cast<size_t>(len), fpf)
            == static_cast<size_t>(len);
        fclose(fpf);
    }
    delete[] wpath;
    return ok;
}

// JSON 使用 nlohmann/json 3.11.3（单头文件，MIT）。
// 游戏线程用 SEH，库侧关闭 C++ 异常：解析失败返回 discarded。
#define JSON_NOEXCEPTION 1
#include <nlohmann/json.hpp>
using BattleJson = nlohmann::json;

static int JsonInt_(const BattleJson& obj, const char* key, int fallback)
{
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_number_integer())
        return fallback;
    return obj[key].get<int>();
}

static bool ParseRuleJson_(const BattleJson& obj, AutoStackRule* rule)
{
    if (!obj.is_object() || !rule) return false;
    AutoStackRule r = MakeDefaultRule();
    const int action = JsonInt_(obj, "action", -1);
    if (action < AA_MANUAL || action >= AA_COUNT) return false;
    r.action = static_cast<AutoActionKind>(action);
    const int kind = JsonInt_(obj, "kind", (int)r.target.kind);
    if (kind >= AT_NONE && kind < AT_COUNT)
        r.target.kind = static_cast<AutoTargetKind>(kind);
    const int side = JsonInt_(obj, "side", (int)r.target.side);
    if (side >= ATS_OWN && side < ATS_COUNT)
        r.target.side = static_cast<AutoTargetSide>(side);
    const int selector = JsonInt_(obj, "selector", (int)r.target.selector);
    if (selector >= SEL_RANDOM && selector < SEL_COUNT)
        r.target.selector = static_cast<AutoTargetSelector>(selector);
    if (obj.contains("waypoints") && obj["waypoints"].is_array()) {
        int count = 0;
        for (const BattleJson& item : obj["waypoints"]) {
            if (!item.is_number_integer() || count >= MOVE_WAYPOINT_CAPACITY)
                break;
            r.target.moveWaypoints[count++] =
                static_cast<int16_t>(item.get<int>());
        }
        r.target.moveWaypointCount = static_cast<int8_t>(count);
    }
    if (obj.contains("melee") && obj["melee"].is_array()) {
        int count = 0;
        for (const BattleJson& pair : obj["melee"]) {
            if (!pair.is_array() || pair.size() < 2 || count >= MELEE_PAIR_CAPACITY)
                break;
            if (!pair[0].is_number_integer() || !pair[1].is_number_integer())
                break;
            r.target.meleeStandHexes[count] =
                static_cast<int16_t>(pair[0].get<int>());
            r.target.meleeAttackHexes[count] =
                static_cast<int16_t>(pair[1].get<int>());
            ++count;
        }
        r.target.meleePairCount = static_cast<int8_t>(count);
        if (count > 0) {
            r.target.meleeStandHex = r.target.meleeStandHexes[0];
            r.target.meleeAttackHex = r.target.meleeAttackHexes[0];
        }
    }
    r.allowDefendFallback = JsonInt_(obj, "fallback", 0) != 0;
    if (obj.contains("spells") && obj["spells"].is_array()) {
        int count = 0;
        for (const BattleJson& item : obj["spells"]) {
            if (!item.is_number_integer() || count >= SPELL_SLOT_CAPACITY)
                break;
            r.spellSlots[count++] = static_cast<int8_t>(item.get<int>());
        }
        r.spellSlotCount = static_cast<int8_t>(count);
    }
    const int mode = JsonInt_(obj, "protectMode", (int)PM_NONE);
    r.protectMode = (mode == (int)PM_COUNT_BELOW
            || mode == (int)PM_LOSS_GT_RESTORE)
        ? static_cast<ProtectMode>(mode) : PM_NONE;
    const int protect_count = JsonInt_(obj, "protectCount", r.protectCountBelow);
    r.protectCountBelow = protect_count < 0 ? 0 : protect_count;
    *rule = r;
    return true;
}

static BattleJson RuleToJson_(const AutoStackRule& rule)
{
    BattleJson obj = {
        {"action", (int)rule.action},
        {"kind", (int)rule.target.kind},
        {"side", (int)rule.target.side},
        {"selector", (int)rule.target.selector},
        {"fallback", rule.allowDefendFallback ? 1 : 0},
        {"protectMode", (int)rule.protectMode},
        {"protectCount", rule.protectCountBelow},
    };
    if (rule.target.moveWaypointCount > 0) {
        BattleJson waypoints = BattleJson::array();
        for (int i = 0; i < rule.target.moveWaypointCount; ++i)
            waypoints.push_back((int)rule.target.moveWaypoints[i]);
        obj["waypoints"] = waypoints;
    }
    if (rule.target.meleePairCount > 0) {
        BattleJson pairs = BattleJson::array();
        for (int i = 0; i < rule.target.meleePairCount; ++i)
            pairs.push_back(BattleJson::array({
                (int)rule.target.meleeStandHexes[i],
                (int)rule.target.meleeAttackHexes[i]}));
        obj["melee"] = pairs;
    }
    if (rule.spellSlotCount > 0) {
        BattleJson spells = BattleJson::array();
        for (int i = 0; i < rule.spellSlotCount; ++i)
            spells.push_back((int)rule.spellSlots[i]);
        obj["spells"] = spells;
    }
    return obj;
}

static bool ParseStatusJson_(const BattleJson& obj, StatusProfileFields* status)
{
    if (!status) return false;
    *status = MakeDefaultStatusFields();
    if (!obj.is_object()) return true;
    status->slow = JsonInt_(obj, "slow", 0) != 0;
    // 补状态阈值：缺省（旧记录）= 默认 1，坏数据回默认（与 slow/buffs
    // 同为宽容读；缺省值与旧行为完全一致）。
    const int refresh = JsonInt_(obj, "refreshTurns",
        H3AutoPolicy::kStatusRefreshTurns);
    status->refresh_turns = (refresh >= 1
        && refresh <= H3AutoPolicy::kStatusRefreshTurnsMax)
        ? refresh : H3AutoPolicy::kStatusRefreshTurns;
    if (!obj.contains("buffs") || !obj["buffs"].is_array()) return true;
    int count = 0;
    for (const BattleJson& item : obj["buffs"]) {
        if (!item.is_number_integer() || count >= kStatusSlotCapacity) break;
        const int spell = item.get<int>();
        if (spell <= 0 || spell >= 81) continue;
        status->slots[count++] = spell;
    }
    status->slot_count = count;
    return true;
}

static BattleJson StatusToJson_(const StatusProfileFields& status)
{
    BattleJson buffs = BattleJson::array();
    for (int i = 0; i < status.slot_count && i < kStatusSlotCapacity; ++i)
        if (status.slots[i] > 0) buffs.push_back(status.slots[i]);
    return {{"buffs", buffs}, {"slow", status.slow ? 1 : 0},
        {"refreshTurns", status.refresh_turns}};
}

static bool ParseForceFieldJson_(const BattleJson& obj,
    ForceFieldProfileFields* forcefield)
{
    if (!forcefield || !obj.is_object() || obj.size() != 1
        || !obj.contains("anchor_hex") || !obj["anchor_hex"].is_array()
        || obj["anchor_hex"].size() != 2)
        return false;
    const BattleJson& anchors = obj["anchor_hex"];
    ForceFieldProfileFields fields = MakeDefaultForceFieldFields();
    for (int i = 0; i < 2; ++i) {
        if (!anchors[i].is_number_integer()) return false;
        if (anchors[i].is_number_unsigned()) {
            const unsigned long long value = anchors[i].get<unsigned long long>();
            fields.anchor_hex[i] = value <= 185 ? static_cast<int>(value) : -1;
        } else {
            const long long value = anchors[i].get<long long>();
            fields.anchor_hex[i] = value >= 1 && value <= 185
                ? static_cast<int>(value) : -1;
        }
    }
    NormalizeForceFieldFields(&fields);
    *forcefield = fields;
    return true;
}

static BattleJson ForceFieldToJson_(const ForceFieldProfileFields& forcefield)
{
    ForceFieldProfileFields fields = forcefield;
    NormalizeForceFieldFields(&fields);
    return {{"anchor_hex", BattleJson::array({fields.anchor_hex[0], fields.anchor_hex[1]})}};
}

static bool ParseProfileJson_(const BattleJson& obj, AutoStackRule rules[21],
    uint16_t* stop_turns, SummonProfileFields* summon,
    StatusProfileFields* status, ForceFieldProfileFields* forcefield)
{
    if (!obj.is_object() || !rules || !stop_turns || !summon || !forcefield)
        return false;
    if (!obj.contains("forcefield")
        || !ParseForceFieldJson_(obj["forcefield"], forcefield))
        return false;
    const int turns = JsonInt_(obj, "stopTurns", -1);
    if (turns < 0 || turns > 999) return false;
    *stop_turns = static_cast<uint16_t>(turns);
    if (!obj.contains("summon") || !obj["summon"].is_object()) return false;
    const BattleJson& s = obj["summon"];
    *summon = MakeDefaultSummonFields();
    const int enabled = JsonInt_(s, "enabled", -1);
    const int count = JsonInt_(s, "count", -1);
    const int hp = JsonInt_(s, "hp", -1);
    const int spell = JsonInt_(s, "spell", -1);
    const int combine = JsonInt_(s, "combine", -1);
    const int stop_mana = JsonInt_(s, "stopEnemyMana", -1);
    const int mana = JsonInt_(s, "mana", -1);
    if (enabled < 0 || enabled > 1 || count < 2 || count > 21 || hp < 0
        || spell < 0 || spell > SUMMON_ELEMENT_COUNT
        || (combine != SUMMON_COMBINE_AND && combine != SUMMON_COMBINE_OR)
        || stop_mana < 0 || stop_mana > 1 || mana < 0 || mana > 32767)
        return false;
    summon->enabled = enabled;
    summon->count_th = count;
    summon->hp_th = hp;
    summon->spell_pick = spell;
    summon->cond_combine = combine;
    summon->stop_enemy_mana = stop_mana;
    summon->stop_mana_th = mana;
    if (!s.contains("rule") || !ParseRuleJson_(s["rule"], &summon->summon_rule))
        return false;
    NormalizeSummonRule(&summon->summon_rule);
    if (status && obj.contains("status")
        && !ParseStatusJson_(obj["status"], status))
        return false;
    for (int i = 0; i < 21; ++i) rules[i] = MakeDefaultRule();
    if (!obj.contains("army") || !obj["army"].is_array()) return false;
    for (const BattleJson& unit : obj["army"]) {
        if (!unit.is_object()) return false;
        const int slot = JsonInt_(unit, "slot", -1);
        if (slot < 0 || slot >= 21 || !unit.contains("rule")
            || !ParseRuleJson_(unit["rule"], &rules[slot]))
            return false;
    }
    return true;
}

static BattleJson ProfileToJson_(const AutoStackRule rules[21],
    uint16_t stop_turns, const SummonProfileFields& summon,
    const StatusProfileFields& status, const ForceFieldProfileFields& forcefield)
{
    BattleJson army = BattleJson::array();
    for (int slot = 0; slot < 21; ++slot) {
        const AutoStackRule& rule = rules[slot];
        if (rule.action == AA_MANUAL && rule.spellSlotCount == 0
            && rule.protectMode == PM_NONE
            && rule.target.moveWaypointCount == 0
            && rule.target.meleePairCount == 0)
            continue;
        army.push_back({
            {"slot", slot},
            {"rule", RuleToJson_(rule)},
        });
    }
    return {
        {"stopTurns", (int)stop_turns},
        {"summon", {
            {"enabled", summon.enabled ? 1 : 0},
            {"count", summon.count_th},
            {"hp", summon.hp_th},
            {"spell", summon.spell_pick},
            {"combine", summon.cond_combine},
            {"stopEnemyMana", summon.stop_enemy_mana ? 1 : 0},
            {"mana", summon.stop_mana_th},
            {"rule", RuleToJson_(summon.summon_rule)},
        }},
        {"army", army},
        {"status", StatusToJson_(status)},
        {"forcefield", ForceFieldToJson_(forcefield)},
    };
}

static bool ParseRecordJson_(const BattleJson& obj, BattleStoreRecord* rec)
{
    if (!obj.is_object() || !rec) return false;
    if (!obj.contains("time") || !obj["time"].is_string()) return false;
    const std::string time = obj["time"].get<std::string>();
    if (time.size() >= sizeof(rec->time)) return false;
    memcpy(rec->time, time.c_str(), time.size() + 1);
    const int active = JsonInt_(obj, "active", 0) - 1;
    if (active < 0 || active > 4) return false;
    rec->active = active;
    if (!obj.contains("profiles") || !obj["profiles"].is_array()
        || obj["profiles"].size() != 5) return false;
    for (int i = 0; i < 5; ++i) {
        if (!ParseProfileJson_(obj["profiles"][i], rec->rules[i],
                &rec->stop_turns[i], &rec->summon[i], &rec->status[i],
                &rec->forcefield[i]))
            return false;
    }
    return true;
}

// 读整库（升序=旧→新）。返回条数；文件不存在=0；损坏=-1。
static int LoadBattleStoreRaw_(unsigned long long fp,
    BattleStoreRecord* records, int cap)
{
    char* path = new(std::nothrow) char[kPathCap_];
    if (!path) return -1;
    BattleStorePath_(fp, path, kPathCap_);
    char* text = ReadTextFileAlloc_(path);
    delete[] path;
    if (!text) return 0;
    const BattleJson root = BattleJson::parse(text, nullptr, false, true);
    delete[] text;
    if (root.is_discarded() || !root.is_object()
        || !root.contains("version") || !root["version"].is_number_integer()
        || root["version"] != 4
        || !root.contains("battle") || !root["battle"].is_string()
        || !root.contains("entries") || !root["entries"].is_array())
        return -1;
    char expected_battle[17] = {};
    _snprintf(expected_battle, sizeof(expected_battle), "%016llX", fp);
    const std::string battle = root["battle"].get<std::string>();
    if (battle.size() != 16 || _stricmp(battle.c_str(), expected_battle) != 0)
        return -1;
    int count = 0;
    for (const BattleJson& entry : root["entries"]) {
        if (count >= cap) break;
        if (!ParseRecordJson_(entry, &records[count])) return -1;
        ++count;
    }
    return count;
}

// 整库序列化写回（升序）。
static bool SaveBattleStoreRaw_(unsigned long long fp,
    const BattleStoreRecord* records, int count)
{
    char battle[32] = {};
    _snprintf(battle, sizeof(battle), "%016llX", fp);
    BattleJson entries = BattleJson::array();
    for (int i = 0; i < count; ++i) {
        BattleJson profiles = BattleJson::array();
        for (int p = 0; p < 5; ++p)
            profiles.push_back(ProfileToJson_(records[i].rules[p],
                records[i].stop_turns[p], records[i].summon[p],
                records[i].status[p], records[i].forcefield[p]));
        entries.push_back({
            {"time", records[i].time},
            {"active", records[i].active + 1},
            {"profiles", profiles},
        });
    }
    const BattleJson root = {
        {"version", 4},
        {"battle", battle},
        {"entries", entries},
    };
    const std::string text = root.dump(2);
    char* path = new(std::nothrow) char[kPathCap_];
    if (!path) return false;
    BattleStorePath_(fp, path, kPathCap_);
    const bool ok = WriteTextFile_(path, text.c_str(), (int)text.size());
    delete[] path;
    return ok;
}

// 打开面板用：读整库（升序）。成功返回 true（*out_count=条数，0=无档）。
bool LoadBattleStore(unsigned long long fp, BattleStoreRecord* records,
    int cap, int* out_count)
{
    if (!fp || !records || !out_count) return false;
    bool ok = false;
    DWORD code = 0;
    __try {
        const int n = LoadBattleStoreRaw_(fp, records, cap);
        if (n >= 0) { *out_count = n; ok = true; }
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        LogError("[BattleStore] 读战斗存档异常 code=0x%08X", code);
        ok = false;
    }
    if (!ok) *out_count = 0;
    return ok;
}

// 「确定」时追加一条：时间戳取当前；与最后一条内容相同则不新增
// （*skipped_same=true）；超 kBattlesKeep_ 条裁最老。返回是否落盘成功。
bool AppendBattleStoreRecord(unsigned long long fp,
    const AutoStackRule rules[5][21],
    const uint16_t stop_turns[5], const SummonProfileFields summon[5],
    const StatusProfileFields status[5],
    const ForceFieldProfileFields forcefield[5], int active, bool* skipped_same)
{
    if (skipped_same) *skipped_same = false;
    if (!fp) return false;
    bool ok = false;
    DWORD code = 0;
    __try {
        BattleStoreRecord* all = new BattleStoreRecord[kBattlesKeep_ + 1]();
        BattleStoreRecord* rec = new BattleStoreRecord();
        int n = LoadBattleStoreRaw_(fp, all, kBattlesKeep_);
        if (n < 0) n = 0; // 旧档损坏：从头开始重建
        NowStamp_(rec->time, sizeof(rec->time));
        rec->active = active >= 0 && active <= 4 ? active : 0;
        memcpy(rec->rules, rules, sizeof(rec->rules));
        memcpy(rec->stop_turns, stop_turns, sizeof(rec->stop_turns));
        memcpy(rec->summon, summon, sizeof(rec->summon));
        memcpy(rec->status, status, sizeof(rec->status));
        memcpy(rec->forcefield, forcefield, sizeof(rec->forcefield));
        for (int p = 0; p < 5; ++p)
            NormalizeForceFieldFields(&rec->forcefield[p]);
        if (n > 0
            && H3AutoPolicy::BattleStoreRecordContentEquals(all[n - 1], *rec)) {
            if (skipped_same) *skipped_same = true;
            ok = true; // 内容没变：不新增记录
        } else {
            if (n >= kBattlesKeep_) { // 满：丢最老
                memmove(all, all + 1, sizeof(BattleStoreRecord) * (n - 1));
                n = kBattlesKeep_ - 1;
            }
            all[n++] = *rec;
            ok = SaveBattleStoreRaw_(fp, all, n);
        }
        delete[] all;
        delete rec;
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        LogError("[BattleStore] 追加战斗存档异常 code=0x%08X", code);
        ok = false;
    }
    return ok;
}

// 跨文件 LRU：列目录 <16位hex>.json，按 mtime 降序保留前 keep 个。
// 宽字符 API：新目录含中文地图名，UTF-8 字节直接喂 FindFirstFileA 会按
// GBK 误读成乱码路径（旧版在含中文的 DLL 目录里因此静默查不到、从不淘汰）。
static void PruneBattleStore_LRU_(int keep)
{
    char* dir8 = new(std::nothrow) char[kPathCap_]();
    wchar_t* pattern = new(std::nothrow) wchar_t[kPathCap_ / 2]();
    if (!dir8 || !pattern) { delete[] dir8; delete[] pattern; return; }
    BattleStoreDirUtf8_(dir8, kPathCap_);
    wchar_t* dir = dir8[0] ? Utf8ToWideAlloc_(dir8) : nullptr;
    delete[] dir8;
    if (!dir) { delete[] dir; delete[] pattern; return; }
    _snwprintf(pattern, kPathCap_ / 2, L"%s\\*.json", dir);
    pattern[kPathCap_ / 2 - 1] = 0;
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { delete[] dir; delete[] pattern; return; }
    struct Entry { FILETIME t; wchar_t name[MAX_PATH]; };
    Entry* items = new Entry[128];
    int count = 0;
    do {
        // 只认 16 位十六进制文件名（战斗标识），目录里其它 json 不动。
        const wchar_t* nm = fd.cFileName;
        const size_t nl = wcslen(nm);
        bool hex16 = nl == 21 && nm[16] == L'.' && nm[17] == L'j'
            && nm[18] == L's' && nm[19] == L'o' && nm[20] == L'n';
        for (int i = 0; hex16 && i < 16; ++i) {
            const wchar_t c = nm[i];
            const bool okc = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F');
            if (!okc) { hex16 = false; break; }
        }
        if (!hex16 || count >= 128) continue;
        items[count].t = fd.ftLastWriteTime;
        wcsncpy(items[count].name, nm, MAX_PATH - 1);
        items[count].name[MAX_PATH - 1] = 0;
        ++count;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    // 冒泡足够（≤128）：新的在前，删第 keep+1 起的。
    for (int i = 1; i < count; ++i) {
        Entry cur = items[i];
        int j = i - 1;
        while (j >= 0 && (CompareFileTime(&items[j].t, &cur.t) < 0)) {
            items[j + 1] = items[j];
            --j;
        }
        items[j + 1] = cur;
    }
    for (int i = keep; i < count; ++i) {
        wchar_t* full = new(std::nothrow) wchar_t[kPathCap_ / 2]();
        if (!full) continue;
        _snwprintf(full, kPathCap_ / 2, L"%s\\%s", dir, items[i].name);
        full[kPathCap_ / 2 - 1] = 0;
        if (DeleteFileW(full)) {
            char name8[64] = {};
            WideCharToMultiByte(CP_UTF8, 0, items[i].name, -1, name8,
                sizeof(name8) - 1, nullptr, nullptr);
            LogInfo("[BattleStore] LRU 淘汰 %s", name8);
        }
        delete[] full;
    }
    delete[] items;
    delete[] dir;
    delete[] pattern;
}

void PruneBattleStore()
{
    PruneBattleStore_LRU_(kBattlesKeep_);
}

static void ReadConfig()
{
    // 全部键走分层读取：H3Auto.default.ini 打底，H3Auto.user.ini 叠加覆盖。
    cfg.disable_on_start = IniReadIntUtf8Layered("General", "DisableOnStart", 0);
    cfg.disable_on_start = ClampInt(cfg.disable_on_start, 0, 1);

    // 日志级别：[Logging] MinLevel（trace/debug/info/warn/error，坏值回 info）。
    {
        char lv[16] = {};
        IniReadUtf8Layered("Logging", "MinLevel", "info", lv, sizeof(lv));
        g_log_level = ParseLogLevel_(lv);
    }

    // 上次勾号生效的槽位（面板自动选中该编号，不自动读档）。
    // 存 H3Auto.user.ini [General] LastProfile，无记录默认 1。
    {
        char buf[8] = {};
        const bool hit = IniReadUtf8Layered("General", "LastProfile",
            "", buf, (int)sizeof(buf));
        const int last = (hit && buf[0]) ? atoi(buf) : 1;
        g_last_profile = ClampInt(last, 1, 5) - 1;
    }

    char toggle_buf[64] = {};
    char oneshot_buf[64] = {};
    char openpanel_buf[64] = {};
    IniReadUtf8Layered("Hotkeys", "ToggleManual", "F9",
        toggle_buf, sizeof(toggle_buf));
    IniReadUtf8Layered("Hotkeys", "OneShotManual", "J",
        oneshot_buf, sizeof(oneshot_buf));
    IniReadUtf8Layered("Hotkeys", "OpenSettings", "P",
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
