// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册

// 前向声明
extern void ResetAutoState();
extern void ShowSettingsDlg();

// ================================================
// 策略：
//   HiHook 0x495C50 — 战斗循环
//   LoHook 0x5F4503 — 每帧渲染，检测右键点击并弹窗
// ================================================

// ---- 全局状态 ----
static bool s_in_combat = false;
static bool s_show_pending = false;
static bool s_rbutton_was_down = false;
static int s_hi_count = 0;
static int s_lo_count = 0;

// ---- 战斗循环 HiHook ----
static INT __stdcall Hook_CycleCombatScreen(HiHook* h, INT thisptr)
{
    typedef INT(__thiscall* OrigFunc_t)(INT);
    INT result = reinterpret_cast<OrigFunc_t>(h->GetDefaultFunc())(thisptr);
    s_in_combat = true;
    s_hi_count++;
    return result;
}

// ---- 战斗渲染 LoHook：每帧检测右键点击 ----
static INT __stdcall Hook_BuildCombat(LoHook* h, HookContext* c)
{
    (void)h; (void)c;
    s_in_combat = true;
    s_lo_count++;

    // 检测右键松开（右键点击完成）
    bool rbutton_now = (GetAsyncKeyState(VK_RBUTTON) < 0);
    if (s_rbutton_was_down && !rbutton_now && !s_show_pending) {
        s_show_pending = true;
        WriteLog("[Build] RButton click! lo_count=%d", s_lo_count);
        ShowSettingsDlg();
        s_show_pending = false;
        WriteLog("[Build] dialog closed, lo_count=%d", s_lo_count);
    }
    s_rbutton_was_down = rbutton_now;

    return EXEC_DEFAULT;
}

static void StartPlugin()
{
    WriteLog("战场自动化 开始注册 Hook。");

    // HiHook：战斗循环
    using CycleFunc_t = INT(__stdcall*)(HiHook*, INT);
    _PI->WriteHiHook(0x495C50, SPLICE_, EXTENDED_, THISCALL_,
        static_cast<void*>(static_cast<CycleFunc_t>(&Hook_CycleCombatScreen)));
    WriteLog("HiHook 0x495C50 已注册。");

    // LoHook：每帧渲染，检测右键
    _PI->WriteLoHook(0x5F4503, Hook_BuildCombat);
    WriteLog("LoHook 0x5F4503 已注册。");

    ResetAutoState();
    WriteLog("战场自动化 已启用。");
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
        WriteLog("战场自动化 正在加载。");
        _P = GetPatcher();
        if (!_P) {
            WriteLog("GetPatcher 失败；插件将保持未激活状态。");
            return TRUE;
        }
        WriteLog("GetPatcher 成功。");
        _PI = _P->CreateInstance("HD.Plugin.ZhanChangZiDongHua");
        if (!_PI) {
            WriteLog("CreateInstance 失败；插件将保持未激活状态。");
            return TRUE;
        }
        WriteLog("CreateInstance 成功。");
        ReadConfig();
        StartPlugin();
    }
    if (reason == DLL_PROCESS_DETACH) {
        ResetAutoState();
    }
    return TRUE;
}
