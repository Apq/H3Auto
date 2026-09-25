// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册

extern void ResetAutoState();
extern void ShutdownCombatHotkeys();
extern INT __stdcall Hook_BltComplete(LoHook* h, HookContext* c);
extern INT __stdcall Hook_BattleMsgProc(LoHook* h, HookContext* c);
extern int __stdcall HH_ShouldAutoExecute(HiHook* h, _BattleMgr_* This);
extern int __stdcall HH_OnBattleActionExecute(HiHook* h, _BattleMgr_* This, int flags);
extern void LoadLabels_(const char* ini_path);
extern char g_profiles_path[MAX_PATH];

// ---- Plugin start ----
static void StartPlugin()
{
    WriteLog("打铁助手: registering hooks.");

    // LoHook: 每帧检测自动战斗对话框 + 画面板
    _PI->WriteLoHook(0x600430, Hook_BltComplete);
    WriteLog("LoHook 0x600430 registered.");

    // LoHook: 战斗消息处理入口，面板打开时拦掉鼠标移动的 hover 重算
    _PI->WriteLoHook(0x4746B0, Hook_BattleMsgProc);
    WriteLog("LoHook 0x4746B0 registered.");

    // HiHook: 战争机器接管判定点。FUN_004744d0 判定当前活动单位是否走自动
    // 执行；我们在原版返回“等待人类输入”时，若该战争机器已配置非手动策略，
    // 改返回“自动执行”，复用原版 AI 执行、动画、回合推进。
    _PI->WriteHiHook(0x4744D0, SPLICE_, THISCALL_, HH_ShouldAutoExecute);
    WriteLog("HiHook 0x4744D0 registered.");

    // HiHook: 原版动作执行入口。单次接管时，在玩家真正提交的动作进入
    // 执行链后结束锁定，恢复后续部队自动执行。
    _PI->WriteHiHook(0x4786B0, SPLICE_, THISCALL_, HH_OnBattleActionExecute);
    WriteLog("HiHook 0x4786B0 registered.");

    ResetAutoState();
    WriteLog("打铁助手: plugin enabled.");
}

// ========== DllMain ==========

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    static bool initialized = false;
    if (reason == DLL_PROCESS_ATTACH && !initialized) {
        initialized = true;
        g_hModule = hModule;
        GetModuleFileNameA(hModule, g_ini_path, MAX_PATH);
        char* dot = strrchr(g_ini_path, '.');
        if (dot) strcpy(dot, ".ini");
        // 方案存档与 DLL 同目录：每个编号一个独立文件（前缀 H3Auto.profiles. + 编号）。
        GetModuleFileNameA(hModule, g_profiles_prefix, MAX_PATH);
        char* slash = strrchr(g_profiles_prefix, (char)92);
        if (!slash) slash = strrchr(g_profiles_prefix, '/');
        if (slash) strcpy(slash + 1, "H3Auto.profiles.");
        else strcpy(g_profiles_prefix, "H3Auto.profiles.");
        // 编号记忆：独立文件 H3Auto.last（一行数字 1..5），不写 INI。
        GetModuleFileNameA(hModule, g_last_profile_path, MAX_PATH);
        char* slash2 = strrchr(g_last_profile_path, (char)92);
        if (!slash2) slash2 = strrchr(g_last_profile_path, '/');
        if (slash2) strcpy(slash2 + 1, "H3Auto.last");
        else strcpy(g_last_profile_path, "H3Auto.last");
        wchar_t ini_path_w[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, g_ini_path, -1, ini_path_w, MAX_PATH);
        g_disable_log = ReadDisableLogFromIniFileW(ini_path_w);
        SetupDatedLogPathAndCleanup(hModule);
        WriteLog("打铁助手 loading.");
        _P = GetPatcher();
        if (!_P) { WriteLog("GetPatcher failed."); return TRUE; }
        WriteLog("GetPatcher ok.");
        _PI = _P->CreateInstance("HD.Plugin.H3Auto");
        if (!_PI) { WriteLog("CreateInstance failed."); return TRUE; }
        WriteLog("CreateInstance ok.");
        ReadConfig();
        LoadLabels_(g_ini_path);
        StartPlugin();
    }
    if (reason == DLL_PROCESS_DETACH) {
        ShutdownCombatHotkeys();
        ResetAutoState();
    }
    return TRUE;
}
