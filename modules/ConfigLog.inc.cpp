// ========== ConfigLog.inc.cpp ==========
// 日志与配置读写。H3Auto 所有模块共享此文件中的全局状态。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static HMODULE g_hModule = nullptr;
static char g_ini_path[MAX_PATH];
static bool g_disable_log = false;
static char g_log_path[MAX_PATH];

static void GetTodayPrefix(char* buf, size_t len)
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_s(&t, &now);
    snprintf(buf, len, "%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

static void SetupDatedLogPathAndCleanup(HMODULE hModule)
{
    char mod_path[MAX_PATH];
    GetModuleFileNameA(hModule, mod_path, MAX_PATH);
    char* last_sep = strrchr(mod_path, '\\');
    if (last_sep) *last_sep = '\0';

    char date_prefix[16];
    GetTodayPrefix(date_prefix, sizeof(date_prefix));

    char search[MAX_PATH];
    snprintf(search, sizeof(search), "%s\\H3Auto_*.log", mod_path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    int count = 0;
    while (h != INVALID_HANDLE_VALUE) {
        ++count;
        if (count > 30) {
            char del_path[MAX_PATH];
            snprintf(del_path, sizeof(del_path), "%s\\%s", mod_path, fd.cFileName);
            DeleteFileA(del_path);
        }
        if (!FindNextFileA(h, &fd)) break;
    }
    if (h != INVALID_HANDLE_VALUE) FindClose(h);

    char ts[32];
    time_t now = time(nullptr);
    struct tm t;
    localtime_s(&t, &now);
    snprintf(ts, sizeof(ts), "%04d%02d%02d_%02d%02d%02d",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
        t.tm_hour, t.tm_min, t.tm_sec);
    snprintf(g_log_path, sizeof(g_log_path), "%s\\H3Auto_%s.log", mod_path, ts);
}

static bool ReadDisableLogFromIniFileA(const char* ini_path)
{
    char buf[8] = {0};
    GetPrivateProfileStringA("Logging", "DisableLog", "0", buf, sizeof(buf), ini_path);
    return buf[0] == '1';
}

static void WriteLog(const char* fmt, ...)
{
    if (g_disable_log) return;
    FILE* f = fopen(g_log_path, "a");
    if (!f) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_s(&t, &now);
    fprintf(f, "[%02d:%02d:%02d] ", t.tm_hour, t.tm_min, t.tm_sec);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}
