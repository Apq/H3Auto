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

// 原版 eBattleAction 值（与 H3API / FUN_00476500 映射一致）
enum BattleActionCode {
    BA_WALK        = 2,
    BA_DEFEND      = 3,
    BA_WALK_ATTACK = 6,
    BA_SHOOT       = 7,
    BA_WAIT        = 8,
    BA_CATAPULT    = 9,
    BA_FIRST_AID   = 11,
};

static bool IsWarMachineCid_(int cid)
{
    return cid == WM_CATAPULT || cid == WM_BALLISTA
        || cid == WM_FIRST_AID || cid == WM_AMMO_CART
        || cid == WM_ARROW_TOWER;
}

// 通用部队选择：side_filter = 0己方 / 1敌方 / 2双方；require_wounded 用于急救。
static _BattleStack_* SelectStackTarget_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule, int side_filter, bool require_wounded,
    bool require_can_shoot)
{
    if (!mgr || !self) return nullptr;

    // 固定部队优先
    if (rule.target.selector == SEL_FIXED
        && rule.target.fixedSide >= 0 && rule.target.fixedSlot >= 0
        && rule.target.fixedSide <= 1 && rule.target.fixedSlot < 21)
    {
        _BattleStack_* t = &mgr->stack[rule.target.fixedSide][rule.target.fixedSlot];
        if (t->count_current > 0
            && (rule.target.fixedCreatureId < 0
                || t->creature_id == rule.target.fixedCreatureId)
            && (!require_can_shoot || self->CanShoot(t))
            && (!require_wounded || t->lost_hp > 0 || t->count_current < t->count_at_start))
        {
            if (side_filter == 2
                || (side_filter == 0 && t->def_group_ix == self->def_group_ix)
                || (side_filter == 1 && t->def_group_ix != self->def_group_ix))
                return t;
        }
        return nullptr;
    }

    _BattleStack_* candidates[42] = {};
    int count = 0;
    for (int side = 0; side < 2; ++side) {
        if (side_filter == 0 && side != self->def_group_ix) continue;
        if (side_filter == 1 && side == self->def_group_ix) continue;
        for (int i = 0; i < 21 && count < 42; ++i) {
            _BattleStack_* t = &mgr->stack[side][i];
            if (t == self) continue;
            if (t->count_current <= 0 || t->count_at_start <= 0) continue;
            if (t->creature_id < 0) continue;
            if (require_can_shoot && !self->CanShoot(t)) continue;
            if (require_wounded
                && t->lost_hp <= 0 && t->count_current >= t->count_at_start)
                continue;
            candidates[count++] = t;
        }
    }
    if (count <= 0) return nullptr;

    switch (rule.target.selector) {
    case SEL_SEQUENTIAL: {
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
    case SEL_MOST_WOUNDED: {
        _BattleStack_* best = candidates[0];
        int best_loss = best->count_at_start - best->count_current;
        if (best_loss < 0) best_loss = 0;
        best_loss = best_loss * 1000 + best->lost_hp;
        for (int i = 1; i < count; ++i) {
            int loss = candidates[i]->count_at_start - candidates[i]->count_current;
            if (loss < 0) loss = 0;
            loss = loss * 1000 + candidates[i]->lost_hp;
            if (loss > best_loss) {
                best = candidates[i];
                best_loss = loss;
            }
        }
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
    case SEL_RANDOM:
    default:
        return candidates[rand() % count];
    }
}

// 解析位置目标：固定 hex 优先，否则用部队目标的位置，或随机合法邻格占位。
static int ResolvePositionTarget_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule)
{
    if (!mgr || !self) return -1;
    if (rule.target.kind == AT_POSITION
        && rule.target.fixedHex >= 1 && rule.target.fixedHex <= 185)
        return rule.target.fixedHex;

    // 部队作为靠近锚点：返回该部队 hex
    int side_filter = 2;
    if (rule.target.side == ATS_OWN) side_filter = 0;
    else if (rule.target.side == ATS_ENEMY) side_filter = 1;
    _BattleStack_* t = SelectStackTarget_(mgr, self, rule, side_filter, false, false);
    if (t) return t->hex_ix;
    return -1;
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

static bool WriteAction_(_BattleMgr_* mgr, _BattleStack_* self,
    int action, int param, int target_hex)
{
    if (!mgr || !self) return false;
    if (mgr->action != 0) return false;
    mgr->action = action;
    mgr->action_parameter = param;
    mgr->action_target = target_hex;
    mgr->action_parameter2 = 0;
    g_auto_state.last_handled_stack = self;
    return true;
}

// 提交远程攻击：action=7, actionTarget=目标 hex。
static bool SubmitRanged_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    _BattleStack_* target = SelectStackTarget_(mgr, self, rule, 1, false, true);
    if (!target) return false;
    if (!WriteAction_(mgr, self, BA_SHOOT, -1, target->hex_ix))
        return false;
    WriteLog("[Auto] submit SHOOT slot=%d -> hex=%d target_slot=%d",
        self->army_slot_ix, target->hex_ix, target->army_slot_ix);
    return true;
}

// 提交移动：action=2, actionTarget=目标 hex。
// 注：可达性未做完整原版寻路校验；固定位置优先，否则靠近目标部队格子。
static bool SubmitMove_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    int hex = ResolvePositionTarget_(mgr, self, rule);
    if (hex < 1 || hex > 185) return false;
    if (hex == self->hex_ix) return false;
    if (!WriteAction_(mgr, self, BA_WALK, -1, hex))
        return false;
    WriteLog("[Auto] submit WALK slot=%d -> hex=%d",
        self->army_slot_ix, hex);
    return true;
}

