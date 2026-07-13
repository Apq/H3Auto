// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册

extern void ResetAutoState();
extern INT __stdcall Hook_BltComplete(LoHook* h, HookContext* c);
extern void LoadLabels_(const char* ini_path);

// ---- Plugin start ----
static void StartPlugin()
{
    WriteLog("H3Auto: registering hooks.");

    // LoHook: 每帧检测自动战斗对话框 + 画面板
    _PI->WriteLoHook(0x600430, Hook_BltComplete);
    WriteLog("LoHook 0x600430 registered.");

    ResetAutoState();
    WriteLog("H3Auto: plugin enabled.");
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
        wchar_t ini_path_w[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, g_ini_path, -1, ini_path_w, MAX_PATH);
        g_disable_log = ReadDisableLogFromIniFileW(ini_path_w);
        SetupDatedLogPathAndCleanup(hModule);
        WriteLog("H3Auto loading.");
        _P = GetPatcher();
        if (!_P) { WriteLog("GetPatcher failed."); return TRUE; }
        WriteLog("GetPatcher ok.");
        _PI = _P->CreateInstance("HD.Plugin.ZhanChangZiDongHua");
        if (!_PI) { WriteLog("CreateInstance failed."); return TRUE; }
        WriteLog("CreateInstance ok.");
        ReadConfig();
        // 构建 labels 文件路径：与 H3Auto.ini 同目录的 H3Auto_labels.txt
        {
            char labels_path[MAX_PATH] = {};
            strncpy(labels_path, g_ini_path, MAX_PATH - 1);
            char* dot = strrchr(labels_path, '.');
            if (dot) strcpy(dot, "_labels.txt");
            LoadLabels_(labels_path);
        }
        StartPlugin();
    }
    if (reason == DLL_PROCESS_DETACH) {
        ResetAutoState();
    }
    return TRUE;
}
