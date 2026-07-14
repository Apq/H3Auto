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

// 控制权三态：
// KEEP_ORIGINAL = 保持原版（人类输入/原版逻辑）
// HAND_TO_AI    = 交给原版 AI
// EXECUTE_H3AUTO= 本回合由 H3Auto 提交主动动作
enum ControlDecision {
    CD_KEEP_ORIGINAL = 0,
    CD_HAND_TO_AI    = 1,
    CD_EXECUTE_H3AUTO = 2,
};

// 原版 eBattleAction 值（与 H3API 一致）
enum BattleActionCode {
    BA_DEFEND = 3,
    BA_SHOOT  = 7,
};

static bool IsWarMachineCid_(int cid)
{
    return cid == WM_CATAPULT || cid == WM_BALLISTA
        || cid == WM_FIRST_AID || cid == WM_AMMO_CART
        || cid == WM_ARROW_TOWER;
}

// 选择远程目标：按选择器从敌方存活部队中挑一个，并过 CanShoot。
static _BattleStack_* SelectRangedTarget_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule)
{
    if (!mgr || !self) return nullptr;
    const int my_side = self->def_group_ix;
    const int enemy_side = 1 - my_side;

    _BattleStack_* candidates[21] = {};
    int count = 0;
    for (int i = 0; i < 21 && count < 21; ++i) {
        _BattleStack_* t = &mgr->stack[enemy_side][i];
        if (t->count_current <= 0 || t->count_at_start <= 0) continue;
        if (t->creature_id < 0) continue;
        if (!self->CanShoot(t)) continue;
        candidates[count++] = t;
    }
    if (count <= 0) return nullptr;

    // 固定部队：side+slot 指纹
    if (rule.target.selector == SEL_FIXED
        && rule.target.fixedSide >= 0 && rule.target.fixedSlot >= 0
        && rule.target.fixedSide <= 1 && rule.target.fixedSlot < 21)
    {
        _BattleStack_* t = &mgr->stack[rule.target.fixedSide][rule.target.fixedSlot];
        if (t->count_current > 0
            && (rule.target.fixedCreatureId < 0
                || t->creature_id == rule.target.fixedCreatureId)
            && self->CanShoot(t))
            return t;
        return nullptr;
    }

    switch (rule.target.selector) {
    case SEL_SEQUENTIAL: {
        // 简单顺序：按 army_slot_ix 升序第一个
        _BattleStack_* best = candidates[0];
        for (int i = 1; i < count; ++i)
            if (candidates[i]->army_slot_ix < best->army_slot_ix)
                best = candidates[i];
        return best;
    }
    case SEL_COUNT_HIGH: {
        _BattleStack_* best = candidates[0];
        for (int i = 1; i < count; ++i)
            if (candidates[i]->count_current > best->count_current)
                best = candidates[i];
        return best;
    }
    case SEL_COUNT_LOW: {
        _BattleStack_* best = candidates[0];
        for (int i = 1; i < count; ++i)
            if (candidates[i]->count_current < best->count_current)
                best = candidates[i];
        return best;
    }
    case SEL_NEAREST:
    case SEL_FARTHEST: {
        _BattleStack_* best = candidates[0];
        int best_d = abs(best->hex_ix - self->hex_ix);
        for (int i = 1; i < count; ++i) {
            int d = abs(candidates[i]->hex_ix - self->hex_ix);
            if ((rule.target.selector == SEL_NEAREST && d < best_d)
                || (rule.target.selector == SEL_FARTHEST && d > best_d))
            {
                best = candidates[i];
                best_d = d;
            }
        }
        return best;
    }
    case SEL_RANGED_SPEED:
        // 暂无 creature speed 细节字段时，回退随机
    case SEL_RANDOM:
    default:
        return candidates[rand() % count];
    }
}

// 提交防御：仅写 battle->action=3，由主循环自然进入 FUN_004786b0。
static bool SubmitDefend_(_BattleMgr_* mgr, _BattleStack_* self)
{
    if (!mgr || !self) return false;
    if (mgr->action != 0) return false; // 已有动作
    mgr->action = BA_DEFEND;
    mgr->action_parameter = -1;
    mgr->action_target = -1;
    mgr->action_parameter2 = 0;
    g_auto_state.last_handled_stack = self;
    WriteLog("[Auto] submit DEFEND slot=%d creature=0x%X",
        self->army_slot_ix, self->creature_id);
    return true;
}

// 提交远程攻击：action=7, actionTarget=目标 hex。
static bool SubmitRanged_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    if (!mgr || !self) return false;
    if (mgr->action != 0) return false;
    _BattleStack_* target = SelectRangedTarget_(mgr, self, rule);
    if (!target) return false;
    mgr->action = BA_SHOOT;
    mgr->action_parameter = -1;
    mgr->action_target = target->hex_ix;
    mgr->action_parameter2 = 0;
    g_auto_state.last_handled_stack = self;
    WriteLog("[Auto] submit SHOOT slot=%d -> hex=%d target_slot=%d",
        self->army_slot_ix, target->hex_ix, target->army_slot_ix);
    return true;
}

