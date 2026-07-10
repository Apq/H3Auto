// AutoExecute.inc.cpp
// Automated battle execution module

static void WriteLog(const char* fmt, ...);

enum StackStrategy {
    STRAT_MANUAL   = 0,
    STRAT_DEFEND   = 1,
    STRAT_RANDOM   = 2,
    STRAT_SMART    = 3,
    STRAT_BALANCED = 4
};

static struct {
    bool enabled;
    int  strategies[21];
} g_auto_state;

void ResetAutoState()
{
    g_auto_state.enabled = false;
    memset(g_auto_state.strategies, 0, sizeof(g_auto_state.strategies));
    WriteLog("Auto state reset.");
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

void DoSmartShot(_BattleStack_* self)
{
    _BattleStack_* targets[21];
    int enemy_side = 1 - self->def_group_ix;
    int count = GetEnemyStacks(targets, 21, enemy_side);
    if (count == 0) return;
    WriteLog("[Auto] Stack #%d smart shot (TODO: implement)", self->army_slot_ix);
}

void DoBalancedShot(_BattleStack_* self)
{
    _BattleStack_* targets[21];
    int enemy_side = 1 - self->def_group_ix;
    int count = GetEnemyStacks(targets, 21, enemy_side);
    if (count == 0) return;
    WriteLog("[Auto] Stack #%d balanced shot (TODO: implement)", self->army_slot_ix);
}

void ExecuteForStack(_BattleStack_* self, int strategy)
{
    if (!self || self->count_current <= 0) return;
    switch (strategy) {
        case STRAT_MANUAL: return;
        case STRAT_DEFEND:
            WriteLog("[Auto] Stack #%d defend (TODO: implement)", self->army_slot_ix);
            break;
        case STRAT_RANDOM:   DoRandomShot(self);   break;
        case STRAT_SMART:    DoSmartShot(self);    break;
        case STRAT_BALANCED: DoBalancedShot(self); break;
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
    int strategy = g_auto_state.strategies[idx];
    if (strategy == STRAT_MANUAL) return;
    ExecuteForStack(stack, strategy);
}
