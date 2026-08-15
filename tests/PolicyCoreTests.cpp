#include "../modules/PolicyCore.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

using namespace H3AutoPolicy;

namespace {

int g_checks = 0;

void Check(bool condition, const char* name)
{
    ++g_checks;
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        std::exit(1);
    }
}

void CheckActions(int creature, bool ranged, bool artillery, bool firstAid,
    std::initializer_list<AutoActionKind> expected, const char* name)
{
    AutoActionKind actual[AA_COUNT] = {};
    const int count = GetAllowedActions(creature, ranged, artillery, firstAid, actual);
    Check(count == static_cast<int>(expected.size()), name);
    int i = 0;
    for (AutoActionKind action : expected)
        Check(actual[i++] == action, name);
}

void TestPanelAdmission()
{
    Check(!IsConfigurablePanelStack(CREATURE_AMMO_CART, 10, true),
        "ammo cart is never configurable");
    Check(!IsConfigurablePanelStack(CREATURE_CATAPULT, 10, false),
        "catapult requires ballistics");
    Check(IsConfigurablePanelStack(CREATURE_CATAPULT, 10, true),
        "catapult with ballistics is configurable");
    Check(IsConfigurablePanelStack(CREATURE_FIRST_AID_TENT, 10, false),
        "first aid tent is always configurable");
    Check(!IsConfigurablePanelStack(CREATURE_BALLISTA, 0, true),
        "dead stack is not configurable");
}

void TestWarMachineActions()
{
    CheckActions(CREATURE_BALLISTA, true, true, false,
        {AA_MANUAL, AA_RANGED_ATTACK}, "ballista with artillery");
    CheckActions(CREATURE_BALLISTA, true, false, false,
        {AA_RANGED_ATTACK}, "ballista without artillery");
    CheckActions(CREATURE_ARROW_TOWER, true, true, false,
        {AA_MANUAL, AA_RANGED_ATTACK}, "arrow tower with artillery");
    CheckActions(CREATURE_ARROW_TOWER, true, false, false,
        {AA_RANGED_ATTACK}, "arrow tower without artillery");
    CheckActions(CREATURE_FIRST_AID_TENT, false, false, true,
        {AA_MANUAL, AA_FIRST_AID}, "first aid tent with first aid skill");
    CheckActions(CREATURE_FIRST_AID_TENT, false, false, false,
        {AA_FIRST_AID}, "first aid tent without first aid skill");
    CheckActions(CREATURE_CATAPULT, false, false, false,
        {AA_MANUAL, AA_DEFEND}, "catapult actions");
    CheckActions(CREATURE_AMMO_CART, false, false, false,
        {AA_MANUAL}, "ammo cart stays original");
}

void TestSelectorsAreIndependent()
{
    AutoTargetSelector ranged[SEL_COUNT] = {};
    const int rangedCount = GetAllowedSelectors(AA_RANGED_ATTACK, AT_STACK, ranged);
    Check(rangedCount == 3, "ranged selector count");
    Check(ranged[0] == SEL_RANDOM && ranged[1] == SEL_RANGED_SPEED
        && ranged[2] == SEL_COUNT_HIGH, "ranged selector order");

    AutoTargetSelector aid[SEL_COUNT] = {};
    const int aidCount = GetAllowedSelectors(AA_FIRST_AID, AT_STACK, aid);
    Check(aidCount == 3, "first aid selector count");
    Check(aid[0] == SEL_WOUND_VALUE && aid[1] == SEL_WOUND_RATIO
        && aid[2] == SEL_RANDOM, "first aid selector order");

    AutoTargetRule aidDefault = DefaultTargetForAction(AA_FIRST_AID);
    Check(aidDefault.kind == AT_STACK && aidDefault.side == ATS_OWN,
        "first aid candidates are own stacks");
    Check(aidDefault.selector == SEL_WOUND_VALUE,
        "first aid default is wound value");
}

void TestInvalidActionNormalization()
{
    AutoStackRule ballista = MakeDefaultRule();
    ballista.action = AA_FIRST_AID;
    NormalizeRule(&ballista, CREATURE_BALLISTA, true, true, false);
    Check(ballista.action == AA_MANUAL, "invalid ballista action snaps to manual with skill");

    ballista = MakeDefaultRule();
    ballista.action = AA_MANUAL;
    NormalizeRule(&ballista, CREATURE_BALLISTA, true, false, false);
    Check(ballista.action == AA_RANGED_ATTACK,
        "manual ballista snaps to ranged without artillery");

    AutoStackRule tent = MakeDefaultRule();
    tent.action = AA_MANUAL;
    NormalizeRule(&tent, CREATURE_FIRST_AID_TENT, false, false, false);
    Check(tent.action == AA_FIRST_AID,
        "manual tent snaps to first aid without first aid skill");

    Check(!ActionShowsFallback(CREATURE_BALLISTA, AA_RANGED_ATTACK),
        "war machine hides defend fallback");
    Check(ActionShowsFallback(1, AA_RANGED_ATTACK),
        "ordinary ranged stack keeps defend fallback");
}

