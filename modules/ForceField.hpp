#pragma once

#include "PolicyCore.hpp"

extern H3AutoPolicy::ForceFieldProfileFields g_forcefield[5];

// Presence matches a live obstacle's template, owner and configured anchor only.
H3AutoPolicy::ForceFieldPresence ReadForceField_(H3CombatManager* cm,
    int side, int anchor);

// False blocks automatic actions during casts or an invalidated action window.
bool EnsureForceFieldBeforeAction_(_BattleMgr_* mgr);
void ResetForceFieldRuntime_();
