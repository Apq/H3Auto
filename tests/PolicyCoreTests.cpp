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
        {10, 10, 100, 0, 0, 20, 0, 0},
        {5, 10, 100, 0, 1, 10, 0, 1},
        {8, 10, 100, 0, 0, 30, 0, 0},
    };
    Check(SelectTargetIndex(candidates, 3, SEL_COUNT_HIGH, 0) == 0,
        "count high keeps first tie");
    Check(SelectTargetIndex(candidates, 3, SEL_RANGED_SPEED, 0) == 1,
        "ranged flyer speed prefers shooter before flyer");

    const TargetCandidate flight[] = {
        {5, 10, 100, 0, 1, 30, 0, 1},
        {5, 10, 100, 0, 1, 10, 1, 1},
        {5, 10, 100, 0, 0, 40, 1, 0},
    };
    Check(SelectTargetIndex(flight, 3, SEL_RANGED_SPEED, 0) == 1,
        "ranged flyer speed prefers flyer before speed");

    // 同类同速：剩余总血量高者优先（第三队 20×100=2000 > 第一队 5×100=500）。
    // 第二队弹药已空但类型仍是远程，不得因此掉出远程档。
    const TargetCandidate hp[] = {
        {5, 10, 100, 0, 0, 12, 0, 1},
        {8, 10, 100, 0, 0, 12, 0, 1},
        {20, 20, 100, 0, 0, 12, 0, 1},
    };
    Check(SelectTargetIndex(hp, 3, SEL_RANGED_SPEED, 0) == 2,
        "same ranged and speed prefers higher remaining hp");
    // 速度仍高于血量：血厚但更慢的远程不压过更快的远程。
    const TargetCandidate slow_fat[] = {
        {20, 20, 100, 0, 0, 8, 0, 1},
        {5, 10, 100, 0, 0, 15, 0, 1},
    };
    Check(SelectTargetIndex(slow_fat, 2, SEL_RANGED_SPEED, 0) == 1,
        "speed outranks remaining hp");
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
    Check(summon.kind == STACK_ID_SUMMON,
        "summon gets within-battle identity");
    Check(StableStackIdentityEquals(summon, MakeStableStackIdentity(0, -1, 10, 0)),
        "summon identity comparable within battle");

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
    Check(remap[2] == -1, "summon does not carry rule across retry");
}