void TestTargetScoring()
{
    const TargetCandidate candidates[] = {
        {10, 10, 100, 0, 0, 20},
        {5, 10, 100, 0, 1, 10},
        {8, 10, 100, 0, 0, 30},
    };
    Check(SelectTargetIndex(candidates, 3, SEL_COUNT_HIGH, 0) == 0,
        "count high keeps first tie");
    Check(SelectTargetIndex(candidates, 3, SEL_RANGED_SPEED, 0) == 1,
        "ranged speed prefers shooter before speed");
    Check(SelectTargetIndex(candidates, 3, SEL_RANDOM, 4) == 1,
        "random selector uses injected random value");

    const TargetCandidate wounded[] = {
        {8, 10, 100, 0, 0, 10}, // 200 lost HP from deaths
        {9, 10, 100, 50, 0, 10}, // 150 lost HP
        {5, 10, 100, 0, 0, 10}, // 500 lost HP
    };
    Check(WoundValue(wounded[2]) == 500, "wound value includes dead units");
    Check(SelectTargetIndex(wounded, 3, SEL_WOUND_VALUE, 0) == 2,
        "wound value selects largest absolute loss");
    Check(SelectTargetIndex(wounded, 3, SEL_WOUND_RATIO, 0) == 2,
        "wound ratio selects largest relative loss");
}

void TestTargetNormalization()
{
    AutoStackRule ranged = MakeDefaultRule();
    ranged.action = AA_RANGED_ATTACK;
    ranged.target.kind = AT_STACK;
    ranged.target.side = ATS_OWN;
    ranged.target.selector = SEL_WOUND_VALUE;
    NormalizeRule(&ranged, 1, true, false, false);
    Check(ranged.target.side == ATS_ENEMY, "ranged target side is enemy");
    Check(ranged.target.selector == SEL_RANDOM,
        "invalid ranged selector snaps to random");

    AutoStackRule aid = MakeDefaultRule();
    aid.action = AA_FIRST_AID;
    aid.target.kind = AT_STACK;
    aid.target.side = ATS_ENEMY;
    aid.target.selector = SEL_COUNT_HIGH;
    NormalizeRule(&aid, CREATURE_FIRST_AID_TENT, false, false, false);
    Check(aid.target.side == ATS_OWN, "first aid target side is own");
    Check(aid.target.selector == SEL_WOUND_VALUE,
        "invalid first aid selector snaps to wound value");
}

void TestResultLifecycle()
{
    ResultLifecycleState state = {};
    Check(ApplyResultLifecycle(&state, RESULT_SHOWN) == RESULT_WAIT,
        "result shown waits");
    Check(ApplyResultLifecycle(&state, RESULT_CANCEL_CLICKED) == RESULT_WAIT,
        "cancel click waits for result close");
    Check(ApplyResultLifecycle(&state, RESULT_CLOSED_WITH_BATTLE_UI)
        == RESULT_KEEP_AND_REBIND, "cancel keeps settings and rebinds");

    Check(ApplyResultLifecycle(&state, RESULT_SHOWN) == RESULT_WAIT,
        "second result shown waits");
    Check(ApplyResultLifecycle(&state, RESULT_ACCEPT_CLICKED) == RESULT_WAIT,
        "accept click waits for result close");
    Check(ApplyResultLifecycle(&state, RESULT_CLOSED_WITHOUT_BATTLE_UI)
        == RESULT_CLEAR_SETTINGS, "accept clears settings");

    Check(ApplyResultLifecycle(&state, RESULT_SHOWN) == RESULT_WAIT,
        "plain result shown waits");
    Check(ApplyResultLifecycle(&state, RESULT_CLOSED_WITHOUT_BATTLE_UI)
        == RESULT_CLEAR_SETTINGS, "plain result close clears settings");
}

void TestLegacySpellSlotCompatibility()
{
    AutoStackRule oldRule = MakeDefaultRule();
    oldRule.quickCastFirst = true;
    oldRule.spellSlot = 7;
    oldRule.spellSlotCount = 0;
    NormalizeRule(&oldRule, 1, false, false, false);
    Check(oldRule.spellSlotCount == 1 && oldRule.spellSlots[0] == 7,
        "legacy spell slot migrates");
    Check(oldRule.quickCastFirst && oldRule.spellSlot == 7,
        "legacy spell mirrors synchronize");

    AutoStackRule slots = MakeDefaultRule();
    slots.spellSlotCount = 5;
    slots.spellSlots[0] = 1;
    slots.spellSlots[1] = 99;
    slots.spellSlots[2] = 0;
    slots.spellSlots[3] = -1;
    slots.spellSlots[4] = 4;
    NormalizeRule(&slots, 1, false, false, false);
    Check(slots.spellSlotCount == 3, "invalid spell slots are removed");
    Check(slots.spellSlots[0] == 1 && slots.spellSlots[1] == 0
        && slots.spellSlots[2] == 4, "spell slots compact in order");
}

} // namespace

int main()
{
    TestPanelAdmission();
    TestWarMachineActions();
    TestSelectorsAreIndependent();
    TestInvalidActionNormalization();
    TestTargetScoring();
    TestTargetNormalization();
    TestResultLifecycle();
    TestLegacySpellSlotCompatibility();
    std::cout << "PolicyCoreTests: " << g_checks << " checks passed\n";
    return 0;
}