// 尝试按规则提交主动动作；成功返回 true。
static bool TrySubmitConfiguredAction_(_BattleMgr_* mgr)
{
    if (!mgr) return false;
    if (mgr->auto_combat) return false;
    if (IsHiddenBattle(mgr)) return false;
    if (mgr->action != 0) return false;
    _BattleStack_* self = mgr->active_stack;
    if (!self || self->count_current <= 0) return false;
    if (g_auto_state.last_handled_stack == self) return false; // 本单位已处理

    const int idx = self->army_slot_ix;
    if (idx < 0 || idx >= 21) return false;
    const AutoStackRule& rule = g_active_rules[idx];
    const int cid = self->creature_id;

    // 弹药车永不主动。
    if (cid == WM_AMMO_CART)
        return false;

    // 急救帐篷：仅配置了急救时主动（暂未实现治疗提交）。
    if (cid == WM_FIRST_AID) {
        if (rule.action != AA_FIRST_AID) return false;
        WriteLog("[Auto] tent first-aid not implemented yet, leave original");
        return false;
    }

    // 投石车：仅配置了投石时主动（暂未实现）。
    if (cid == WM_CATAPULT) {
        if (rule.action != AA_CATAPULT) return false;
        WriteLog("[Auto] catapult action not implemented yet, leave original");
        return false;
    }

    // 弩车/箭塔/普通远程：远程攻击
    if (rule.action == AA_RANGED_ATTACK) {
        if (SubmitRanged_(mgr, self, rule))
            return true;
        // 失败：普通部队可降级防御；战争机器不降级
        if (!IsWarMachineCid_(cid) && rule.allowDefendFallback)
            return SubmitDefend_(mgr, self);
        return false;
    }

    // 防御
    if (rule.action == AA_DEFEND)
        return SubmitDefend_(mgr, self);

    // 其它动作（移动/近战/等待/急救/投石）尚未实现。
    // 普通部队失败时若允许降级则防御。
    if (!IsWarMachineCid_(cid) && rule.action != AA_MANUAL) {
        if (rule.allowDefendFallback)
            return SubmitDefend_(mgr, self);
        WriteLog("[Auto] action %d not implemented; leave original slot=%d",
            (int)rule.action, idx);
    }
    return false;
}

// DecideTakeover：仅用于 FUN_004744d0 返回值改写（交回AI / 保持原版）。
// 主动执行不在此函数写动作，而在 TrySubmitConfiguredAction_。
int DecideTakeover(_BattleMgr_* mgr)
{
    if (!mgr) return CD_KEEP_ORIGINAL;
    if (mgr->auto_combat) return CD_KEEP_ORIGINAL;
    if (IsHiddenBattle(mgr)) return CD_KEEP_ORIGINAL;
    _BattleStack_* stack = mgr->active_stack;
    if (!stack || stack->count_current <= 0) return CD_KEEP_ORIGINAL;

    int idx = stack->army_slot_ix;
    if (idx < 0 || idx >= 21) return CD_KEEP_ORIGINAL;
    const AutoStackRule& rule = g_active_rules[idx];
    const int cid = stack->creature_id;

    switch (cid) {
    case WM_AMMO_CART:
        return CD_KEEP_ORIGINAL;

    case WM_FIRST_AID:
        // 配置了急救→主动执行；否则保持原版。
        if (rule.action == AA_FIRST_AID)
            return CD_EXECUTE_H3AUTO;
        return CD_KEEP_ORIGINAL;

    case WM_CATAPULT:
        if (rule.action == AA_CATAPULT)
            return CD_EXECUTE_H3AUTO;
        if (HeroHasSkill(mgr, SK_BALLISTICS))
            return CD_KEEP_ORIGINAL;
        return CD_HAND_TO_AI;

    case WM_BALLISTA:
    case WM_ARROW_TOWER:
        if (rule.action == AA_RANGED_ATTACK)
            return CD_EXECUTE_H3AUTO;
        if (HeroHasSkill(mgr, SK_ARTILLERY))
            return CD_KEEP_ORIGINAL;
        return CD_HAND_TO_AI;

    default:
        if (rule.action == AA_MANUAL)
            return CD_KEEP_ORIGINAL;
        return CD_EXECUTE_H3AUTO; // 绝不交回 AI
    }
}

// 供战斗消息入口调用：若当前单位应由 H3Auto 主动执行，则提交动作。
// 返回 true 表示已写入 action，原版输入可继续走默认路径处理后续。
bool TryAutoExecuteActiveStack()
{
    _BattleMgr_* mgr = o_BattleMgr;
    if (!mgr) return false;
    __try {
        const int decision = DecideTakeover(mgr);
        if (decision != CD_EXECUTE_H3AUTO)
            return false;
        return TrySubmitConfiguredAction_(mgr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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
        // 活动单位变化时允许重新提交
        if (This && This->active_stack
            && g_auto_state.last_handled_stack != This->active_stack)
        {
            // 不在这里清空 last_handled；由提交成功时写入
        }
        if (This && This->active_stack
            && g_auto_state.last_handled_stack
            && g_auto_state.last_handled_stack != This->active_stack)
        {
            g_auto_state.last_handled_stack = nullptr;
        }

        const int decision = DecideTakeover(This);
        if (decision == CD_HAND_TO_AI)
            return 1;           // 交回 AI
        // CD_EXECUTE_H3AUTO / CD_KEEP_ORIGINAL：返回 0，走人类输入路径；
        // 主动动作在 Hook_BattleMsgProc 入口提交。
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