// 判断某 hex 是否被敌方部队占据（双格头/尾都算）。
// 尾格用 GetSecondSquare(0x4463C0)；失败则仅比头格。
static _BattleStack_* FindEnemyOccupyingHex_(_BattleMgr_* mgr, _BattleStack_* self, int hex)
{
    if (!mgr || !self || hex < 1 || hex > 185) return nullptr;
    const int enemy_side = 1 - self->def_group_ix;
    for (int i = 0; i < 21; ++i) {
        _BattleStack_* t = &mgr->stack[enemy_side][i];
        if (t->count_current <= 0 || t->count_at_start <= 0) continue;
        if (t->creature_id < 0) continue;
        if (t->hex_ix == hex) return t;
        int second = -1;
        __try {
            second = THISCALL_1(int, 0x4463C0, t);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            second = -1;
        }
        if (second == hex) return t;
    }
    return nullptr;
}

// 提交近战：配置 = 站立格(meleeStandHex) + 攻击格(meleeAttackHex)。
// 规则：攻击格上有敌人（头或尾）才出手；站立格为 -1 表示保持当前位置。
// 提交：action=6, actionTarget=攻击格, actionParameter=站立格
// （对应 FUN_00476500 case7 的 0x132d4/0x132d8）。
// 同时写入 mouse_coord/attacker_coord，贴近玩家点击后的状态。
static bool SubmitMelee_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    if (!mgr || !self) return false;
    const int attack_hex = rule.target.meleeAttackHex;
    if (attack_hex < 1 || attack_hex > 185) {
        WriteLog("[Auto] melee missing attack hex slot=%d", self->army_slot_ix);
        return false;
    }

    _BattleStack_* enemy = FindEnemyOccupyingHex_(mgr, self, attack_hex);
    if (!enemy) {
        WriteLog("[Auto] melee attack hex=%d empty (no enemy head/tail) slot=%d",
            attack_hex, self->army_slot_ix);
        return false;
    }

    int stand_hex = rule.target.meleeStandHex;
    if (stand_hex < 1 || stand_hex > 185)
        stand_hex = self->hex_ix; // 未设站立格：原地攻击

    // 写悬停相关字段，模拟玩家点过该攻击格
    mgr->mouse_coord = attack_hex;
    mgr->attacker_coord = stand_hex;
    mgr->move_type = 7; // 近战悬停类型（FUN_00475dc0 返回 7）

    if (!WriteAction_(mgr, self, BA_WALK_ATTACK, stand_hex, attack_hex))
        return false;
    WriteLog("[Auto] submit MELEE stand=%d attack=%d enemy_slot=%d enemy_hex=%d",
        stand_hex, attack_hex, enemy->army_slot_ix, enemy->hex_ix);
    return true;
}

// 提交等待：action=8。
static bool SubmitWait_(_BattleMgr_* mgr, _BattleStack_* self)
{
    if (!WriteAction_(mgr, self, BA_WAIT, -1, -1))
        return false;
    WriteLog("[Auto] submit WAIT slot=%d", self->army_slot_ix);
    return true;
}

// 提交急救：action=11, actionTarget=己方伤员 hex。
static bool SubmitFirstAid_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    _BattleStack_* target = SelectStackTarget_(mgr, self, rule, 0, true, false);
    if (!target) return false;
    if (!WriteAction_(mgr, self, BA_FIRST_AID, -1, target->hex_ix))
        return false;
    WriteLog("[Auto] submit FIRST_AID slot=%d -> hex=%d target_slot=%d",
        self->army_slot_ix, target->hex_ix, target->army_slot_ix);
    return true;
}

// 提交投石：action=9, actionTarget=城墙/位置 hex。
// 城墙合法性未完整复用 FUN_00473530；先按位置目标提交。
static bool SubmitCatapult_(_BattleMgr_* mgr, _BattleStack_* self, const AutoStackRule& rule)
{
    int hex = ResolvePositionTarget_(mgr, self, rule);
    if (hex < 1 || hex > 185) {
        // 无配置位置时给一个常见城墙 hex 占位（后续替换为原版城墙枚举）
        hex = rule.target.fixedHex;
        if (hex < 1 || hex > 185) return false;
    }
    if (!WriteAction_(mgr, self, BA_CATAPULT, -1, hex))
        return false;
    WriteLog("[Auto] submit CATAPULT slot=%d -> hex=%d",
        self->army_slot_ix, hex);
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

    auto fallback_or_fail = [&](bool ok) -> bool {
        if (ok) return true;
        // 战争机器不降级；普通部队可降级防御。
        if (!IsWarMachineCid_(cid) && rule.allowDefendFallback)
            return SubmitDefend_(mgr, self);
        return false;
    };

    switch (rule.action) {
    case AA_MANUAL:
        return false;
    case AA_DEFEND:
        return SubmitDefend_(mgr, self);
    case AA_WAIT:
        return fallback_or_fail(SubmitWait_(mgr, self));
    case AA_MOVE:
        return fallback_or_fail(SubmitMove_(mgr, self, rule));
    case AA_MELEE_ATTACK:
        return fallback_or_fail(SubmitMelee_(mgr, self, rule));
    case AA_RANGED_ATTACK:
        return fallback_or_fail(SubmitRanged_(mgr, self, rule));
    case AA_FIRST_AID:
        if (cid != WM_FIRST_AID) return false;
        return SubmitFirstAid_(mgr, self, rule); // 帐篷不降级
    case AA_CATAPULT:
        if (cid != WM_CATAPULT) return false;
        return SubmitCatapult_(mgr, self, rule); // 投石不降级
    default:
        return false;
    }
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
