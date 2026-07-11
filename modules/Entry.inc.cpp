// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册

// 前向声明
extern void ResetAutoState();
extern void ShowSettingsDlg();

// ================================================
// 策略：
//   HiHook 0x495C50 — 战斗循环，每帧调用，检测右键
//   LoHook 0x600430 — Blt 完成后，弹窗
// ================================================

// ---- 全局状态 ----
static bool s_in_combat = false;
static bool s_show_pending = false;
static bool s_rbutton_down = false;
static int s_hi_count = 0;
static int s_blt_count = 0;

// ---- 战斗循环 HiHook：每帧调用，检测右键 ----
static INT __stdcall Hook_CycleCombatScreen(HiHook* h, INT thisptr)
{
    typedef INT(__thiscall* OrigFunc_t)(INT);
    INT result = reinterpret_cast<OrigFunc_t>(h->GetDefaultFunc())(thisptr);
    s_in_combat = true;
    s_hi_count++;

    // 入口日志（仅前3帧）
    if (s_hi_count <= 3) {
        WriteLog("[HiHook] called! hi_count=%d, thisptr=0x%X", s_hi_count, thisptr);
    }

    // GetAsyncKeyState 适合 hook 场景：右键按下时弹窗（只弹一次）
    bool rbutton_now = (GetAsyncKeyState(VK_RBUTTON) < 0);
    if (rbutton_now && !s_rbutton_down) {
        s_show_pending = true;
        WriteLog("[HiHook] RButton DOWN! pending=1");
    }
    s_rbutton_down = rbutton_now;

    return result;
}

// ---- Blt 完成后 LoHook：弹窗 ----
static INT __stdcall Hook_AfterBlt(LoHook* h, HookContext* c)
{
    (void)h; (void)c;
    s_blt_count++;
    if (s_show_pending) {
        s_show_pending = false;
        WriteLog("[AfterBlt] Showing settings dlg, blt_count=%d...", s_blt_count);
        ShowSettingsDlg();
    }
    return EXEC_DEFAULT;
}

static void StartPlugin()
{
    WriteLog("战场自动化 开始注册 Hook。");

    // HiHook：战斗循环，每帧调用，检测右键
    using CycleFunc_t = INT(__stdcall*)(HiHook*, INT);
    _PI->WriteHiHook(0x495C50, SPLICE_, EXTENDED_, THISCALL_,
        static_cast<void*>(static_cast<CycleFunc_t>(&Hook_CycleCombatScreen)));
    WriteLog("HiHook 0x495C50 已注册（战斗循环+GetKeyState检测右键）。");

    // LoHook 0x600430：Blt 完成后弹窗
    _PI->WriteLoHook(0x600430, Hook_AfterBlt);
    WriteLog("LoHook 0x600430 已注册（Blt完成后弹窗）。");

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
