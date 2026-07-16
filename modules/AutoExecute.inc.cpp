// AutoExecute.inc.cpp
// Automated battle execution module

static void WriteLog(const char* fmt, ...);

extern void CommitProfiles(int active_profile, AutoStackRule rules[5][21]);
extern AutoStackRule g_profiles[5][21];
extern AutoStackRule g_active_rules[21];
extern int  g_active_profile;
extern bool IsPanelActive();
extern void CloseSettingsPanel();

// 接管模型（收敛后）：
// 1) 只在“控制权交给玩家”时介入（HH_ShouldAutoExecute 返回 0 的路径）。
//    被蛊惑/敌方回合等本就不会把控制权交给玩家，无需单独状态机。
// 2) 普通部队：有非手动设置 → 代为提交动作；手动 → 原样留给玩家。
// 3) 仅战争机器有特殊分支（技能条件 / 交回 AI / 可选主动执行）。
// last_handled_stack 防止同一活动单位在一个回合内被重复下达命令。
// 循环施法两阶段：先投递快捷键 1-9/0，等英雄施法结束（或超时）再提交部队动作。
static struct {
    void* last_handled_stack;   // 上次已处理的活动单位指针
    void* spell_wait_stack;     // 等待快捷施法完成的活动单位
    void* spell_done_stack;     // 本单位本回合施法阶段已结束，避免重复投键
    int   spell_wait_slot;      // 对应 army_slot
    int   spell_wait_key;       // 已投递的快捷键 0..9
    int   spell_wait_frames;    // 已等待帧数
    int   spell_mana_before;    // 投递前法力
    int   spell_casted_before;  // 投递前 hero_casted[side]
    bool  spell_waiting;        // true=已投递快捷键，等待结果
} g_auto_state;

// 当前战斗的人类侧部队跟踪表（辅助正确套用设置，不是第二套控制权逻辑）。
// 设置提交时绑定“槽位 + 生物类型”；之后刷新存活/位置/数量。
// 执行前校验身份，避免仅凭 army_slot_ix 套错（死亡、召唤复用槽等）。
struct StackTrackEntry {
    bool  bound;         // 本场是否已绑定
    bool  alive;         // 当前是否存活
    int   side;          // 0/1
    int   slot;          // 0..20
    int   creature_id;   // 绑定身份
    int   hex;           // 当前位置
    int   count_alive;   // 当前数量
    int   count_start;   // 绑定时刻/开战数量
    // 本场运行游标：配置只保存序列，进度只存在跟踪表。
    int   move_cursor;
    int   melee_cursor;
    int   spell_cursor;
};
static StackTrackEntry g_stack_track[21] = {};
static int  g_track_side = -1;
static bool g_track_active = false;

