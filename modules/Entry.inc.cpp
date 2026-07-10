// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册。

static void StartPlugin()
{
    WriteLog("H3Auto 开始注册 Hook。");
    // TODO: 注册 Hook
    WriteLog("H3Auto 已启用。");
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
        g_disable_log = ReadDisableLogFromIniFileA(g_ini_path);
        SetupDatedLogPathAndCleanup(hModule);
        WriteLog("H3Auto 正在加载。");
        _P = GetPatcher();
        if (!_P) {
            WriteLog("GetPatcher 失败；插件将保持未激活状态。");
            return TRUE;
        }
        _PI = _P->CreateInstance("HD.Plugin.H3Auto");
        if (!_PI) {
            WriteLog("CreateInstance 失败；插件将保持未激活状态。");
            return TRUE;
        }
        StartPlugin();
    }
    return TRUE;
}
