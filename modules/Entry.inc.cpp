// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册

// 前向声明
extern void ResetAutoState();
extern void ShowSettingsDlg();

// ================================================
// 策略：
//   WH_GETMESSAGE 钩子 — 拦截游戏窗口的 WM_RBUTTONUP 消息
//   HiHook 0x495C50 — 战斗循环，检测到右键消息后弹窗
// ================================================

// ---- 全局状态 ----
static volatile LONG s_rbutton_up_flag = 0;  // 原子标志：收到右键松开消息
static bool s_in_combat = false;
static bool s_show_pending = false;
static HHOOK s_getmsg_hook = nullptr;

// ---- WH_GETMESSAGE 钩子过程 ----
static LRESULT CALLBACK Hook_GetMsgProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0) {
        MSG* msg = (MSG*)lParam;
        if (msg && msg->hwnd) {
            HWND gameHwnd = H3Hwnd::Get();
            if (gameHwnd && msg->hwnd == gameHwnd) {
                if (msg->message == WM_RBUTTONUP) {
                    // 收到右键松开，设原子标志
                    InterlockedExchange(&s_rbutton_up_flag, 1);
                    WriteLog("[WH_GETMESSAGE] WM_RBUTTONUP at (%d,%d)", LOWORD(msg->lParam), HIWORD(msg->lParam));
                }
            }
        }
    }
    return CallNextHookEx(s_getmsg_hook, code, wParam, lParam);
}

// ---- 战斗循环 HiHook：检测右键标志并弹窗 ----
static INT __stdcall Hook_CycleCombatScreen(HiHook* h, INT thisptr)
{
    typedef INT(__thiscall* OrigFunc_t)(INT);
    INT result = reinterpret_cast<OrigFunc_t>(h->GetDefaultFunc())(thisptr);
    s_in_combat = true;

    // 检测到右键松开标志，弹窗
    if (!s_show_pending && InterlockedCompareExchange(&s_rbutton_up_flag, 0, 1) == 1) {
        s_show_pending = true;
        WriteLog("[HiHook] RButton detected! showing dialog...");
        ShowSettingsDlg();
        s_show_pending = false;
        WriteLog("[HiHook] dialog closed.");
    }

    return result;
}

static void StartPlugin()
{
    WriteLog("战场自动化 开始注册 Hook。");

    // HiHook：战斗循环，检测右键弹窗
    using CycleFunc_t = INT(__stdcall*)(HiHook*, INT);
    _PI->WriteHiHook(0x495C50, SPLICE_, EXTENDED_, THISCALL_,
        static_cast<void*>(static_cast<CycleFunc_t>(&Hook_CycleCombatScreen)));
    WriteLog("HiHook 0x495C50 已注册。");

    // WH_GETMESSAGE 钩子：拦截游戏窗口的鼠标右键消息
    HMODULE hMod = GetModuleHandleA("user32.dll");
    if (hMod) {
        s_getmsg_hook = SetWindowsHookExA(WH_GETMESSAGE, Hook_GetMsgProc, hMod, 0);
        if (s_getmsg_hook) {
            WriteLog("WH_GETMESSAGE 钩子已安装（hHook=0x%X）。", (UINT_PTR)s_getmsg_hook);
        } else {
            DWORD err = GetLastError();
            WriteLog("WH_GETMESSAGE 钩子安装失败！err=%u", err);
        }
    } else {
        WriteLog("获取 user32.dll 模块句柄失败！");
    }

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
        // 卸载钩子
        if (s_getmsg_hook) {
            UnhookWindowsHookEx(s_getmsg_hook);
            s_getmsg_hook = nullptr;
        }
        ResetAutoState();
    }
    return TRUE;
}
