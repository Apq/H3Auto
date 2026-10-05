// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册

#pragma comment(lib, "version.lib") // LogSelfVersion_ 读 VERSIONINFO

extern void ResetAutoState();
extern void ShutdownCombatHotkeys();
extern INT __stdcall Hook_BltComplete(LoHook* h, HookContext* c);
extern INT __stdcall Hook_BattleMsgProc(LoHook* h, HookContext* c);
extern int __stdcall HH_ShouldAutoExecute(HiHook* h, _BattleMgr_* This);
extern int __stdcall HH_OnBattleActionExecute(HiHook* h, _BattleMgr_* This, int flags);
extern void LoadUiTexts();
extern char g_profiles_path[MAX_PATH];

// ---- 版本自证（0.5 对外版）----
// 玩家反馈排查第一步：确认实际加载的 DLL 版本与路径。多包双装/旧包残留
// 表现为"更新了没生效"（2026-10-04 玩家日志即旧版在跑）。版本号读自身
// VERSIONINFO（与 .rc 同源），不维护第二份字符串；路径走宽字符转 UTF-8，
// 与 DllMain 里 ini 路径同口径（GetModuleFileNameA 的 ANSI 结果是 GBK）。
static void LogSelfVersion_()
{
    wchar_t wpath[MAX_PATH] = {};
    GetModuleFileNameW(g_hModule, wpath, MAX_PATH);
    char utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8,
        (int)sizeof(utf8), nullptr, nullptr);
    char ver[64] = "?";
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(wpath, &handle);
    if (size) {
        BYTE* data = new BYTE[size];
        if (GetFileVersionInfoW(wpath, 0, size, data)) {
            struct LangCodePage { WORD lang, codepage; };
            LangCodePage* langs = nullptr; UINT lang_count = 0;
            if (VerQueryValueW(data, L"\\VarFileInfo\\Translation",
                    (LPVOID*)&langs, &lang_count)
                && lang_count > 0)
            {
                wchar_t key[80] = {};
                swprintf(key, 80, L"\\StringFileInfo\\%04X%04X\\ProductVersion",
                    langs[0].lang, langs[0].codepage);
                wchar_t* product = nullptr; UINT len = 0;
                if (VerQueryValueW(data, key, (LPVOID*)&product, &len) && product)
                    WideCharToMultiByte(CP_UTF8, 0, product, -1, ver,
                        (int)sizeof(ver), nullptr, nullptr);
            }
        }
        delete[] data;
    }
    LogInfo("打铁助手 v%s | DLL=%s", ver, utf8);
}

// ---- Plugin start ----
static void StartPlugin()
{
    LogSelfVersion_();
    LogInfo("打铁助手: registering hooks.");

    // LoHook: 每帧检测自动战斗对话框 + 画面板
    _PI->WriteLoHook(0x600430, Hook_BltComplete);

    // LoHook: 战斗消息处理入口，面板打开时拦掉鼠标移动的 hover 重算
    _PI->WriteLoHook(0x4746B0, Hook_BattleMsgProc);

    // HiHook: 战争机器接管判定点。FUN_004744d0 判定当前活动单位是否走自动
    // 执行；我们在原版返回“等待人类输入”时，若该战争机器已配置非手动策略，
    // 改返回“自动执行”，复用原版 AI 执行、动画、回合推进。
    _PI->WriteHiHook(0x4744D0, SPLICE_, THISCALL_, HH_ShouldAutoExecute);

    // HiHook: 原版动作执行入口。单次接管时，在玩家真正提交的动作进入
    // 执行链后结束锁定，恢复后续部队自动执行。
    _PI->WriteHiHook(0x4786B0, SPLICE_, THISCALL_, HH_OnBattleActionExecute);
    LogInfo("hooks 已注册：0x600430 / 0x4746B0 / 0x4744D0 / 0x4786B0");

    ResetAutoState();
    LogInfo("打铁助手: plugin enabled.");
}

// ========== DllMain ==========

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    static bool initialized = false;
    if (reason == DLL_PROCESS_ATTACH && !initialized) {
        initialized = true;
        g_hModule = hModule;
        // 路径一律走宽字符版再转 UTF-8：GetModuleFileNameA 返回系统 ANSI（中文
        // 系统是 GBK），直接存进 g_ini_path 会让 UTF-8 日志里的中文路径显示成
        // 乱码，中文目录下的文件访问也只能靠 GBK 碰巧工作。
        auto utf8_from_wide = [](const wchar_t* w, char* out, int out_size) {
            WideCharToMultiByte(CP_UTF8, 0, w, -1, out, out_size, nullptr, nullptr);
            if (out_size > 0) out[out_size - 1] = 0;
        };
        wchar_t* wpath = new wchar_t[kPathCap_ / 2]();
        // 配置分两层（DLL 同目录）：default = 出厂默认随包分发；
        // user = 玩家改动层（日志级别等），读取时叠加覆盖默认层。
        auto set_dll_dir_file = [&](const wchar_t* name, char* out_utf8) {
            GetModuleFileNameW(hModule, wpath, kPathCap_ / 2);
            wchar_t* wslash = wcsrchr(wpath, L'\\');
            if (!wslash) wslash = wcsrchr(wpath, L'/');
            if (wslash) wcscpy(wslash + 1, name);
            else wcscpy(wpath, name);
            utf8_from_wide(wpath, out_utf8, kPathCap_);
        };
        set_dll_dir_file(L"H3Auto.default.ini", g_ini_path);
        set_dll_dir_file(L"H3Auto.user.ini", g_user_ini_path);
        // 方案存档与 DLL 同目录：每个编号一个独立文件（前缀 H3Auto.profiles + 编号）。
        GetModuleFileNameW(hModule, wpath, kPathCap_ / 2);
        wchar_t* wslash = wcsrchr(wpath, L'\\');
        if (!wslash) wslash = wcsrchr(wpath, L'/');
        if (wslash) wcscpy(wslash + 1, L"H3Auto.profiles");
        else wcscpy(wpath, L"H3Auto.profiles");
        utf8_from_wide(wpath, g_profiles_prefix, kPathCap_);
        g_disable_log = ReadDisableLogFromIniFiles();
        delete[] wpath;
        SetupDatedLogPathAndCleanup(hModule);
        LogInfo("打铁助手 loading.");
        _P = GetPatcher();
        if (!_P) { LogError("GetPatcher failed."); return TRUE; }
        _PI = _P->CreateInstance("HD.Plugin.H3Auto");
        if (!_PI) { LogError("CreateInstance failed."); return TRUE; }
        ReadConfig();
        LoadUiTexts();
        StartPlugin();
    }
    if (reason == DLL_PROCESS_DETACH) {
        ShutdownCombatHotkeys();
        ResetAutoState();
    }
    return TRUE;
}
