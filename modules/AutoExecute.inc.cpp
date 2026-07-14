// AutoExecute.inc.cpp
// Automated battle execution module

static void WriteLog(const char* fmt, ...);

extern void CommitProfiles(int active_profile,
    int actions[5][21], int targets[5][21]);
extern int  g_action_profiles[5][21];
extern int  g_target_profiles[5][21];
extern int  g_active_profile;
extern int  g_action_strategies[21];
extern int  g_target_strategies[21];
extern bool IsPanelActive();
extern void CloseSettingsPanel();

static struct {
    bool enabled;
} g_auto_state;

void ResetAutoState()
{
    if (IsPanelActive())
        CloseSettingsPanel();
    g_auto_state.enabled = false;
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

void DoRandomShot(_BattleStack_* self)
{
    _BattleStack_* targets[21];
    int enemy_side = 1 - self->def_group_ix;
    int count = GetEnemyStacks(targets, 21, enemy_side);
    if (count == 0) return;
    int idx = rand() % count;
    WriteLog("[Auto] Stack #%d random shot -> target #%d (TODO: commit)", self->army_slot_ix, idx);
    (void)idx;
}

void DoSequentialShot(_BattleStack_* self)
{
    // 顺序射击：按敌方站位顺序依次射击
    _BattleStack_* targets[21];
    int enemy_side = 1 - self->def_group_ix;
    int count = GetEnemyStacks(targets, 21, enemy_side);
    if (count == 0) return;
    WriteLog("[Auto] Stack #%d sequential shot (TODO: commit)", self->army_slot_ix);
}

// CommitProfiles：勾号/Enter一次性提交全部5套内存方案，当前选中方案立即生效
void CommitProfiles(int active_profile, int actions[5][21], int targets[5][21])
{
    if (active_profile < 0 || active_profile >= 5)
        active_profile = 0;
    memcpy(g_action_profiles, actions, sizeof(g_action_profiles));
    memcpy(g_target_profiles, targets, sizeof(g_target_profiles));
    g_active_profile = active_profile;
    memcpy(g_action_strategies, g_action_profiles[g_active_profile],
        sizeof(g_action_strategies));
    memcpy(g_target_strategies, g_target_profiles[g_active_profile],
        sizeof(g_target_strategies));
    WriteLog("[Auto] 5 profiles committed; active profile=%d", g_active_profile + 1);
}

void ExecuteForStack(_BattleStack_* self, int action, int target)
{
    if (!self || self->count_current <= 0) return;
    switch (action) {
        case AS_MANUAL: return;
        case AS_DEFEND:
            WriteLog("[Auto] Stack #%d defend (TODO: implement)", self->army_slot_ix);
            break;
        case AS_MELEE:
            WriteLog("[Auto] Stack #%d melee attack (TODO: implement)", self->army_slot_ix);
            break;
        case AS_RANDOM:   DoRandomShot(self);   break;
        case AS_SEQUENTIAL: DoSequentialShot(self); break;
        case AS_CYCLE_MOVE:
            WriteLog("[Auto] Stack #%d cycle move (TODO: implement)", self->army_slot_ix);
            break;
        default: break;
    }
}

void __stdcall Hook_CycleCombatScreen(_BattleMgr_* mgr)
{
    if (!mgr) return;
    if (mgr->auto_combat) return;
    if (!mgr->hero[mgr->current_active_side]) return;
    _BattleStack_* stack = mgr->active_stack;
    if (!stack) return;
    if (!g_auto_state.enabled) return;
    int idx = stack->army_slot_ix;
    if (idx < 0 || idx >= 21) return;
    int action = g_action_strategies[idx];
    int target = g_target_strategies[idx];
    if (action == AS_MANUAL) return;
    ExecuteForStack(stack, action, target);
}
