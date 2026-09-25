// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册

extern void ResetAutoState();
extern void ShutdownCombatHotkeys();
extern INT __stdcall Hook_BltComplete(LoHook* h, HookContext* c);
extern INT __stdcall Hook_BattleMsgProc(LoHook* h, HookContext* c);
extern int __stdcall HH_ShouldAutoExecute(HiHook* h, _BattleMgr_* This);
extern int __stdcall HH_OnBattleActionExecute(HiHook* h, _BattleMgr_* This, int flags);
extern void LoadUiTexts();
extern char g_profiles_path[MAX_PATH];

// ---- Plugin start ----
static void StartPlugin()
{
    LogInfo("打铁助手: registering hooks.");

    // LoHook: 每帧检测自动战斗对话框 + 画面板
    _PI->WriteLoHook(0x600430, Hook_BltComplete);
    LogInfo("LoHook 0x600430 registered.");

    // LoHook: 战斗消息处理入口，面板打开时拦掉鼠标移动的 hover 重算
    _PI->WriteLoHook(0x4746B0, Hook_BattleMsgProc);
    LogInfo("LoHook 0x4746B0 registered.");

    // HiHook: 战争机器接管判定点。FUN_004744d0 判定当前活动单位是否走自动
    // 执行；我们在原版返回“等待人类输入”时，若该战争机器已配置非手动策略，
    // 改返回“自动执行”，复用原版 AI 执行、动画、回合推进。
    _PI->WriteHiHook(0x4744D0, SPLICE_, THISCALL_, HH_ShouldAutoExecute);
    LogInfo("HiHook 0x4744D0 registered.");

    // HiHook: 原版动作执行入口。单次接管时，在玩家真正提交的动作进入
    // 执行链后结束锁定，恢复后续部队自动执行。
    _PI->WriteHiHook(0x4786B0, SPLICE_, THISCALL_, HH_OnBattleActionExecute);
    LogInfo("HiHook 0x4786B0 registered.");

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
        GetModuleFileNameW(hModule, wpath, kPathCap_ / 2);
        utf8_from_wide(wpath, g_ini_path, kPathCap_);
        char* dot = strrchr(g_ini_path, '.');
        if (dot) strcpy(dot, ".ini");
        // 方案存档与 DLL 同目录：每个编号一个独立文件（前缀 H3Auto.profiles + 编号）。
        GetModuleFileNameW(hModule, wpath, kPathCap_ / 2);
        wchar_t* wslash = wcsrchr(wpath, L'\\');
        if (!wslash) wslash = wcsrchr(wpath, L'/');
        if (wslash) wcscpy(wslash + 1, L"H3Auto.profiles");
        else wcscpy(wpath, L"H3Auto.profiles");
        utf8_from_wide(wpath, g_profiles_prefix, kPathCap_);
        // 编号记忆：独立文件 H3Auto.last.ini（一行数字 1..5），不写 INI。
        GetModuleFileNameW(hModule, wpath, kPathCap_ / 2);
        wslash = wcsrchr(wpath, L'\\');
        if (!wslash) wslash = wcsrchr(wpath, L'/');
        if (wslash) wcscpy(wslash + 1, L"H3Auto.last.ini");
        else wcscpy(wpath, L"H3Auto.last.ini");
        utf8_from_wide(wpath, g_last_profile_path, kPathCap_);
        wchar_t* ini_path_w = new wchar_t[kPathCap_ / 2];
        MultiByteToWideChar(CP_UTF8, 0, g_ini_path, -1, ini_path_w, kPathCap_ / 2);
        g_disable_log = ReadDisableLogFromIniFileW(ini_path_w);
        delete[] ini_path_w;
        delete[] wpath;
        SetupDatedLogPathAndCleanup(hModule);
        LogInfo("打铁助手 loading.");
        _P = GetPatcher();
        if (!_P) { LogError("GetPatcher failed."); return TRUE; }
        LogInfo("GetPatcher ok.");
        _PI = _P->CreateInstance("HD.Plugin.H3Auto");
        if (!_PI) { LogError("CreateInstance failed."); return TRUE; }
        LogInfo("CreateInstance ok.");
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
