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
    for (int p = 0; p < 5; ++p) {
        for (int s = 0; s < 21; ++s)
            g_profiles[p][s] = def;
        g_stop_turns[p] = H3AutoPolicy::DEFAULT_STOP_TURNS;
        g_summon[p] = summon_def;
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
// 战斗存档库（§17 智能存读档）：DLL 同目录，每场战斗（指纹）一个文件
// <16位十六进制指纹>.json，同场多次「确定」各存一条，按时间升序排列，
// 只保留最近 kBattlesKeep_ 条（旧的丢弃）。JSON 结构：
//   {"version":1,"battle":"<hex16>","entries":[
//     {"time":"yyyymmdd-hhmmss","active":1..5,
//      "p":["H3AP9 …","H3AP9 …","H3AP9 …","H3AP9 …","H3AP9 …"]}, … ]}
// 每方案的规则文本复用 EncodeProfileStoreText/DecodeProfileStoreText
// （输出仅 H3AP9+数字+空格，JSON 字符串无需转义）。army 表喂 0：
// 同指纹战斗即同部队，无需四轮关联，表仅存档格式占位。
// 跨文件再按 mtime LRU 保留最近 kBattlesKeep_ 场。
// ======================================================================
static const int kBattlesKeep_ = 30;

static void BattleStorePath_(unsigned long long fp, char* out, int cap)
{
    // 目录缓冲堆分配：kPathCap_ = 4MB，放栈上必炸游戏线程（~1MB）。
    char* dir = new(std::nothrow) char[kPathCap_]();
    if (!dir) { if (cap > 0) out[0] = 0; return; }
    strncpy(dir, g_profiles_prefix, kPathCap_ - 1);
    char* slash = strrchr(dir, '\\');
    if (slash) *(slash + 1) = 0; else dir[0] = 0;
    _snprintf(out, cap, "%s%016llX.json", dir, fp);
    if (cap > 0) out[cap - 1] = 0;
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

// 在 text 中找 '"键"' 后的第一个 ':' 之后的值起点；找不到返回 nullptr。
static const char* JsonFindValue_(const char* text, const char* key)
{
    char pat[32];
    _snprintf(pat, sizeof(pat), "\"%s\"", key);
    pat[sizeof(pat) - 1] = 0;
    const char* p = strstr(text, pat);
    if (!p) return nullptr;
    p = strchr(p + strlen(pat), ':');
    if (!p) return nullptr;
    return p + 1;
}

// 从 '"…"' 值起点提取字符串内容（不含引号）到 out；成功 true。
static bool JsonReadString_(const char* v, char* out, int cap)
{
    if (!v || *v != '"') return false;
    ++v;
    int n = 0;
    while (v[n] && v[n] != '"') {
        if (n + 1 >= cap) return false;
        out[n] = v[n];
        ++n;
    }
    if (v[n] != '"') return false;
    out[n] = 0;
    return true;
}

// 解析一条 entry 对象文本（obj 指向 '{'）。文本内不含嵌套花括号。
static bool ParseRecordText_(const char* obj, BattleStoreRecord* rec)
{
    const char* v = JsonFindValue_(obj, "time");
    if (!v || !JsonReadString_(v, rec->time, sizeof(rec->time))) return false;
    v = JsonFindValue_(obj, "active");
    if (!v) return false;
    rec->active = atoi(v) - 1;
    if (rec->active < 0 || rec->active > 4) return false;
    const char* pv = JsonFindValue_(obj, "p");
    if (!pv || *pv != '[') return false;
    ++pv;
    // 32KB 单方案文本缓冲：堆分配防游戏线程栈溢出（读档同款惯例）。
    char* one = new(std::nothrow) char[32 * 1024];
    if (!one) return false;
    bool ok = true;
    for (int p = 0; p < 5 && ok; ++p) {
        if (!JsonReadString_(pv, one, 32 * 1024)) { ok = false; break; }
        int zero_types[21] = {};
        int zero_counts[21] = {};
        uint16_t turns = 0;
        SummonProfileFields sf = {};
        if (!H3AutoPolicy::DecodeProfileStoreText(one, zero_types,
                zero_counts, rec->rules[p], &turns, &sf)) {
            ok = false;
            break;
        }
        rec->stop_turns[p] = turns;
        rec->summon[p] = sf;
        // 跳到下一个字符串值：pv 此时指向当前串的「开引号」（JsonReadString_
        // 不动 pv）——闭引号 = strchr(pv+1)，下一个字符串的开引号 = 再 +1
        // 越过闭引号后的分隔符（',' 或 '[' 直连的第一串由 ++pv 前移到位）。
        pv = strchr(pv + 1, '"');          // 闭引号
        if (!pv) { ok = false; break; }
        pv = strchr(pv + 1, '"');          // 下一个字符串的开引号
        if (!pv) {
            if (p < 4) { ok = false; break; } // 还有方案没读：格式坏
            break;                          // 第 5 串已读完：正常收尾
        }
    }
    delete[] one;
    return ok;
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
    // 单条 entry 文本 ≈5×32KB：堆缓冲防游戏线程栈溢出。
    char* obj = new(std::nothrow) char[5 * 32 * 1024 + 1];
    int count = 0;
    if (!obj) { delete[] text; return -1; }
    const char* p = strstr(text, "\"entries\"");
    if (p) {
        p = strchr(p, '[');
        while (p && count < cap) {
            p = strchr(p, '{');
            if (!p) break;
            const char* end = strchr(p, '}');
            if (!end) break;
            const size_t len = (size_t)(end - p + 1);
            if (len >= (size_t)(5 * 32 * 1024 + 1)) break;
            memcpy(obj, p, len);
            obj[len] = 0;
            if (!ParseRecordText_(obj, &records[count])) { count = -1; break; }
            ++count;
            p = end + 1;
        }
    }
    delete[] obj;
    delete[] text;
    return count;
}

// 整库序列化写回（升序）。
static bool SaveBattleStoreRaw_(unsigned long long fp,
    const BattleStoreRecord* records, int count)
{
    const size_t buf_cap = (size_t)count * (5 * 32 * 1024 + 128) + 256;
    char* text = new char[buf_cap];
    int off = _snprintf(text, 256,
        "{\"version\":1,\"battle\":\"%016llX\",\"entries\":[", fp);
    bool ok = off > 0;
    const int zero_types[21] = {};
    const int zero_counts[21] = {};
    for (int i = 0; ok && i < count; ++i) {
        const BattleStoreRecord& r = records[i];
        off += _snprintf(text + off, 160,
            "%s\n {\"time\":\"%s\",\"active\":%d,\"p\":[",
            i ? "," : "", r.time, r.active + 1);
        for (int p = 0; ok && p < 5; ++p) {
            if (p) text[off++] = ',';
            text[off++] = '"';
            const int n = H3AutoPolicy::EncodeProfileStoreText(zero_types,
                zero_counts, r.rules[p], r.stop_turns[p],
                r.summon[p], text + off, 32 * 1024);
            if (n <= 0) { ok = false; break; }
            off += n;
            text[off++] = '"';
        }
        if (ok) off += _snprintf(text + off, 16, "]}");
    }
    if (ok) {
        off += _snprintf(text + off, 16, "]}\n");
        char* path = new(std::nothrow) char[kPathCap_];
        if (!path) { delete[] text; return false; }
        BattleStorePath_(fp, path, kPathCap_);
        ok = WriteTextFile_(path, text, off);
        delete[] path;
    }
    delete[] text;
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
        LogDebug("[BattleStore] 读战斗存档异常 code=0x%08X", code);
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
    int active, bool* skipped_same)
{
    if (skipped_same) *skipped_same = false;
    if (!fp) return false;
    bool ok = false;
    DWORD code = 0;
    __try {
        BattleStoreRecord* all = new BattleStoreRecord[kBattlesKeep_ + 1];
        BattleStoreRecord* rec = new BattleStoreRecord();
        int n = LoadBattleStoreRaw_(fp, all, kBattlesKeep_);
        if (n < 0) n = 0; // 旧档损坏：从头开始重建
        NowStamp_(rec->time, sizeof(rec->time));
        rec->active = active >= 0 && active <= 4 ? active : 0;
        memcpy(rec->rules, rules, sizeof(rec->rules));
        memcpy(rec->stop_turns, stop_turns, sizeof(rec->stop_turns));
        memcpy(rec->summon, summon, sizeof(rec->summon));
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
        LogDebug("[BattleStore] 追加战斗存档异常 code=0x%08X", code);
        ok = false;
    }
    return ok;
}

// 跨文件 LRU：列目录 <16位hex>.json，按 mtime 降序保留前 keep 个。
static void PruneBattleStore_LRU_(int keep)
{
    // dir/pattern 都是 4MB 级缓冲：堆分配（游戏线程栈 ~1MB）。
    char* dir = new(std::nothrow) char[kPathCap_]();
    char* pattern = new(std::nothrow) char[kPathCap_]();
    if (!dir || !pattern) { delete[] dir; delete[] pattern; return; }
    strncpy(dir, g_profiles_prefix, kPathCap_ - 1);
    char* slash = strrchr(dir, '\\');
    if (slash) *(slash + 1) = 0; else dir[0] = 0;
    _snprintf(pattern, kPathCap_, "%s*.json", dir);
    pattern[kPathCap_ - 1] = 0;
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { delete[] dir; delete[] pattern; return; }
    struct Entry { FILETIME t; char name[MAX_PATH]; };
    Entry* items = new Entry[128];
    int count = 0;
    do {
        // 只认 16 位十六进制文件名（战斗标识），目录里其它 json 不动。
        const char* nm = fd.cFileName;
        const size_t nl = strlen(nm);
        bool hex16 = nl == 21 && nm[16] == '.' && nm[17] == 'j'
            && nm[18] == 's' && nm[19] == 'o' && nm[20] == 'n';
        for (int i = 0; hex16 && i < 16; ++i) {
            const char c = nm[i];
            const bool okc = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
            if (!okc) { hex16 = false; break; }
        }
        if (!hex16 || count >= 128) continue;
        items[count].t = fd.ftLastWriteTime;
        strncpy(items[count].name, nm, MAX_PATH - 1);
        items[count].name[MAX_PATH - 1] = 0;
        ++count;
    } while (FindNextFileA(h, &fd));
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
        char* full = new(std::nothrow) char[kPathCap_]();
        if (!full) continue;
        _snprintf(full, kPathCap_, "%s%s", dir, items[i].name);
        full[kPathCap_ - 1] = 0;
        if (DeleteFileA(full))
            LogInfo("[BattleStore] LRU 淘汰 %s", items[i].name);
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