static int ResolveHumanSide_(_BattleMgr_* mgr)
{
    if (!mgr) return -1;
    // 优先走 H3API 的 isHuman；Compat 结构体没有该字段。
    __try {
        if (H3CombatManager* cm = H3CombatManager::Get()) {
            if (cm->isHuman[0]) return 0;
            if (cm->isHuman[1]) return 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (mgr->current_active_side == 0 || mgr->current_active_side == 1)
        return mgr->current_active_side;
    return 0;
}

static void ClearStackTracking_()
{
    memset(g_stack_track, 0, sizeof(g_stack_track));
    g_track_side = -1;
    g_track_active = false;
}

// 从当前战场绑定人类侧所有“开战时存在”的部队身份。
static void BindStackTrackingFromBattle_()
{
    ClearStackTracking_();
    _BattleMgr_* mgr = o_BattleMgr;
    if (!mgr) {
        WriteLog("[Track] bind skipped: no battle manager");
        return;
    }

    const int side = ResolveHumanSide_(mgr);
    if (side < 0 || side > 1) {
        WriteLog("[Track] bind skipped: invalid human side");
        return;
    }

    int bound = 0;
    int configured = 0;
    for (int i = 0; i < 21; ++i) {
        _BattleStack_* s = &mgr->stack[side][i];
        // 本场从未上场的空槽跳过。
        if (s->count_at_start <= 0 && s->count_current <= 0) continue;
        if (s->creature_id < 0) continue;

        StackTrackEntry& t = g_stack_track[i];
        t.bound = true;
        t.side = side;
        t.slot = i;
        t.creature_id = s->creature_id;
        t.hex = s->hex_ix;
        t.count_alive = s->count_current;
        t.count_start = (s->count_at_start > 0) ? s->count_at_start : s->count_current;
        t.alive = s->count_current > 0;
        t.move_cursor = 0;
        t.melee_cursor = 0;
        t.spell_cursor = 0;
        ++bound;
        if (g_active_rules[i].action != AA_MANUAL)
            ++configured;

        WriteLog("[Track] bind slot=%d cid=0x%X hex=%d alive=%d count=%d/%d action=%d",
            i, t.creature_id, t.hex, t.alive ? 1 : 0,
            t.count_alive, t.count_start, (int)g_active_rules[i].action);
    }

    g_track_side = side;
    g_track_active = bound > 0;
    WriteLog("[Track] bound side=%d stacks=%d configured=%d",
        side, bound, configured);
}

// 刷新跟踪表：存活、位置、数量；身份变化则标记死亡并解除可用。
static void UpdateStackTracking_()
{
    if (!g_track_active || g_track_side < 0 || g_track_side > 1)
        return;
    _BattleMgr_* mgr = o_BattleMgr;
    if (!mgr) return;

    for (int i = 0; i < 21; ++i) {
        StackTrackEntry& t = g_stack_track[i];
        if (!t.bound) continue;

        _BattleStack_* s = &mgr->stack[t.side][i];
        if (s->creature_id != t.creature_id) {
            // 槽位被复用/清空：本绑定失效。
            if (t.alive) {
                WriteLog("[Track] identity lost slot=%d expect=0x%X got=0x%X",
                    i, t.creature_id, s->creature_id);
            }
            t.alive = false;
            t.count_alive = 0;
            t.hex = -1;
            continue;
        }

        const int prev_alive = t.count_alive;
        const int prev_hex = t.hex;
        t.hex = s->hex_ix;
        t.count_alive = s->count_current;
        t.alive = s->count_current > 0;

        if (prev_alive > 0 && !t.alive) {
            WriteLog("[Track] dead slot=%d cid=0x%X last_hex=%d",
                i, t.creature_id, prev_hex);
        } else if (t.alive && (prev_hex != t.hex || prev_alive != t.count_alive)) {
            // 位置/数量变化仅在调试时有用；降噪：只在数量变化时打日志。
            if (prev_alive != t.count_alive) {
                WriteLog("[Track] update slot=%d cid=0x%X hex=%d count=%d",
                    i, t.creature_id, t.hex, t.count_alive);
            }
        }
    }
}

// 当前活动单位是否仍对应跟踪表中的那支人类侧部队。
static bool ActiveStackMatchesTrack_(_BattleStack_* self)
{
    if (!self) return false;
    // 尚未绑定本场跟踪时：仅允许人类侧活动单位走旧逻辑。
    if (!g_track_active) {
        _BattleMgr_* mgr = o_BattleMgr;
        const int human = ResolveHumanSide_(mgr);
        return human >= 0 && self->def_group_ix == human && self->count_current > 0;
    }

    const int idx = self->army_slot_ix;
    if (idx < 0 || idx >= 21) return false;
    const StackTrackEntry& t = g_stack_track[idx];
    if (!t.bound || !t.alive) return false;
    if (self->def_group_ix != t.side) return false;
    if (self->creature_id != t.creature_id) return false;
    if (self->count_current <= 0) return false;
    return true;
}

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
    g_auto_state.spell_wait_stack = nullptr;
    g_auto_state.spell_done_stack = nullptr;
    g_auto_state.spell_wait_slot = -1;
    g_auto_state.spell_wait_key = -1;
    g_auto_state.spell_wait_frames = 0;
    g_auto_state.spell_mana_before = 0;
    g_auto_state.spell_casted_before = 0;
    g_auto_state.spell_waiting = false;
    // 策略是本进程内的已确认设置，战斗状态重置时保留；
    // 跟踪表是“当前战斗绑定”，进程重置时清空。
    ClearStackTracking_();
    WriteLog("Auto state reset; confirmed strategies preserved, tracking cleared.");
}

static void ClearSpellWait_()
{
    g_auto_state.spell_waiting = false;
    g_auto_state.spell_wait_stack = nullptr;
    g_auto_state.spell_wait_slot = -1;
    g_auto_state.spell_wait_key = -1;
    g_auto_state.spell_wait_frames = 0;
    g_auto_state.spell_mana_before = 0;
    g_auto_state.spell_casted_before = 0;
}

// 把 1-9/0 映射到 H3 战斗消息键码：H3VK_1=2 ... H3VK_9=10, H3VK_0=11。
static int DigitToH3Vk_(int digit)
{
    if (digit >= 1 && digit <= 9) return digit + 1; // 1->2 ... 9->10
    if (digit == 0) return 11; // H3VK_0
    return -1;
}

// 向游戏主窗口投递一次快捷施法数字键（KEY_DOWN/KEY_UP）。
// 不走系统 keybd_event，避免被设置面板钩子吞掉；直接注入战斗消息链。
static bool PostQuickSpellDigitKey_(int digit)
{
    const int h3vk = DigitToH3Vk_(digit);
    if (h3vk < 0) return false;
    HWND hwnd = *reinterpret_cast<HWND*>(0x699650);
    if (!hwnd) return false;

    // 战斗消息：command=KEY_DOWN(1)/KEY_UP(2)，subtype=H3VK。
    // 直接调用 0x4746B0 不可靠（依赖 this 与调用约定），改用 PostMessage 注入
    // 游戏自己的输入泵。原版键盘映射会把 WM_KEY* 翻译成 H3Msg。
    const WPARAM vk_win =
        (digit == 0) ? static_cast<WPARAM>('0') : static_cast<WPARAM>('0' + digit);
    // 扫描码可选，传 0 也可被多数路径接受。
    PostMessageA(hwnd, WM_KEYDOWN, vk_win, 1);
    PostMessageA(hwnd, WM_KEYUP, vk_win, 0xC0000001);
    WriteLog("[Spell] post quick key digit=%d win_vk=0x%X h3vk=%d",
        digit, (unsigned)vk_win, h3vk);
    return true;
}

static int GetHeroMana_(_BattleMgr_* mgr, int side)
{
    if (!mgr || side < 0 || side > 1) return -1;
    _Hero_* hero = mgr->hero[side];
    if (!hero) return -1;
    return hero->spell_points;
}

static int GetHeroCasted_(_BattleMgr_* mgr, int side)
{
    if (!mgr || side < 0 || side > 1) return 0;
    return mgr->hero_casted[side] ? 1 : 0;
}

// 从配置序列 + 运行游标取本回合快捷键；无序列返回 -1。
static int PeekSpellKey_(const AutoStackRule& rule, const StackTrackEntry& runtime)
{
    int n = rule.spellSlotCount;
    if (n <= 0) return -1;
    if (n > SPELL_SLOT_CAPACITY) n = SPELL_SLOT_CAPACITY;
    int cur = runtime.spell_cursor;
    if (cur < 0 || cur >= n) cur = 0;
    const int key = rule.spellSlots[cur];
    if (key == 0 || (key >= 1 && key <= 9)) return key;
    return -1;
}

static void AdvanceSpellCursor_(StackTrackEntry& runtime, int count)
{
    if (count <= 0) {
        runtime.spell_cursor = 0;
        return;
    }
    if (count > SPELL_SLOT_CAPACITY) count = SPELL_SLOT_CAPACITY;
    int cur = runtime.spell_cursor;
    if (cur < 0 || cur >= count) cur = 0;
    runtime.spell_cursor = (cur + 1) % count;
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
    // 提交后立即绑定本场部队身份；后续执行依赖跟踪校验。
    BindStackTrackingFromBattle_();
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
    case SEL_RANGED_SPEED: {
        // 远程优先 → 速度降序。射手（shots>0）排在近战前；同类按 speed 高者优先。
        _BattleStack_* best = candidates[0];
        int best_ranged = (best->creature.shots > 0) ? 1 : 0;
        int best_speed = best->creature.speed;
        for (int i = 1; i < count; ++i) {
            int r = (candidates[i]->creature.shots > 0) ? 1 : 0;
            int s = candidates[i]->creature.speed;
            if (r > best_ranged || (r == best_ranged && s > best_speed)) {
                best = candidates[i];
                best_ranged = r;
                best_speed = s;
            }
        }
        return best;
    }
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
// 循环移动：按 moveWaypoints 有序巡逻，逐点走向下一个路径点，到达后推进游标（末点回首点）。
// 无路径点时回退到旧的单目标移动（固定位置 / 靠近目标部队）。
// 注：可达性未做完整原版寻路校验。
static bool SubmitMove_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule, StackTrackEntry& runtime)
{
    if (!mgr || !self) return false;

    // 收集有效路径点（1..185）。配置只读，运行游标来自 StackTrackEntry。
    int wps[MOVE_WAYPOINT_CAPACITY];
    int n = 0;
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i) {
        int h = rule.target.moveWaypoints[i];
        if (h >= 1 && h <= 185) wps[n++] = h;
    }

    if (n > 0) {
        // 规范化运行时游标。
        int cur = runtime.move_cursor;
        if (cur < 0 || cur >= n) cur = 0;

        // 到点才推进（方案 A）：已站在当前点，则切到下一个点；
        // 若配置含重复点，跳过连续的脚下点。
        if (self->hex_ix == wps[cur])
            cur = (cur + 1) % n;
        int guard = 0;
        while (wps[cur] == self->hex_ix && guard < n) {
            cur = (cur + 1) % n;
            ++guard;
        }
        runtime.move_cursor = cur;

        int hex = wps[cur];
        if (hex == self->hex_ix) return false; // 所有点都在脚下
        if (!WriteAction_(mgr, self, BA_WALK, -1, hex))
            return false;
        WriteLog("[Auto] submit WALK(patrol) slot=%d -> hex=%d cursor=%d/%d",
            self->army_slot_ix, hex, cur, n);
        return true;
    }

    // 回退：旧单目标移动。
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
static bool SubmitMelee_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule, StackTrackEntry& runtime)
{
    if (!mgr || !self) return false;
    const AutoTargetRule& target = rule.target;
    int count = target.meleePairCount;
    if (count < 0) count = 0;
    if (count > MELEE_PAIR_CAPACITY) count = MELEE_PAIR_CAPACITY;

    // 兼容旧版单组规则：序列为空时仍执行旧字段，但不虚构新记录。
    const bool legacy = count == 0;
    int cursor = legacy ? 0 : runtime.melee_cursor;
    if (!legacy && (cursor < 0 || cursor >= count)) cursor = 0;

    const int attack_hex = legacy
        ? target.meleeAttackHex : target.meleeAttackHexes[cursor];
    const int stand_hex = legacy
        ? target.meleeStandHex : target.meleeStandHexes[cursor];
    if (attack_hex < 1 || attack_hex > 185
        || stand_hex < 1 || stand_hex > 185) {
        WriteLog("[Auto] melee pair invalid slot=%d cursor=%d/%d stand=%d attack=%d",
            self->army_slot_ix, cursor, count, stand_hex, attack_hex);
        return false;
    }

    _BattleStack_* enemy = FindEnemyOccupyingHex_(mgr, self, attack_hex);
    if (!enemy) {
        WriteLog("[Auto] melee attack hex=%d empty (no enemy head/tail) slot=%d",
            attack_hex, self->army_slot_ix);
        return false;
    }

    // 写悬停相关字段，模拟玩家点过该攻击格
    mgr->mouse_coord = attack_hex;
    mgr->attacker_coord = stand_hex;
    mgr->move_type = 7; // 近战悬停类型（FUN_00475dc0 返回 7）

    if (!WriteAction_(mgr, self, BA_WALK_ATTACK, stand_hex, attack_hex))
        return false;
    // 只在成功提交后推进；失败时保持当前组合不变。
    if (!legacy && count > 0)
        runtime.melee_cursor = (cursor + 1) % count;
    WriteLog("[Auto] submit MELEE(loop) pair=%d/%d stand=%d attack=%d enemy_slot=%d enemy_hex=%d next=%d",
        cursor, count, stand_hex, attack_hex, enemy->army_slot_ix, enemy->hex_ix,
        legacy ? 0 : runtime.melee_cursor);
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

// 提交部队主动作（不含循环施法阶段）。
static bool SubmitConfiguredUnitAction_(_BattleMgr_* mgr, _BattleStack_* self,
    const AutoStackRule& rule, StackTrackEntry& runtime)
{
    if (!mgr || !self) return false;
    const int cid = self->creature_id;
    if (cid == WM_AMMO_CART) return false;

    auto fallback_or_fail = [&](bool ok) -> bool {
        if (ok) return true;
        // 战争机器不降级；普通部队可降级防御。
        if (!IsWarMachineCid_(cid) && rule.allowDefendFallback)
            return SubmitDefend_(mgr, self);
        return false;
    };

    switch (rule.action) {
    case AA_MANUAL:
        // 仅配置了循环施法时：施法阶段结束后把主动作交回玩家。
        return false;
    case AA_DEFEND:
        return SubmitDefend_(mgr, self);
    case AA_WAIT:
        return fallback_or_fail(SubmitWait_(mgr, self));
    case AA_MOVE:
        return fallback_or_fail(SubmitMove_(mgr, self, rule, runtime));
    case AA_MELEE_ATTACK:
        return fallback_or_fail(SubmitMelee_(mgr, self, rule, runtime));
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

// 尝试按规则提交主动动作；成功返回 true。
// 含循环施法两阶段：先投递快捷键，等待施法结束/超时后再提交部队动作。
static bool TrySubmitConfiguredAction_(_BattleMgr_* mgr)
{
    if (!mgr) return false;
    if (mgr->auto_combat) return false;
    if (IsHiddenBattle(mgr)) return false;
    if (mgr->action != 0) return false;
    UpdateStackTracking_();
    _BattleStack_* self = mgr->active_stack;
    if (!self || self->count_current <= 0) return false;

    // 活动单位变化：清空施法等待/完成标记与 last_handled。
    if (g_auto_state.spell_waiting
        && g_auto_state.spell_wait_stack
        && g_auto_state.spell_wait_stack != self)
        ClearSpellWait_();
    if (g_auto_state.spell_done_stack
        && g_auto_state.spell_done_stack != self)
        g_auto_state.spell_done_stack = nullptr;
    if (g_auto_state.last_handled_stack == self
        && !g_auto_state.spell_waiting)
        return false; // 本单位已处理完

    // 必须是跟踪表中仍存活、身份匹配的人类侧部队。
    if (!ActiveStackMatchesTrack_(self)) {
        if (g_auto_state.spell_waiting)
            ClearSpellWait_();
        static void* s_last_mismatch = nullptr;
        if (s_last_mismatch != self) {
            s_last_mismatch = self;
            WriteLog("[Track] skip action: mismatch side=%d slot=%d cid=0x%X count=%d",
                self->def_group_ix, self->army_slot_ix,
                self->creature_id, self->count_current);
        }
        return false;
    }

    const int idx = self->army_slot_ix;
    if (idx < 0 || idx >= 21) return false;
    const AutoStackRule& rule = g_active_rules[idx];
    StackTrackEntry& runtime = g_stack_track[idx];
    const int side = self->def_group_ix;
    const int spell_key = PeekSpellKey_(rule, runtime);
    const bool want_spell = spell_key >= 0;
    const bool want_action = rule.action != AA_MANUAL;

    // 既无施法序列、又无主动作：不介入。
    if (!want_spell && !want_action)
        return false;

    // —— 阶段 1：投递快捷施法键 ——
    if (want_spell && !g_auto_state.spell_waiting
        && g_auto_state.spell_done_stack != self) {
        // 本英雄本回合已施过法：跳过施法，直接进入部队动作。
        if (GetHeroCasted_(mgr, side) != 0) {
            WriteLog("[Spell] already cast this turn side=%d; skip quick key=%d",
                side, spell_key);
            AdvanceSpellCursor_(runtime, rule.spellSlotCount);
            g_auto_state.spell_done_stack = self;
        } else {
            g_auto_state.spell_mana_before = GetHeroMana_(mgr, side);
            g_auto_state.spell_casted_before = GetHeroCasted_(mgr, side);
            if (!PostQuickSpellDigitKey_(spell_key)) {
                WriteLog("[Spell] post key failed digit=%d; fallthrough to unit action",
                    spell_key);
                AdvanceSpellCursor_(runtime, rule.spellSlotCount);
                g_auto_state.spell_done_stack = self;
            } else {
                g_auto_state.spell_waiting = true;
                g_auto_state.spell_wait_stack = self;
                g_auto_state.spell_wait_slot = idx;
                g_auto_state.spell_wait_key = spell_key;
                g_auto_state.spell_wait_frames = 0;
                WriteLog("[Spell] wait start slot=%d key=%d mana=%d casted=%d",
                    idx, spell_key, g_auto_state.spell_mana_before,
                    g_auto_state.spell_casted_before);
                return false; // 本帧只投键，不提交部队动作
            }
        }
    }

    // —— 阶段 2：等待施法结果 ——
    if (g_auto_state.spell_waiting && g_auto_state.spell_wait_stack == self) {
        ++g_auto_state.spell_wait_frames;
        const int mana_now = GetHeroMana_(mgr, side);
        const int casted_now = GetHeroCasted_(mgr, side);
        const bool cast_done =
            (casted_now != 0 && g_auto_state.spell_casted_before == 0)
            || (mana_now >= 0 && g_auto_state.spell_mana_before >= 0
                && mana_now < g_auto_state.spell_mana_before);
        // 超时保护：约 90 帧后不再等，避免卡死。
        const bool timed_out = g_auto_state.spell_wait_frames >= 90;
        if (!cast_done && !timed_out)
            return false; // 继续等

        WriteLog("[Spell] wait end slot=%d key=%d frames=%d cast_done=%d timed_out=%d mana %d->%d casted %d->%d",
            idx, g_auto_state.spell_wait_key, g_auto_state.spell_wait_frames,
            cast_done ? 1 : 0, timed_out ? 1 : 0,
            g_auto_state.spell_mana_before, mana_now,
            g_auto_state.spell_casted_before, casted_now);

        // 无论成功/失败/超时都推进游标，避免同一键卡死整场。
        AdvanceSpellCursor_(runtime, rule.spellSlotCount);
        ClearSpellWait_();
        g_auto_state.spell_done_stack = self;
        // 施法动画/选择目标期间 action 可能被占用；若仍非 0 则下帧再提交主动作。
        if (mgr->action != 0)
            return false;
    }

    // —— 阶段 3：提交部队主动作 ——
    if (!want_action) {
        // 仅循环施法、主动作为手动：施法流程结束后交回玩家。
        g_auto_state.last_handled_stack = self;
        return false;
    }

    const bool ok = SubmitConfiguredUnitAction_(mgr, self, rule, runtime);
    return ok;
}

// DecideTakeover：仅在“原版本会把控制权交给玩家”时被询问（HH 里 orig==0）。
// 普通部队：有配置则本插件提交动作；手动则留给玩家。
// 战争机器：才有“交回 AI / 保持原版 / 按配置执行”的特殊分支。
int DecideTakeover(_BattleMgr_* mgr)
{
    if (!mgr) return CD_KEEP_ORIGINAL;
    if (mgr->auto_combat) return CD_KEEP_ORIGINAL;
    if (IsHiddenBattle(mgr)) return CD_KEEP_ORIGINAL;
    UpdateStackTracking_();
    _BattleStack_* stack = mgr->active_stack;
    if (!stack || stack->count_current <= 0) return CD_KEEP_ORIGINAL;

    // 非本场已绑定的人类侧存活单位：不介入（控制权本就不该由我们改写）。
    if (!ActiveStackMatchesTrack_(stack))
        return CD_KEEP_ORIGINAL;

    int idx = stack->army_slot_ix;
    if (idx < 0 || idx >= 21) return CD_KEEP_ORIGINAL;
    const AutoStackRule& rule = g_active_rules[idx];
    const int cid = stack->creature_id;

    // —— 仅战争机器特殊处理 ——
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
        // 无弹道术时原版很笨：未配置则交回 AI；有弹道术则保持原版。
        if (HeroHasSkill(mgr, SK_BALLISTICS))
            return CD_KEEP_ORIGINAL;
        return CD_HAND_TO_AI;

    case WM_BALLISTA:
    case WM_ARROW_TOWER:
        if (rule.action == AA_RANGED_ATTACK)
            return CD_EXECUTE_H3AUTO;
        // 无炮术时原版很笨：未配置则交回 AI；有炮术则保持原版。
        if (HeroHasSkill(mgr, SK_ARTILLERY))
            return CD_KEEP_ORIGINAL;
        return CD_HAND_TO_AI;

    default:
        // 普通部队：无特殊 AI 分支。
        // - 有非手动主动作 → 代发动作
        // - 主动作手动但配置了循环施法 → 仍进入执行路径（仅做施法阶段）
        // - 都没有 → 留给玩家
        if (rule.action != AA_MANUAL)
            return CD_EXECUTE_H3AUTO;
        if (rule.spellSlotCount > 0)
            return CD_EXECUTE_H3AUTO;
        return CD_KEEP_ORIGINAL;
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

// ==== 接管钩子：控制权交给玩家的时机 ====
// FUN_004744d0 @ 0x4744D0：
//   返回非 0 = 自动/AI；返回 0 = 等待本地人类输入（控制权交给玩家）。
// 只在 orig==0（本会交给玩家）时介入：
//   - 战争机器未配置且应走 AI → 改返回 1 交回 AI
//   - 有配置要代发动作 → 仍返回 0，由消息入口提交 action
//   - 手动 / 其它 → 返回 0，真正留给玩家
// 蛊惑等“本就不会交给玩家”的情况：orig 已非 0，我们直接放行。
int __stdcall HH_ShouldAutoExecute(HiHook* h, _BattleMgr_* This)
{
    int orig = THISCALL_1(int, h->GetDefaultFunc(), This);
    if (orig != 0)
        return orig;            // 非“交给玩家”路径：不介入
    __try {
        if (This && This->active_stack) {
            if (g_auto_state.last_handled_stack
                && g_auto_state.last_handled_stack != This->active_stack)
                g_auto_state.last_handled_stack = nullptr;
            if (g_auto_state.spell_done_stack
                && g_auto_state.spell_done_stack != This->active_stack)
                g_auto_state.spell_done_stack = nullptr;
            if (g_auto_state.spell_waiting
                && g_auto_state.spell_wait_stack
                && g_auto_state.spell_wait_stack != This->active_stack)
                ClearSpellWait_();
        }

        const int decision = DecideTakeover(This);
        if (decision == CD_HAND_TO_AI)
            return 1;           // 仅战争机器特殊：交回 AI
        // CD_EXECUTE_H3AUTO / CD_KEEP_ORIGINAL：返回 0（控制权在玩家路径）。
        // 若需代发动作，在 Hook_BattleMsgProc 入口提交。
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
