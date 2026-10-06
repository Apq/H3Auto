// ========================================================================
// CrashGuard.inc.cpp - 崩溃自记录 + 钩子铠甲 + 自熔断 + 版本门卫
// ------------------------------------------------------------------------
// 背景（2026-10-06 玩家反馈"游戏崩溃"）：三份日志只能证明两次异常终止
// （日志末尾没有 DLL_PROCESS_DETACH 的收尾重置行），崩溃本身零痕迹——
// 硬崩溃杀进程时任何"代码跑过才写"的日志都不会留痕，除非在进程终止
// 前自己动手写。
//
// 分层防御（设计文档 §18）：
//   L1 崩溃自记录：VEH 记首次机会异常环（8 条），未处理异常报告落盘
//      异常码/出错地址/所在模块+偏移/历史/调用栈（尽力回溯）。
//   L2 钩子铠甲：全部钩子入口 __try/__except，出错落盘 error 并走安全
//      默认值（LoHook=EXEC_DEFAULT 放行原版；HiHook=按原参调原函数；
//      键盘钩子=CallNextHookEx 透传）。插件故障 = 功能跳过，游戏回到
//      原版行为，绝不把异常抛回游戏主循环。
//   L3 异常聚合：同一钩子首次异常记完整详情，之后每 100 次记一行计数；
//      钩子永远保持可用（异常被吞掉后下次调用继续尝试——不做熔断式
//      停用，那会让功能静默失效）。
//   L4 版本门卫：挂任何钩子前校验 SoD 数据指纹（力场表 0x63CF18/2C，
//      实测取证见 H3Note\BattleCrashFix逆向笔记.md），不吻合则只保留
//      日志、不挂钩——防完整版/HotA/改版 exe 上的偏移错配崩溃。
//
// 崩溃上下文安全约束（UEF/VEH/异常过滤器共同遵守）：
//   - 只用静态/栈缓冲 + 内核文件 API（CreateFileW/WriteFile），不碰堆、
//     不拿内核锁、不依赖 CRT 堆状态；_snprintf 只写栈/静态缓冲。
//   - 全程 __try/__except 自包裹 + 重入标志：自身出错静默放行。
//   - 链回前一个 UnhandledExceptionFilter（可能是 HD Mod 的），返回值
//     透传；没有前驱时返回 EXCEPTION_CONTINUE_SEARCH 交回 WER——崩溃
//     表现与不装本模块完全一致。
//   - 捕获不到的（方案边界，如实告知）：TerminateProcess 强杀、
//     __fastfail（系统故意绕过全部过滤器）、栈溢出到过滤器自身无栈
//     可用（尽力而为：跳过栈回溯只写短行）。
// ========================================================================

#include "CrashGuardCore.hpp"

using namespace H3AutoGuard;

// ---- 钩子 id（异常聚合计数用；顺序与 §12 Hook 列表一致）----
enum {
    GHID_BLT = 0,        // 0x600430 LoHook 每帧
    GHID_MSGPROC,        // 0x4746B0 LoHook 战斗消息
    GHID_SHOULD_AUTO,    // 0x4744D0 HiHook 接管判定
    GHID_ACTION_EXEC,    // 0x4786B0 HiHook 动作执行
    GHID_KB,             // WH_KEYBOARD 战斗热键回调
    GHID_COUNT
};

static const char* GuardHookName_(int id)
{
    switch (id) {
    case GHID_BLT:         return "Blt";
    case GHID_MSGPROC:     return "MsgProc";
    case GHID_SHOULD_AUTO: return "ShouldAuto";
    case GHID_ACTION_EXEC: return "ActionExec";
    case GHID_KB:          return "KbHook";
    default:               return "?";
    }
}

