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
        "ranged flyer speed prefers shooter before flyer");

    const TargetCandidate flight[] = {
        {5, 10, 100, 0, 1, 30, 0},
        {5, 10, 100, 0, 1, 10, 1},
        {5, 10, 100, 0, 0, 40, 1},
    };
    Check(SelectTargetIndex(flight, 3, SEL_RANGED_SPEED, 0) == 1,
        "ranged flyer speed prefers flyer before speed");
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
    Check(ApplyResultLifecycle(&state, RESULT_CLOSED_WITHOUT_BATTLE_UI)
        == RESULT_WAIT, "cancel transition does not clear settings");
    Check(ApplyResultLifecycle(&state, RESULT_CLOSED_WITH_BATTLE_UI)
        == RESULT_KEEP_AND_REBIND, "cancel keeps settings and rebinds");

    Check(ApplyResultLifecycle(&state, RESULT_SHOWN) == RESULT_WAIT,
        "second result shown waits");
    Check(ApplyResultLifecycle(&state, RESULT_ACCEPT_CLICKED) == RESULT_WAIT,
        "accept click waits for result close");
    Check(ApplyResultLifecycle(&state, RESULT_CLOSED_WITHOUT_BATTLE_UI)
        == RESULT_CLEAR_SETTINGS, "accept clears settings");

    Check(ApplyResultLifecycle(&state, RESULT_SHOWN) == RESULT_WAIT,
        "accept with transient battle UI starts");
    Check(ApplyResultLifecycle(&state, RESULT_ACCEPT_CLICKED) == RESULT_WAIT,
        "accept with transient battle UI is armed");
    Check(ApplyResultLifecycle(&state, RESULT_CLOSED_WITH_BATTLE_UI)
        == RESULT_CLEAR_SETTINGS, "explicit accept overrides transient battle UI");

    Check(ApplyResultLifecycle(&state, RESULT_SHOWN) == RESULT_WAIT,
        "plain result shown waits");
    Check(ApplyResultLifecycle(&state, RESULT_CLOSED_WITHOUT_BATTLE_UI)
        == RESULT_CLEAR_SETTINGS, "plain result close clears settings");

    Check(ApplyResultLifecycle(&state, RESULT_SHOWN) == RESULT_WAIT,
        "retry without captured click starts");
    Check(ApplyResultLifecycle(&state, RESULT_CLOSED_WITH_BATTLE_UI)
        == RESULT_KEEP_AND_REBIND, "battle UI return identifies retry");
}

void TestStableStackIdentity()
{
    const StableStackIdentity army0 = MakeStableStackIdentity(0, 0, 10, 0);
    const StableStackIdentity army6 = MakeStableStackIdentity(0, 6, 10, 0);
    const StableStackIdentity ballista = MakeStableStackIdentity(
        0, -1, CREATURE_BALLISTA, 0);
    const StableStackIdentity tower1 = MakeStableStackIdentity(
        0, -1, CREATURE_ARROW_TOWER, 1);
    const StableStackIdentity summon = MakeStableStackIdentity(0, -1, 10, 0);

    Check(army0.kind == STACK_ID_ARMY_SLOT && army0.value == 0,
        "ordinary stack identity uses source army slot");
    Check(!StableStackIdentityEquals(army0, army6),
        "same creature in different army slots stays distinct");
    Check(ballista.kind == STACK_ID_WAR_MACHINE,
        "war machine identity uses machine kind");
    Check(tower1.occurrence == 1,
        "duplicate war machines use occurrence");
    Check(summon.kind == STACK_ID_NONE,
        "summon has no cross-attempt identity");

    const StableStackIdentity previous[5] = {
        army0, ballista, army6, tower1, summon
    };
    const StableStackIdentity current[5] = {
        tower1, army6, summon, army0, ballista
    };
    int remap[5] = {};
    BuildStableStackSlotRemap(previous, 5, current, 5, remap);
    Check(remap[0] == 3 && remap[1] == 2 && remap[3] == 0
        && remap[4] == 1, "stable identities remap reordered battle slots");
    Check(remap[2] == -1, "dynamic stack does not inherit a rule");
}

