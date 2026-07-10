// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册
// 【测试阶段】逐个加回，WH_GETMESSAGE 已移除（会锁住 DLL）

// 前向声明
extern void ResetAutoState();
extern void ShowSettingsDlg();

// ================================================
// 策略：
//   HiHook 0x495C50 — 战斗循环，标记 s_in_combat
//   LoHook 0x600430 — Blt 完成后弹窗（只在 s_show_pending=true 时）
//   WH_KEYBOARD_LL 钩子 — 检测鼠标右键（不锁 DLL）
//
// 不使用 WH_GETMESSAGE 钩子（会锁住 DLL 导致无法释放）
// ================================================

// ---- 全局状态 ----
static bool s_in_combat = false;
static bool s_show_pending = false;
static HHOOK s_mouse_hook = nullptr;

// ---- WH_MOUSE_LL 钩子：检测鼠标右键 ----
static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0) {
        if (wParam == WM_RBUTTONUP) {
            if (s_in_combat) {
                s_show_pending = true;
                WriteLog("[MouseHook] RButton up in combat, pending=1");
            }
        }
    }
    return CallNextHookEx(s_mouse_hook, code, wParam, lParam);
}

// ---- 战斗循环 HiHook ----
static INT __stdcall Hook_CycleCombatScreen(HiHook* h, INT thisptr)
{
    typedef INT(__thiscall* OrigFunc_t)(INT);
    INT result = reinterpret_cast<OrigFunc_t>(h->GetDefaultFunc())(thisptr);
    s_in_combat = true;
    return result;
}

// ---- Blt 完成后 LoHook：仅当 pending 时弹窗 ----
static INT __stdcall Hook_AfterBlt(LoHook* h, HookContext* c)
{
    (void)h; (void)c;
    if (s_show_pending) {
        s_show_pending = false;
        WriteLog("[AfterBlt] Showing settings dlg...");
        ShowSettingsDlg();
    }
    return EXEC_DEFAULT;
}

static void StartPlugin()
{
    WriteLog("战场自动化 开始注册 Hook。");

    // WH_MOUSE_LL 钩子
    s_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_hModule, 0);
    if (s_mouse_hook) {
        WriteLog("WH_MOUSE_LL 钩子安装成功。");
    } else {
        WriteLog("WH_MOUSE_LL 钩子安装失败! err=%d", GetLastError());
    }

    using CycleFunc_t = INT(__stdcall*)(HiHook*, INT);
    _PI->WriteHiHook(0x495C50, SPLICE_, EXTENDED_, THISCALL_,
        static_cast<void*>(static_cast<CycleFunc_t>(&Hook_CycleCombatScreen)));
    WriteLog("HiHook 0x495C50 已注册（战斗循环）。");

    _PI->WriteLoHook(0x600430, Hook_AfterBlt);
    WriteLog("LoHook 0x600430 已注册（延迟弹窗）。");

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
        if (s_mouse_hook) {
            UnhookWindowsHookEx(s_mouse_hook);
            s_mouse_hook = nullptr;
        }
        ResetAutoState();
    }
    return TRUE;
}
