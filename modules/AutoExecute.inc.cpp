// AutoExecute.inc.cpp
// Automated battle execution module

static void WriteLog(const char* fmt, ...);

extern void CommitProfiles(int active_profile, AutoStackRule rules[5][21]);
extern AutoStackRule g_profiles[5][21];
extern AutoStackRule g_active_rules[21];
extern int  g_active_profile;
extern bool IsPanelActive();
extern void CloseSettingsPanel();

// 接管由“每格已提交策略”驱动，不用全局开关/热键：
// 轮到某单位时，若它在当前生效方案里的策略 != 手动，就自动下达动作。
// last_handled_stack 防止同一活动单位在一个回合内被重复下达命令。
static struct {
    void* last_handled_stack;   // 上次已处理的活动单位指针
} g_auto_state;

// 战争机器 creature id（原版内部编号）
enum WarMachineId {
    WM_CATAPULT    = 0x91,  // 145 投石车
    WM_BALLISTA    = 0x92,  // 146 弩车
    WM_FIRST_AID   = 0x93,  // 147 急救帐篷
    WM_AMMO_CART   = 0x94,  // 148 弹药车
    WM_ARROW_TOWER = 0x95,  // 149 箭塔
};

// FUN_0046a080：回放/隐藏战斗判定。非 0 表示当前不是本地人类交互，禁止接管。
static bool IsHiddenBattle(_BattleMgr_* mgr)
{
    return THISCALL_1(char, 0x46A080, mgr) != 0;
}

void ResetAutoState()
{
    if (IsPanelActive())
        CloseSettingsPanel();
    g_auto_state.last_handled_stack = nullptr;
    // 策略是本进程内的已确认设置，战斗状态重置时保留。
    WriteLog("Auto state reset; confirmed strategies preserved.");
}

int GetAliveStacks(_BattleStack_* out_stacks[], int max_count, int side)
{
    int n = 0;
    for (int i = 0; i < 21 && n < max_count; ++i) {
        if (o_BattleMgr->stack[side][i].count_current > 0 && o_BattleMgr->stack[side][i].count_at_start > 0)
            out_stacks[n++] = &o_BattleMgr->stack[side][i];
    }
    return n;
}

int GetEnemyStacks(_BattleStack_* out_stacks[], int max_count, int my_side)
{
    int enemy_side = 1 - my_side;
    int n = 0;
    for (int i = 0; i < 21 && n < max_count; ++i) {
        if (o_BattleMgr->stack[enemy_side][i].count_current > 0 && o_BattleMgr->stack[enemy_side][i].count_at_start > 0)
            out_stacks[n++] = &o_BattleMgr->stack[enemy_side][i];
    }
    return n;
}

// 英雄二级技能索引（second_skill[28]）
enum SecSkillIndex {
    SK_BALLISTICS = 10,  // 弹道术（投石车）
    SK_ARTILLERY  = 20,  // 炮术（弩车/箭塔）
    SK_FIRST_AID  = 27,  // 急救术（急救帐篷）
};

// 当前行动方英雄是否掌握某二级技能（等级 > 0）。
static bool HeroHasSkill(_BattleMgr_* mgr, int skill_index)
{
    if (!mgr) return false;
    int side = mgr->current_active_side;
    if (side < 0 || side > 1) return false;
    _Hero_* hero = mgr->hero[side];
    if (!hero) return false;
    return hero->second_skill[skill_index] > 0;
}

// CommitProfiles：勾号/Enter 一次性提交全部 5 套内存方案，当前选中方案立即生效。
void CommitProfiles(int active_profile, AutoStackRule rules[5][21])
{
    if (active_profile < 0 || active_profile >= 5)
        active_profile = 0;
    memcpy(g_profiles, rules, sizeof(g_profiles));
    g_active_profile = active_profile;
    memcpy(g_active_rules, g_profiles[g_active_profile], sizeof(g_active_rules));
    WriteLog("[Auto] 5 profiles committed; active profile=%d", g_active_profile + 1);
}

// 接管决策结果：判定点注入只能表达两种结果。
enum TakeoverDecision {
    TD_LEAVE_ORIGINAL = 0,  // 不干预：保持原版返回值
    TD_HAND_TO_AI     = 1,  // 交回 AI
};

// 主动动作提交尚未落地；“按设置执行”分支暂时不干预，待后续实现。
static const bool AUTO_ACTION_EXECUTION_READY = false;