void TestFailedActionPlayerHandoffEligibility()
{
    Check(CanYieldFailedActionToPlayer(10, false, false, false),
        "ordinary stack can receive failed configured action");
    Check(CanYieldFailedActionToPlayer(CREATURE_CATAPULT, true, false, false),
        "ballistics allows catapult player handoff");
    Check(!CanYieldFailedActionToPlayer(CREATURE_CATAPULT, false, false, false),
        "catapult handoff requires ballistics");
    Check(CanYieldFailedActionToPlayer(CREATURE_BALLISTA, false, true, false),
        "artillery allows ballista player handoff");
    Check(CanYieldFailedActionToPlayer(CREATURE_ARROW_TOWER, false, true, false),
        "artillery allows arrow tower player handoff");
    Check(!CanYieldFailedActionToPlayer(CREATURE_BALLISTA, false, false, false),
        "ballista handoff requires artillery");
    Check(CanYieldFailedActionToPlayer(CREATURE_FIRST_AID_TENT, false, false, true),
        "first aid allows tent player handoff");
    Check(!CanYieldFailedActionToPlayer(CREATURE_FIRST_AID_TENT, false, false, false),
        "tent handoff requires first aid");
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

    // 回归：右键删除最后一个槽后不能被旧版兼容镜像复活。
    AutoStackRule deleted = MakeDefaultRule();
    deleted.spellSlotCount = 1;
    deleted.spellSlots[0] = 7;
    deleted.quickCastFirst = true;
    deleted.spellSlot = 7;
    Check(RemoveSpellSlot(&deleted, 0), "remove last spell slot succeeds");
    Check(deleted.spellSlotCount == 0 && deleted.spellSlots[0] == -1,
        "deleting last spell slot stays empty");
    Check(!deleted.quickCastFirst && deleted.spellSlot == 1,
        "deleting last spell slot clears legacy mirror");
}

void TestProtect()
{
    TargetCandidate angel = {};
    angel.count_current = 2;
    angel.hit_points = 200;
    angel.lost_hp = 30;
    Check(StackRemainingHp(angel) == 370, "remaining hp subtracts only the damaged top creature");

    TargetCandidate dead = {};
    dead.count_current = 0;
    dead.hit_points = 200;
    dead.lost_hp = 50;
    Check(StackRemainingHp(dead) == 0, "dead stack has no remaining hp");

    // 可恢复量：基础 50×力量 / 高级 75×力量 / 专家 100×力量。
    Check(ResurrectionRestoreHp(1, 10) == 500, "basic resurrection restores 50*power");
    Check(ResurrectionRestoreHp(2, 10) == 750, "advanced resurrection restores 75*power");
    Check(ResurrectionRestoreHp(3, 10) == 1000, "expert resurrection restores 100*power");
    Check(ResurrectionRestoreHp(0, 10) == 0, "unlearned spell restores nothing");

    // 阈值：先算倍率/100（浮点），再乘可恢复量。100.00% 存 10000。
    Check(ProtectThresholdHp(10000, 500) == 500, "ratio 100% keeps restorable hp");
    Check(ProtectThresholdHp(2500, 500) == 125, "ratio 25% is a quarter");
    Check(ProtectThresholdHp(3333, 1000) == 333, "ratio 33.33% rounds via truncation");

    // 严格小于阈值触发；未勾选不触发；全灭尸体可触发。
    Check(ProtectShouldCast(true, 10000, 500, 2, 499), "hp strictly below threshold casts");
    Check(!ProtectShouldCast(true, 10000, 500, 2, 500), "hp equal to threshold does not cast");
    Check(!ProtectShouldCast(false, 10000, 500, 2, 1), "unchecked protect disables");
    Check(ProtectShouldCast(true, 10000, 500, 0, 0), "dead stack with a corpse triggers");
    Check(!ProtectShouldCast(true, 10000, 0, 0, 0), "dead stack without the spell never triggers");

    // 默认规则：未勾选，倍率 100%。
    const AutoStackRule def = MakeDefaultRule();
    Check(def.protectEnable == 0 && def.protectRatioX100 == PROTECT_RATIO_DEFAULT_X100,
        "default rule has protect off at 100%");

    // 倍率夹范围（0..10000.00%）。
    AutoStackRule dirty = def;
    dirty.protectRatioX100 = -5;
    NormalizeRule(&dirty, 0, false, false, false);
    Check(dirty.protectRatioX100 == PROTECT_RATIO_MIN_X100, "negative ratio clamps to 0");
    dirty.protectRatioX100 = 5000000;
    NormalizeRule(&dirty, 0, false, false, false);
    Check(dirty.protectRatioX100 == PROTECT_RATIO_MAX_X100, "huge ratio clamps to max");
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
    TestStableStackIdentity();
    TestFailedActionPlayerHandoffEligibility();
    TestLegacySpellSlotCompatibility();
    TestProtect();
    std::cout << "PolicyCoreTests: " << g_checks << " checks passed\n";
    return 0;
}
