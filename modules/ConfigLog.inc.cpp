// ========== 配置与策略枚举 ==========
// 新模型：行动策略与目标策略正交；见 H3Note/H3Auto行动与目标策略实现方案.md

enum AutoActionKind : uint8_t {
    AA_MANUAL = 0,        // 手动（不干预）
    AA_DEFEND,            // 防御
    AA_WAIT,              // 等待
    AA_MOVE,              // 移动
    AA_MELEE_ATTACK,      // 循环近战（枚举值固定为 4）
    AA_RANGED_ATTACK,     // 远程攻击
    AA_FIRST_AID,         // 急救帐篷疗伤
    AA_COUNT
};

enum AutoTargetKind : uint8_t {
    AT_NONE = 0,          // 无目标
    AT_STACK,             // 部队目标
    AT_POSITION,          // 战场位置目标
    AT_COUNT
};

enum AutoTargetSide : uint8_t {
    ATS_OWN = 0,          // 己方
    ATS_ENEMY,            // 敌方
    ATS_EITHER,           // 双方均可（执行前仍受兼容矩阵约束）
    ATS_COUNT
};

// 选择器按行动分流；近战/移动不走选择器菜单。
enum AutoTargetSelector : uint8_t {
    SEL_RANDOM = 0,       // 随机（远程/急救共用）
    SEL_RANGED_SPEED,     // 远程：远程优先 + 高速
    SEL_COUNT_HIGH,       // 远程：数量最多
    SEL_WOUND_RATIO,      // 急救：失血比例最高
    SEL_WOUND_VALUE,      // 急救：失血数值最大
    SEL_COUNT
};

// 容量按第三小列宽度（≈368）与槽位文本宽度反算：
//  循环施法单数字 → 槽宽约 34，一行 10 个；
//  循环移动 A01 → 槽宽约 43，每行 8 个，两行 16 个；
//  循环近战 A01→B02 → 槽宽约 71，每行 5 组，两行 10 组。
static const int MELEE_PAIR_CAPACITY = 10;
static const int MOVE_WAYPOINT_CAPACITY = 16;
static const int SPELL_SLOT_CAPACITY = 10;

struct AutoTargetRule {
    AutoTargetKind kind;
    AutoTargetSide side;
    AutoTargetSelector selector;

    // 近战专用：模拟玩家“站到 standHex，再点 attackHex”。
    // attackHex 上有敌人（头格或双格尾格都算）时才提交近战。
    int16_t meleeStandHex;     // 站立/接近格，-1=未设
    int16_t meleeAttackHex;    // 攻击点击格，-1=未设

    // 循环移动专用：有序路径点列表，逐点巡逻循环（末点回首点）。
    // 从战场直接点选追加；-1=空槽。
    int16_t moveWaypoints[MOVE_WAYPOINT_CAPACITY]; // 路径点，原版 hex 1..185，-1=空
    int8_t  moveWaypointCount; // 有效点数 0..MOVE_WAYPOINT_CAPACITY

    // 循环近战专用：最多 MELEE_PAIR_CAPACITY 组“站立位 + 攻击位”。
    // 保留上面的 meleeStandHex/meleeAttackHex 作为旧规则兼容镜像；
    // 新 UI 和执行端均以本序列为准。
    int16_t meleeStandHexes[MELEE_PAIR_CAPACITY];
    int16_t meleeAttackHexes[MELEE_PAIR_CAPACITY];
    int8_t  meleePairCount;    // 有效组合数 0..MELEE_PAIR_CAPACITY
};

struct AutoStackRule {
    AutoActionKind action;
    AutoTargetRule target;
    bool allowDefendFallback;  // 允许降级为防御（仅普通部队）
    bool quickCastFirst;       // 兼容字段：spellSlotCount>0 时为 true
    int8_t spellSlot;          // 兼容镜像：等于 spellSlots[0]（1-9/0）
    // 行动前循环施法：按顺序轮换快捷键 1-9/0，单行最多 SPELL_SLOT_CAPACITY 槽。
    int8_t spellSlots[SPELL_SLOT_CAPACITY];
    int8_t spellSlotCount;     // 有效槽位数 0..SPELL_SLOT_CAPACITY
};

static AutoStackRule MakeDefaultRule_()
{
    AutoStackRule r = {};
    r.action = AA_MANUAL;
    r.target.kind = AT_NONE;
    r.target.side = ATS_ENEMY;
    r.target.selector = SEL_RANDOM;
    r.target.meleeStandHex = -1;
    r.target.meleeAttackHex = -1;
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i) r.target.moveWaypoints[i] = -1;
    r.target.moveWaypointCount = 0;
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i) {
        r.target.meleeStandHexes[i] = -1;
        r.target.meleeAttackHexes[i] = -1;
    }
    r.target.meleePairCount = 0;
    r.allowDefendFallback = false;
    r.quickCastFirst = false;
    r.spellSlot = 1;
    for (int i = 0; i < SPELL_SLOT_CAPACITY; ++i)
        r.spellSlots[i] = -1;
    r.spellSlotCount = 0;
    return r;
}

// 五套仅驻留内存的已确认方案；默认全为手动。
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
    int  toggle_manual_vk;     // F11：本场自动/全手动切换
    int  one_shot_manual_vk;   // 左 Ctrl：单次接管当前/下一支部队
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

static int ParseHotkeyVk_(const char* text, int default_vk)
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
    return default_vk;
}

static void ReadConfig()
{
    const char* f = g_ini_path;
    cfg.disable_on_start = GetPrivateProfileIntA("General", "DisableOnStart", 0, f);
    cfg.disable_on_start = ClampInt(cfg.disable_on_start, 0, 1);

    char toggle_buf[64] = {};
    char oneshot_buf[64] = {};
    GetPrivateProfileStringA("Hotkeys", "ToggleManual", "F11",
        toggle_buf, sizeof(toggle_buf), f);
    GetPrivateProfileStringA("Hotkeys", "OneShotManual", "LControl",
        oneshot_buf, sizeof(oneshot_buf), f);
    cfg.toggle_manual_vk = ParseHotkeyVk_(toggle_buf, VK_F11);
    cfg.one_shot_manual_vk = ParseHotkeyVk_(oneshot_buf, VK_LCONTROL);

    WriteLog("配置加载：DisableOnStart=%d ToggleManual=0x%X OneShotManual=0x%X",
        cfg.disable_on_start, cfg.toggle_manual_vk, cfg.one_shot_manual_vk);
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