void TestArchiveSlotMapRounds()
{
    // 完全相同：第一轮全中，映射恒等。空槽用 -1（真实存档口径）。
    {
        int types[21]; int counts[21];
        for (int i = 0; i < 21; ++i) { types[i] = -1; counts[i] = 0; }
        types[0] = 10; counts[0] = 20;
        types[1] = 11; counts[1] = 30;
        types[2] = 12; counts[2] = 40;
        int map[21] = {};
        BuildArchiveSlotMapByRounds(types, counts, types, counts, map);
        Check(map[0] == 0 && map[1] == 1 && map[2] == 2,
            "identical armies map identity");
        Check(map[3] == -1, "empty slots stay unmatched");
    }
    // 换槽 + 数量区分：槽位 0/1 互换类型，第一轮全不中；
    // 第二轮按类型+数量把各自规则搬回原部队。
    {
        int arch_t[21]; int arch_c[21];
        int cur_t[21]; int cur_c[21];
        for (int i = 0; i < 21; ++i) {
            arch_t[i] = -1; arch_c[i] = 0; cur_t[i] = -1; cur_c[i] = 0;
        }
        arch_t[0] = 10; arch_c[0] = 20; arch_t[1] = 11; arch_c[1] = 30;
        cur_t[0] = 11; cur_c[0] = 30; cur_t[1] = 10; cur_c[1] = 20;
        int map[21] = {};
        BuildArchiveSlotMapByRounds(arch_t, arch_c, cur_t, cur_c, map);
        Check(map[0] == 1 && map[1] == 0,
            "swapped stacks remap by type+count");
    }
    // 同类型多组：当前侧同槽类型错开时，第二轮按数量配对。
    {
        int arch_t[21]; int arch_c[21];
        int cur_t[21]; int cur_c[21];
        for (int i = 0; i < 21; ++i) {
            arch_t[i] = -1; arch_c[i] = 0; cur_t[i] = -1; cur_c[i] = 0;
        }
        arch_t[0] = 11; arch_c[0] = 50;
        arch_t[1] = 10; arch_c[1] = 9;
        arch_t[2] = 10; arch_c[2] = 7;
        cur_t[0] = 10; cur_c[0] = 9;
        cur_t[1] = 99; cur_c[1] = 1;
        cur_t[2] = 10; cur_c[2] = 7;
        int map[21] = {};
        BuildArchiveSlotMapByRounds(arch_t, arch_c, cur_t, cur_c, map);
        Check(map[0] == 1 && map[2] == 2 && map[1] == -1,
            "same-type stacks pair by count");
    }
    // 第三轮兜底：同类型不同数量仍关联；同槽换类型走第二轮。
    {
        int arch_t[21]; int arch_c[21];
        int cur_t[21]; int cur_c[21];
        for (int i = 0; i < 21; ++i) {
            arch_t[i] = -1; arch_c[i] = 0; cur_t[i] = -1; cur_c[i] = 0;
        }
        arch_t[0] = 10; arch_c[0] = 20;
        arch_t[1] = 11; arch_c[1] = 1;
        cur_t[0] = 11; cur_c[0] = 1;
        cur_t[1] = 10; cur_c[1] = 15;
        int map[21] = {};
        BuildArchiveSlotMapByRounds(arch_t, arch_c, cur_t, cur_c, map);
        Check(map[0] == 1 && map[1] == 0,
            "type-only fallback matches across slots");
    }
    // 同类型两组互换：第一轮降级（类型出现 2 次），第二轮按数量把
    // 规则带回原部队。
    {
        int arch_t[21]; int arch_c[21];
        int cur_t[21]; int cur_c[21];
        for (int i = 0; i < 21; ++i) {
            arch_t[i] = -1; arch_c[i] = 0; cur_t[i] = -1; cur_c[i] = 0;
        }
        arch_t[0] = 10; arch_c[0] = 20;
        arch_t[1] = 10; arch_c[1] = 30;
        cur_t[0] = 10; cur_c[0] = 30;
        cur_t[1] = 10; cur_c[1] = 20;
        int map[21] = {};
        BuildArchiveSlotMapByRounds(arch_t, arch_c, cur_t, cur_c, map);
        Check(map[0] == 1 && map[1] == 0,
            "duplicate-type swap pairs by count, not slot");
    }
    // 同类型两组 + 槽位数量全对：轮 1 三项全等直配，不降级。
    {
        int arch_t[21]; int arch_c[21];
        int cur_t[21]; int cur_c[21];
        for (int i = 0; i < 21; ++i) {
            arch_t[i] = -1; arch_c[i] = 0; cur_t[i] = -1; cur_c[i] = 0;
        }
        arch_t[0] = 10; arch_c[0] = 20;
        arch_t[1] = 10; arch_c[1] = 30;
        cur_t[0] = 10; cur_c[0] = 20;
        cur_t[1] = 10; cur_c[1] = 30;
        int map[21] = {};
        BuildArchiveSlotMapByRounds(arch_t, arch_c, cur_t, cur_c, map);
        Check(map[0] == 0 && map[1] == 1,
            "slot+type+count triple matches without demotion");
    }
    // 没换槽、规模变了：轮 2 配不上（数量不等）→ 轮 3 按槽位配。
    {
        int arch_t[21]; int arch_c[21];
        int cur_t[21]; int cur_c[21];
        for (int i = 0; i < 21; ++i) {
            arch_t[i] = -1; arch_c[i] = 0; cur_t[i] = -1; cur_c[i] = 0;
        }
        arch_t[0] = 10; arch_c[0] = 20;
        arch_t[1] = 11; arch_c[1] = 5;
        cur_t[0] = 10; cur_c[0] = 25;
        cur_t[1] = 11; cur_c[1] = 5;
        int map[21] = {};
        BuildArchiveSlotMapByRounds(arch_t, arch_c, cur_t, cur_c, map);
        Check(map[0] == 0 && map[1] == 1,
            "same-slot resized stack pairs by slot");
    }
    // 存档多出的部队：丢弃（当前侧找不到映射）。
    {
        int arch_t[21]; int arch_c[21];
        int cur_t[21]; int cur_c[21];
        for (int i = 0; i < 21; ++i) {
            arch_t[i] = -1; arch_c[i] = 0; cur_t[i] = -1; cur_c[i] = 0;
        }
        arch_t[0] = 10; arch_c[0] = 1;
        arch_t[1] = 11; arch_c[1] = 1;
        arch_t[2] = 12; arch_c[2] = 1;
        cur_t[0] = 11; cur_c[0] = 1;
        int map[21] = {};
        BuildArchiveSlotMapByRounds(arch_t, arch_c, cur_t, cur_c, map);
        Check(map[0] == 1 && map[1] == -1 && map[2] == -1,
            "extra archive stacks are dropped");
    }
    // 第一轮要求同槽同类型：同槽不同类型不给身份，避免错配。
    {
        int arch_t[21]; int arch_c[21];
        int cur_t[21]; int cur_c[21];
        for (int i = 0; i < 21; ++i) {
            arch_t[i] = -1; arch_c[i] = 0; cur_t[i] = -1; cur_c[i] = 0;
        }
        arch_t[0] = 10; arch_c[0] = 5;
        cur_t[0] = 11; cur_c[0] = 5;
        int map[21] = {};
        BuildArchiveSlotMapByRounds(arch_t, arch_c, cur_t, cur_c, map);
        Check(map[0] == -1, "different creature types never match");
    }
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

    // 方案级策略门槛：无 / 按数量 / 回合内首动 / 损失量大于恢复量。
    Check(!ProtectShouldCast(true, PS_NONE, 500, 600, 999, 20),
        "PS_NONE never casts");
    Check(ProtectShouldCast(true, PS_FIRST_ACTION, 500, 1, 999, 20),
        "first-action strategy casts on any loss");
    Check(!ProtectShouldCast(true, PS_FIRST_ACTION, 500, 0, 999, 20),
        "first-action strategy skips undamaged stacks");
    Check(ProtectShouldCast(true, PS_LOSS_GT_RESTORE, 500, 501, 999, 20),
        "loss above restorable casts");
    Check(!ProtectShouldCast(true, PS_LOSS_GT_RESTORE, 500, 500, 999, 20),
        "loss equal to restorable does not cast");
    Check(!ProtectShouldCast(true, PS_LOSS_GT_RESTORE, 0, 500, 0, 20),
        "unlearned spell never casts");
    Check(!ProtectShouldCast(false, PS_FIRST_ACTION, 500, 600, 999, 20),
        "not in queue disables");

    // 按数量策略：剩余数量 ≤ 该队阈值才救（阈值默认 2，范围 0..INT_MAX）。
    Check(ProtectShouldCast(true, PS_COUNT_BELOW, 500, 600, 15, 20),
        "count at threshold qualifies");
    Check(ProtectShouldCast(true, PS_COUNT_BELOW, 500, 600, 5, 20),
        "count below threshold qualifies");
    Check(!ProtectShouldCast(true, PS_COUNT_BELOW, 500, 600, 21, 20),
        "count above threshold does not qualify");
    Check(!ProtectShouldCast(true, PS_COUNT_BELOW, 500, 0, 5, 20),
        "undamaged stack never qualifies");
    Check(ProtectShouldCast(true, PS_COUNT_BELOW, 500, 600, 0, 0),
        "count zero qualifies at threshold 0");
    Check(ProtectShouldCast(true, PS_COUNT_BELOW, 500, 600,
            2147483647, 2147483647),
        "int-max threshold accepts any count");

    // 够格者中选目标：血量最低（全灭者剩余 0 天然最前）。
    {
        TargetCandidate cands[3] = {};
        // [0] 大天使 2 剩 1，hp200 lost30：wound=230, remaining=170
        cands[0].count_current = 1;
        cands[0].count_at_start = 2;
        cands[0].hit_points = 200;
        cands[0].lost_hp = 30;
        // [1] 冠军 5 剩 3，hp100 lost40：wound=240, remaining=260
        cands[1].count_current = 3;
        cands[1].count_at_start = 5;
        cands[1].hit_points = 100;
        cands[1].lost_hp = 40;
        // [2] 泰坦 4 全灭，hp150：wound=600, remaining=0
        cands[2].count_current = 0;
        cands[2].count_at_start = 4;
        cands[2].hit_points = 150;

        Check(SelectProtectTargetIndex(cands, 3) == 2,
            "lowest hp picks the dead stack first");
        Check(SelectProtectTargetIndex(cands, 2) == 0,
            "without the dead stack picks angel at 170 over champion at 260");
        Check(SelectProtectTargetIndex(nullptr, 3) == -1,
            "null candidates returns -1");
        Check(SelectProtectTargetIndex(cands, 0) == -1,
            "empty candidate list returns -1");
    }

    // 默认规则：未入保活队列。
    const AutoStackRule def = MakeDefaultRule();
    Check(def.protectEnable == 0, "default rule is not in the protect queue");
}