// ---- 状态（全部静态，无堆）----
static Ring               s_ring;
static volatile LONG      s_ring_lock = 0;   // 自旋锁（崩溃上下文禁内核锁）
static volatile LONG      s_veh_seen = 0;    // 过滤后的首次机会异常总数
static volatile LONG      s_hook_faults[GHID_COUNT] = {};
static volatile LONG      s_uef_busy = 0;
static LPTOP_LEVEL_EXCEPTION_FILTER s_prev_uef = nullptr;

// ---- 模块解析（崩溃安全：仅 kernel32 查询 + 静态缓冲）----
static bool GuardResolveModule_(unsigned long long addr, char* name_utf8,
    int cap, unsigned long long* base, unsigned long long* offset)
{
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(addr)), &m)
        || !m)
        return false;
    wchar_t wpath[MAX_PATH];
    wpath[0] = 0;
    GetModuleFileNameW(m, wpath, MAX_PATH);
    const wchar_t* base_name = wpath;
    const wchar_t* s1 = wcsrchr(wpath, L'\\');
    const wchar_t* s2 = wcsrchr(wpath, L'/');
    if (s1 && s1 + 1 > base_name) base_name = s1 + 1;
    if (s2 && s2 + 1 > base_name) base_name = s2 + 1;
    if (cap > 0) name_utf8[0] = 0;
    WideCharToMultiByte(CP_UTF8, 0, base_name, -1, name_utf8, cap,
        nullptr, nullptr);
    if (cap > 0) name_utf8[cap - 1] = 0;
    *base = static_cast<unsigned long long>(
        reinterpret_cast<uintptr_t>(m));
    *offset = addr - *base;
    return true;
}

// 把"code+addr(+AV 细节)+模块"落盘成一行 error。崩溃上下文可直接调用。
static void GuardLogOne_(const char* tag, unsigned long code,
    unsigned long long addr, const EXCEPTION_RECORD* er)
{
    char mod[96];
    unsigned long long base = 0, off = 0;
    const bool has_mod = GuardResolveModule_(addr, mod, (int)sizeof(mod),
        &base, &off);
    const char* name = ExceptionCodeName(code);
    if (code == 0xC0000005 && er && er->NumberParameters >= 2) {
        LogError("[Guard] %s 异常 0x%08lX(%s: %s 0x%08llX) addr=0x%08llX%s%s+0x%llX",
            tag, code, name ? name : "未分类",
            AVOperationName(static_cast<unsigned long>(er->ExceptionInformation[0])),
            static_cast<unsigned long long>(er->ExceptionInformation[1]),
            addr, has_mod ? " " : "", has_mod ? mod : "模块未知",
            has_mod ? off : 0ull);
    } else {
        LogError("[Guard] %s 异常 0x%08lX(%s) addr=0x%08llX%s%s+0x%llX",
            tag, code, name ? name : "未分类", addr,
            has_mod ? " " : "", has_mod ? mod : "模块未知",
            has_mod ? off : 0ull);
    }
}

