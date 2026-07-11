// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册

// 前向声明
extern void ResetAutoState();
extern void ShowSettingsDlg();

// ================================================
// 策略：
//   SetWindowLongPtr 子类化游戏窗口，拦截 WM_RBUTTONUP
//   收到右键后启动子线程，在子线程里弹窗
//   HiHook 0x495C50 — 战斗循环
// ================================================

// ---- 全局状态 ----
static bool s_in_combat = false;
static bool s_subclassed = false;
static volatile bool s_dlg_thread_running = false;
static volatile bool s_dlg_scheduled = false;
static WNDPROC s_old_wndproc = nullptr;
static HANDLE s_dlg_thread = nullptr;

// ---- 子线程：弹窗 + 维护消息循环 ----
static DWORD WINAPI DlgThreadProc(LPVOID)
{
    WriteLog("[Thread] 子线程启动。");

    // 创建独立的消息窗口（H3Dlg 需要）
    const char szClass[] = "H3AutoMsgWnd";
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = szClass;
    wc.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    ATOM atom = RegisterClassA(&wc);
    if (!atom) {
        WriteLog("[Thread] RegisterClass 失败！");
        s_dlg_thread_running = false;
        return 0;
    }

    HWND hwndMsg = CreateWindowExA(
        0, szClass, "H3Auto",
        WS_OVERLAPPEDWINDOW,
        0, 0, 800, 600,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    if (!hwndMsg) {
        WriteLog("[Thread] CreateWindowEx 失败！");
        if (atom) UnregisterClassA(szClass, wc.hInstance);
        s_dlg_thread_running = false;
        return 0;
    }
    ShowWindow(hwndMsg, SW_HIDE);
    WriteLog("[Thread] 消息窗口已创建(0x%X)。", (UINT_PTR)hwndMsg);

    // 等待主线程通知弹窗
    while (true) {
        MSG msg;
        BOOL got = PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE);
        if (!got) {
            if (s_dlg_scheduled) {
                s_dlg_scheduled = false;
                WriteLog("[Thread] 收到弹窗通知，开始弹窗...");
                ShowSettingsDlg();
                WriteLog("[Thread] 弹窗结束。");
            } else {
                DWORD wait = MsgWaitForMultipleObjectsEx(
                    0, nullptr, INFINITE,
                    QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (wait == WAIT_FAILED) break;
            }
            continue;
        }

        if (msg.message == WM_QUIT) {
            WriteLog("[Thread] 收到 WM_QUIT，退出。");
            break;
        }

        if (msg.message == WM_USER + 0x4841) {
            WriteLog("[Thread] 收到弹窗消息，开始弹窗...");
            ShowSettingsDlg();
            WriteLog("[Thread] 弹窗结束。");
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (hwndMsg) DestroyWindow(hwndMsg);
    if (atom) UnregisterClassA(szClass, wc.hInstance);
    WriteLog("[Thread] 子线程结束。");
    s_dlg_thread_running = false;
    s_dlg_scheduled = false;
    return 0;
}

// ---- 启动弹窗子线程 ----
static void EnsureDlgThread()
{
    if (s_dlg_thread_running && s_dlg_thread) {
        DWORD tid = GetThreadId(s_dlg_thread);
        PostThreadMessageA(tid, WM_USER + 0x4841, 0, 0);
        return;
    }

    s_dlg_thread_running = true;
    s_dlg_scheduled = false;
    DWORD tid = 0;
    s_dlg_thread = CreateThread(nullptr, 0, DlgThreadProc, nullptr, 0, &tid);
    if (s_dlg_thread) {
        CloseHandle(s_dlg_thread);
        WriteLog("[Thread] 弹窗子线程已启动(tid=%u)。", tid);
        Sleep(50);
        PostThreadMessageA(tid, WM_USER + 0x4841, 0, 0);
    } else {
        s_dlg_thread_running = false;
        DWORD err = GetLastError();
        WriteLog("[Thread] CreateThread 失败！err=%u", err);
    }
}

// ---- 窗口子类过程 ----
static LRESULT CALLBACK SubclassWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_RBUTTONUP) {
        WriteLog("[Subclass] WM_RBUTTONUP at (%d,%d)", LOWORD(lParam), HIWORD(lParam));
        EnsureDlgThread();
    }
    return CallWindowProcW(s_old_wndproc, hwnd, msg, wParam, lParam);
}

// ---- 安装窗口子类 ----
static void InstallSubclass()
{
    if (s_subclassed) return;

    HWND hwnd = H3Hwnd::Get();
    if (!hwnd || !IsWindow(hwnd)) {
        WriteLog("[Subclass] 游戏窗口尚未就绪。");
        return;
    }

    s_old_wndproc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)SubclassWndProc);
    if (s_old_wndproc) {
        s_subclassed = true;
        WriteLog("[Subclass] 窗口子类已安装（原过程=0x%X）。", (UINT_PTR)s_old_wndproc);
    } else {
        DWORD err = GetLastError();
        WriteLog("[Subclass] 窗口子类安装失败！err=%u", err);
    }
}

// ---- 卸载窗口子类 ----
static void UninstallSubclass()
{
    if (!s_subclassed || !s_old_wndproc) return;

    HWND hwnd = H3Hwnd::Get();
    if (hwnd && IsWindow(hwnd)) {
        LONG_PTR old = SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)s_old_wndproc);
        if (old == (LONG_PTR)SubclassWndProc) {
            WriteLog("[Subclass] 窗口子类已卸载。");
        }
    }
    s_subclassed = false;
    s_old_wndproc = nullptr;
}

// ---- 战斗循环 HiHook ----
static INT __stdcall Hook_CycleCombatScreen(HiHook* h, INT thisptr)
{
    typedef INT(__thiscall* OrigFunc_t)(INT);
    INT result = reinterpret_cast<OrigFunc_t>(h->GetDefaultFunc())(thisptr);
    s_in_combat = true;

    if (!s_subclassed) {
        InstallSubclass();
    }

    return result;
}

static void StartPlugin()
{
    WriteLog("战场自动化 开始注册 Hook。");

    using CycleFunc_t = INT(__stdcall*)(HiHook*, INT);
    _PI->WriteHiHook(0x495C50, SPLICE_, EXTENDED_, THISCALL_,
        static_cast<void*>(static_cast<CycleFunc_t>(&Hook_CycleCombatScreen)));
    WriteLog("HiHook 0x495C50 已注册。");

    InstallSubclass();

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
        // 通知子线程退出
        if (s_dlg_thread_running && s_dlg_thread) {
            DWORD tid = GetThreadId(s_dlg_thread);
            PostThreadMessageA(tid, WM_QUIT, 0, 0);
        }
        UninstallSubclass();
        ResetAutoState();
    }
    return TRUE;
}