// DecideTakeover：按最终规范判定当前活动单位。
// 1. 弹药车：永远不干预。
// 2. 急救帐篷：有急救术且配置了急救治疗→主动执行（未就绪则暂不干预）；否则不干预。
// 3. 投石车：配置了投石车攻击→主动执行；否则有弹道术→不干预，无→交回AI。
// 4. 弩车/箭塔：配置了远程攻击→主动执行；否则有炮术→不干预，无→交回AI。
// 5. 其它部队：手动→不干预；非手动→主动执行（绝不交回AI）。
int DecideTakeover(_BattleMgr_* mgr)
{
    if (!mgr) return TD_LEAVE_ORIGINAL;
    if (mgr->auto_combat) return TD_LEAVE_ORIGINAL;
    if (IsHiddenBattle(mgr)) return TD_LEAVE_ORIGINAL;
    _BattleStack_* stack = mgr->active_stack;
    if (!stack || stack->count_current <= 0) return TD_LEAVE_ORIGINAL;

    int idx = stack->army_slot_ix;
    if (idx < 0 || idx >= 21) return TD_LEAVE_ORIGINAL;
    const AutoStackRule& rule = g_active_rules[idx];
    const int cid = stack->creature_id;

    switch (cid) {
    case WM_AMMO_CART:
        return TD_LEAVE_ORIGINAL;

    case WM_FIRST_AID:
        // 无急救术时面板已排除；有急救术且配置了治疗→主动执行。
        if (rule.action == AA_FIRST_AID) {
            if (AUTO_ACTION_EXECUTION_READY) {
                // TODO: 主动急救。
            }
            WriteLog("[Auto] tent slot #%d first-aid (主动执行未就绪，暂不干预)", idx);
            return TD_LEAVE_ORIGINAL;
        }
        return TD_LEAVE_ORIGINAL;

    case WM_CATAPULT:
        if (rule.action == AA_CATAPULT) {
            if (AUTO_ACTION_EXECUTION_READY) {
                // TODO: 主动投石。
            }
            WriteLog("[Auto] catapult slot #%d (主动执行未就绪，暂不干预)", idx);
            return TD_LEAVE_ORIGINAL;
        }
        if (HeroHasSkill(mgr, SK_BALLISTICS))
            return TD_LEAVE_ORIGINAL;
        return TD_HAND_TO_AI;

    case WM_BALLISTA:
    case WM_ARROW_TOWER:
        if (rule.action == AA_RANGED_ATTACK) {
            if (AUTO_ACTION_EXECUTION_READY) {
                // TODO: 主动远程。
            }
            WriteLog("[Auto] ballista/tower slot #%d (主动执行未就绪，暂不干预)", idx);
            return TD_LEAVE_ORIGINAL;
        }
        if (HeroHasSkill(mgr, SK_ARTILLERY))
            return TD_LEAVE_ORIGINAL;
        return TD_HAND_TO_AI;

    default:
        if (rule.action == AA_MANUAL)
            return TD_LEAVE_ORIGINAL;
        if (AUTO_ACTION_EXECUTION_READY) {
            // TODO: 主动执行；失败且允许降级→防御。
        }
        WriteLog("[Auto] unit slot #%d action=%d (主动执行未就绪，暂不干预)",
            idx, (int)rule.action);
        return TD_LEAVE_ORIGINAL;
    }
}

// ==== 接管钩子：判定点注入 ====
// FUN_004744d0 @ 0x4744D0 是原版“当前活动单位是否交自动执行”的最窄查询：
//   char c = FUN_0046a080(); if (c) return 1; return FUN_00474520(...);
// 返回非 0 = 走自动/AI 执行；返回 0 = 等待本地人类输入。
// 我们在原函数返回 0（等待人类）时，若该单位应被接管，则改返回 1，
// 让原版把它当作自动执行——复用原版目标选择、动画、回合推进，零手写动作码。
int __stdcall HH_ShouldAutoExecute(HiHook* h, _BattleMgr_* This)
{
    int orig = THISCALL_1(int, h->GetDefaultFunc(), This);
    if (orig != 0)
        return orig;            // 原版已判定自动执行，保持不变
    __try {
        if (DecideTakeover(This) == TD_HAND_TO_AI)
            return 1;           // 交回 AI：原版自动执行该单位
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