// ---- 钩子铠甲：__except 过滤器（在钩子的外壳里使用）----
// 记录 + 熔断计数，返回 EXCEPTION_EXECUTE_HANDLER（吞掉，走安全默认）。
int GuardCrashFilter_(int hook_id, EXCEPTION_POINTERS* ep)
{
    __try {
        if (!ep || !ep->ExceptionRecord) return EXCEPTION_EXECUTE_HANDLER;
        if (hook_id < 0 || hook_id >= GHID_COUNT) return EXCEPTION_EXECUTE_HANDLER;
        const unsigned long code = ep->ExceptionRecord->ExceptionCode;
        const LONG n = InterlockedIncrement(&s_hook_faults[hook_id]);
        if (n == 1) {
            GuardLogOne_(GuardHookName_(hook_id), code,
                reinterpret_cast<unsigned long long>(
                    ep->ExceptionRecord->ExceptionAddress),
                ep->ExceptionRecord);
        } else if (n % kFaultLogEvery == 0) {
            LogError("[Guard] 钩子 %s 累计异常 %ld 次（每次已吞掉并走安全默认，钩子保持可用）",
                GuardHookName_(hook_id), n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// ---- VEH：首次机会异常进环（仅记录，绝不改分发结果）----
static LONG WINAPI GuardVeh_(PEXCEPTION_POINTERS ep)
{
    __try {
        if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
        const unsigned long code = ep->ExceptionRecord->ExceptionCode;
        if (IsNoiseExceptionCode(code)) return EXCEPTION_CONTINUE_SEARCH;
        InterlockedIncrement(&s_veh_seen);
        int spins = 0;
        while (InterlockedCompareExchange(&s_ring_lock, 1, 0) != 0) {
            if (++spins > 4096) return EXCEPTION_CONTINUE_SEARCH; // 放弃记录，不影响分发
            YieldProcessor();
        }
        RingRecord r = {};
        r.tick_ms = GetTickCount();
        r.tid = GetCurrentThreadId();
        r.code = code;
        r.addr = reinterpret_cast<unsigned long long>(
            ep->ExceptionRecord->ExceptionAddress);
        r.info0 = ep->ExceptionRecord->NumberParameters >= 1
            ? ep->ExceptionRecord->ExceptionInformation[0] : 0;
        r.info1 = ep->ExceptionRecord->NumberParameters >= 2
            ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
        s_ring.Push(r);
        InterlockedExchange(&s_ring_lock, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- UEF：未处理异常全量报告（进程终止前最后的落盘机会）----
static void GuardLogFrame_(unsigned long long addr)
{
    char mod[96];
    unsigned long long base = 0, off = 0;
    if (GuardResolveModule_(addr, mod, (int)sizeof(mod), &base, &off))
        LogError("[Guard]   栈 %s+0x%llX", mod, off);
}

static void GuardWriteFatalReport_(PEXCEPTION_POINTERS ep)
{
    LogError("[Guard] ====== 未处理异常：进程即将终止（崩溃自记录；随后交回系统处理，行为不变）======");
    if (!ep || !ep->ExceptionRecord) {
        LogError("[Guard] 无异常记录指针（ep=NULL），报告到此为止");
        return;
    }
    const EXCEPTION_RECORD* er = ep->ExceptionRecord;
    const unsigned long code = er->ExceptionCode;
    GuardLogOne_("致命", code,
        reinterpret_cast<unsigned long long>(er->ExceptionAddress), er);
    LogError("[Guard] 线程 %lu | 本次之前首次机会异常 %ld 条（缓存最近 %d 条）",
        GetCurrentThreadId(), s_veh_seen, s_ring.Count());

    RingRecord r = {};
    for (int i = 0; s_ring.Get(i, &r); ++i) {
        const char* name = ExceptionCodeName(r.code);
        char mod[96];
        unsigned long long base = 0, off = 0;
        const bool has_mod = GuardResolveModule_(r.addr, mod,
            (int)sizeof(mod), &base, &off);
        if (r.code == 0xC0000005)
            LogError("[Guard]   历史[%d] +%lums 0x%08lX(访问冲突: %s 0x%08llX) %s%s+0x%llX",
                i, r.tick_ms, r.code,
                AVOperationName(static_cast<unsigned long>(r.info0)), r.info1,
                has_mod ? "" : "", has_mod ? mod : "模块未知",
                has_mod ? off : 0ull);
        else
            LogError("[Guard]   历史[%d] +%lums 0x%08lX(%s) %s%s+0x%llX",
                i, r.tick_ms, r.code, name ? name : "未分类",
                has_mod ? "" : "", has_mod ? mod : "模块未知",
                has_mod ? off : 0ull);
    }

    if (code == 0xC00000FD) {
        LogError("[Guard] 栈溢出：跳过调用栈回溯（避免再次耗尽栈）");
    } else {
        void* frames[12] = {};
        const WORD n = CaptureStackBackTrace(2, 12, frames, nullptr);
        if (n == 0)
            LogError("[Guard] 调用栈回溯失败（FPO/栈损坏，尽力而为）");
        else
            for (WORD i = 0; i < n; ++i)
                GuardLogFrame_(reinterpret_cast<unsigned long long>(frames[i]));
    }

    // 各钩子累计异常汇总（归因参考：崩前哪些路径已多次吞异常）。
    for (int id = 0; id < GHID_COUNT; ++id) {
        if (s_hook_faults[id] > 0)
            LogError("[Guard] 钩子 %s 本次会话累计异常 %ld 次",
                GuardHookName_(id), s_hook_faults[id]);
    }
    LogError("[Guard] ====== 报告结束 ======");
}

static LONG WINAPI GuardUef_(PEXCEPTION_POINTERS ep)
{
    // 重入（报告过程自身再崩）时不再写，直接放行，避免递归刷日志。
    if (InterlockedCompareExchange(&s_uef_busy, 1, 0) == 0) {
        __try {
            GuardWriteFatalReport_(ep);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        InterlockedExchange(&s_uef_busy, 0);
    }
    // 链回前一个过滤器（可能是 HD Mod 的崩溃窗）；没有则交回 WER。
    // 返回值透传，绝不改变默认崩溃行为。
    if (s_prev_uef) return s_prev_uef(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- 版本门卫：SoD 数据指纹（挂钩前调用）----
// 校验值来源：本机 SoD exe 文件偏移 0x23CF18/0x23CF2C 实测
// （0x63CF1A=WORD 2、0x63CF1C={0,-16}、0x63CF2E=WORD 3、
//  0x63CF30={0,-16,-34}、表内 +0x10 指针 -> "C15spE1.def"/"C15spE10.def"）。
bool GuardVerifySodBytes_()
{
    bool ok = false;
    __try {
        const WORD n2 = *reinterpret_cast<const WORD*>(0x63CF1A);
        const signed char c2a = *reinterpret_cast<const signed char*>(0x63CF1C);
        const signed char c2b = *reinterpret_cast<const signed char*>(0x63CF1D);
        const WORD n3 = *reinterpret_cast<const WORD*>(0x63CF2C + 2);
        const signed char c3a = *reinterpret_cast<const signed char*>(0x63CF2C + 4);
        const signed char c3b = *reinterpret_cast<const signed char*>(0x63CF2C + 5);
        const signed char c3c = *reinterpret_cast<const signed char*>(0x63CF2C + 6);
        const char* def2 = reinterpret_cast<const char*>(
            *reinterpret_cast<const uintptr_t*>(0x63CF18 + 0x10));
        const char* def3 = reinterpret_cast<const char*>(
            *reinterpret_cast<const uintptr_t*>(0x63CF2C + 0x10));
        ok = ForceFieldTableLooksLikeSod(n2, c2a, c2b, n3, c3a, c3b, c3c,
            def2, def3);
        if (!ok) {
            LogError("[Guard] SoD 数据指纹不匹配：0x63CF18 count=%d 格={%d,%d} / 0x63CF2C count=%d 格={%d,%d,%d} def=\"%s\"/\"%s\" —— 疑似非 SoD 或改版 exe",
                (int)n2, (int)c2a, (int)c2b, (int)n3,
                (int)c3a, (int)c3b, (int)c3c,
                def2 ? def2 : "(空)", def3 ? def3 : "(空)");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogError("[Guard] 版本校验读取异常 code=0x%08lX，保守判定失败",
            GetExceptionCode());
        ok = false;
    }
    return ok;
}

// ---- 安装（Entry/StartPlugin 无条件调用：版本不对也要能记录崩溃）----
void InstallCrashGuard()
{
    if (AddVectoredExceptionHandler(1, GuardVeh_))
        LogInfo("[Guard] 崩溃自记录已启用：VEH 首次机会环 + 未处理异常报告 + 钩子铠甲（异常聚合，钩子保持可用）");
    else
        LogError("[Guard] VEH 安装失败，仅保留未处理异常报告与钩子铠甲");
    s_prev_uef = SetUnhandledExceptionFilter(GuardUef_);
}
