// ========== Entry.inc.cpp ==========
// 插件入口与 Hook 注册
// 【测试阶段】逐个加回，WH_GETMESSAGE 已移除（会锁住 DLL）

// 前向声明
extern void ResetAutoState();
extern void ShowSettingsDlg();

// ================================================
// 策略：
//   HiHook 0x495C50 — 战斗循环，自己轮询鼠标右键状态
//   LoHook 0x600430 — Blt 完成后弹窗（BattleValueInfo 不冲突）
//
// 不使用 WH_GETMESSAGE 钩子（会锁住 DLL 导致无法释放）
// ================================================

// ---- 全局状态 ----
static bool s_rbutton_was_down_prev = false; // 上一帧右键状态
static bool s_show_pending = false;
static bool s_in_combat = false;

// ---- 战斗循环 HiHook：自己轮询鼠标右键 ----
static INT __stdcall Hook_CycleCombatScreen(HiHook* h, INT thisptr)
{
    typedef INT(__thiscall* OrigFunc_t)(INT);
    INT result = reinterpret_cast<OrigFunc_t>(h->GetDefaultFunc())(thisptr);
    s_in_combat = true;

    // GetAsyncKeyState 高位=1 表示当前按下；低 1 位=1 表示"上次调用后按下的"
    SHORT state = GetAsyncKeyState(VK_RBUTTON);
    bool rbutton_down_now = (state < 0);  // 高位=1：当前按下

    if (s_rbutton_was_down_prev && !rbutton_down_now) {
        // 上一帧按下，本帧松开：触发弹窗
        s_show_pending = true;
        WriteLog("[Cycle] RButton up, pending=1");
    }

    s_rbutton_was_down_prev = rbutton_down_now;
    return result;
}

// ---- Blt 完成后 LoHook ----
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

    // 战斗循环：设置战斗中标志 + 自己轮询鼠标
    using CycleFunc_t = INT(__stdcall*)(HiHook*, INT);
    _PI->WriteHiHook(0x495C50, SPLICE_, EXTENDED_, THISCALL_,
        static_cast<void*>(static_cast<CycleFunc_t>(&Hook_CycleCombatScreen)));
    WriteLog("HiHook 0x495C50 已注册（战斗循环+鼠标轮询）。");

    // Blt 完成后：延迟弹窗
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
        ResetAutoState();
    }
    return TRUE;
}