void TestProfileStoreRoundtrip()
{
    uint8_t strategy = PS_FIRST_ACTION;
    AutoStackRule rules[PROFILE_STORE_SLOTS] = {};
    for (int s = 0; s < PROFILE_STORE_SLOTS; ++s)
        rules[s] = MakeDefaultRule();
    rules[7].action = AA_MELEE_ATTACK;
    rules[7].target.meleeStandHex = 125;
    rules[7].target.meleeAttackHex = 108;
    rules[7].target.moveWaypoints[0] = 42;
    rules[7].target.moveWaypoints[15] = -1;
    rules[7].target.moveWaypointCount = 1;
    rules[7].spellSlots[0] = 3;
    rules[7].spellSlotCount = 1;
    rules[7].protectEnable = 1;
    rules[7].protectCountBelow = 123;
    rules[7].allowDefendFallback = true;
    rules[20].action = AA_RANGED_ATTACK;
    rules[20].target.selector = SEL_RANGED_SPEED;
    uint16_t stop_turns = 25;
    int army_types[PROFILE_STORE_SLOTS];
    int army_counts[PROFILE_STORE_SLOTS] = {};
    for (int i = 0; i < PROFILE_STORE_SLOTS; ++i) army_types[i] = -1;
    army_types[0] = 10; army_counts[0] = 20;
    army_types[1] = 11; army_counts[1] = 30;
    army_types[2] = 12; army_counts[2] = 40;

    char text[32 * 1024] = {};
    const int written = EncodeProfileStoreText(army_types, army_counts,
        strategy, rules, stop_turns, text, sizeof(text));
    Check(written > 0, "profile store encodes");

    uint8_t out_strategy = 0;
    uint16_t out_stop = 0;
    int out_types[PROFILE_STORE_SLOTS] = {};
    int out_counts[PROFILE_STORE_SLOTS] = {};
    AutoStackRule out_rules[PROFILE_STORE_SLOTS] = {};
    Check(DecodeProfileStoreText(text, out_types, out_counts, &out_strategy,
            out_rules, &out_stop),
        "profile store decodes");
    Check(out_types[0] == 10 && out_counts[0] == 20
        && out_types[2] == 12 && out_counts[2] == 40,
        "army table roundtrip");
    Check(out_types[3] == -1 && out_counts[3] == 0, "empty slot roundtrip");
    Check(out_strategy == strategy, "strategy roundtrip");
    Check(out_rules[7].action == AA_MELEE_ATTACK, "rule action roundtrip");
    Check(out_rules[7].target.meleeStandHex == 125, "melee stand roundtrip");
    Check(out_rules[7].target.meleeAttackHex == 108, "melee attack roundtrip");
    Check(out_rules[7].target.moveWaypoints[0] == 42, "waypoint roundtrip");
    Check(out_rules[7].target.moveWaypoints[15] == -1, "empty waypoint roundtrip");
    Check(out_rules[7].target.moveWaypointCount == 1, "waypoint count roundtrip");
    Check(out_rules[7].spellSlots[0] == 3, "spell slot roundtrip");
    Check(out_rules[7].spellSlotCount == 1, "spell count roundtrip");
    Check(out_rules[7].protectEnable == 1, "protect enable roundtrip");
    Check(out_rules[7].protectCountBelow == 123, "protect count threshold roundtrip");
    Check(out_rules[20].protectCountBelow == 2,
        "protect count default is 2");
    Check(out_rules[7].allowDefendFallback, "fallback roundtrip");
    Check(out_rules[20].action == AA_RANGED_ATTACK, "last slot action roundtrip");
    Check(out_rules[20].target.selector == SEL_RANGED_SPEED,
        "last slot selector roundtrip");
    Check(out_rules[0].action == AA_MANUAL, "default slot stays manual");
    Check(out_stop == stop_turns, "stop turns roundtrip");

    Check(!DecodeProfileStoreText("H3AP3 1 2 3", out_types, out_counts,
            &out_strategy, out_rules, &out_stop),
        "truncated store rejected");
    Check(!DecodeProfileStoreText("H3AP2 1 2 3", out_types, out_counts,
            &out_strategy, out_rules, &out_stop),
        "legacy five-slot magic rejected");
    text[4] = '4'; // H3AP5 -> H3AP4：上一版格式（含全灭后策略枚举）拒绝
    Check(!DecodeProfileStoreText(text, out_types, out_counts, &out_strategy,
            out_rules, &out_stop),
        "h3ap4 store rejected after enum reshuffle");
    text[4] = '3'; // H3AP4 -> H3AP3：59 字段规则旧格式拒绝
    Check(!DecodeProfileStoreText(text, out_types, out_counts, &out_strategy,
            out_rules, &out_stop),
        "h3ap3 store rejected after format bump");
    text[4] = '5';
    text[0] = 'X';
    Check(!DecodeProfileStoreText(text, out_types, out_counts, &out_strategy,
            out_rules, &out_stop),
        "bad magic rejected");
    Check(AutoStopShouldYield(10, 1000, 100, 9), "nine turns of damage projects within ten");
    Check(!AutoStopShouldYield(10, 1000, 990, 1), "slow damage stays running");
    Check(!AutoStopShouldYield(0, 1000, 1, 9), "zero threshold disables stop");
    Check(!AutoStopShouldYield(10, 1000, 1000, 5), "no damage does not stop");
    Check(!AutoStopShouldYield(10, 1000, 1100, 5), "enemy hp gain does not stop");
    Check(ProjectEnemyTurnsLeft(10, 1000, 100, 9) == 1, "remaining turns round up");
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
TestArchiveSlotMapRounds();
    TestFailedActionPlayerHandoffEligibility();
    TestLegacySpellSlotCompatibility();
    TestProtect();
    TestProfileStoreRoundtrip();
    std::cout << "PolicyCoreTests: " << g_checks << " checks passed\n";
    return 0;
}
