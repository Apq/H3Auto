#include "../modules/PolicyCore.hpp"
#include "../modules/CrashGuardCore.hpp"
#include "../modules/PanelInputCore.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>

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

void TestPanelItemOwnership()
{
    int dialog = 0, other_dialog = 0;
    int first_item = 0, current_item = 0, recycled_resource_node = 0;
    int* items[] = { &first_item, &current_item };
    Check(PanelOwnsTrackedItem(&dialog, &dialog, items, 2, &current_item),
        "current dialog owns tracked input blocker");
    Check(PanelOwnsTrackedItem(&dialog, &dialog, items, 2, &first_item),
        "owned item can be first in list");
    Check(!PanelOwnsTrackedItem(&dialog, &dialog, items, 2, &recycled_resource_node),
        "retry reused dialog address does not authorize old item writes");
    Check(!PanelOwnsTrackedItem(&other_dialog, &dialog, items, 2, &current_item),
        "different current dialog rejects tracked item");
    Check(!PanelOwnsTrackedItem(nullptr, &dialog, items, 2, &current_item),
        "missing current dialog rejects tracked item");
    Check(!PanelOwnsTrackedItem(&dialog, nullptr, items, 2, &current_item),
        "missing tracked dialog rejects item");
    Check(!PanelOwnsTrackedItem(&dialog, &dialog, static_cast<int* const*>(nullptr),
            2, &current_item), "missing owned list rejects item");
    Check(!PanelOwnsTrackedItem(&dialog, &dialog, items, 0, &current_item),
        "empty owned list rejects item");
    Check(!PanelOwnsTrackedItem(&dialog, &dialog, items, 2,
            static_cast<int*>(nullptr)), "missing tracked item rejects access");
    Check(!PanelOwnsTrackedItem(&dialog, &dialog, items, 65537, &current_item),
        "corrupt owned list count rejects access before scanning");
}

void TestPanelAdmission()
{
    Check(!IsConfigurablePanelStack(CREATURE_AMMO_CART, 10, true, false),
        "ammo cart is never configurable");
    Check(!IsConfigurablePanelStack(CREATURE_CATAPULT, 10, false, false),
        "catapult requires ballistics");
    Check(IsConfigurablePanelStack(CREATURE_CATAPULT, 10, true, false),
        "catapult with ballistics is configurable");
    Check(IsConfigurablePanelStack(CREATURE_FIRST_AID_TENT, 10, false, false),
        "first aid tent is always configurable");
    Check(!IsConfigurablePanelStack(CREATURE_BALLISTA, 0, true, false),
        "empty slot (never fielded) is not configurable");
    Check(IsConfigurablePanelStack(CREATURE_BALLISTA, 20, true, false),
        "fallen own stack stays listed for config (revive keeps rule)");
    Check(!IsConfigurablePanelStack(10, 0, false, false),
        "empty non-machine slot is not configurable");
    Check(!IsConfigurablePanelStack(kSummonCreatureIds[0], 10, false, true),
        "summoned elemental is not configurable");
    Check(!IsConfigurablePanelStack(10, 10, false, true),
        "clone stack is not configurable");
    Check(IsConfigurablePanelStack(kSummonCreatureIds[0], 10, false, false),
        "brought-in elemental army stack stays configurable");
}

void TestBattleFingerprint()
{
    BattleFingerprintInput a = {};
    a.hero_id[0] = 7;   a.hero_id[1] = -1;               // 攻方英雄 / 守方野怪
    a.side_types[0][0] = 1;  a.side_counts[0][0] = 20;   // 攻方枪兵 20
    a.side_types[0][1] = 13; a.side_counts[0][1] = 5;    // 攻方弓手 5
    a.side_types[1][0] = 66; a.side_counts[1][0] = 40;   // 守方巨兽 40
    a.terrain = 3; a.siege_kind = 0;
    a.map_x = 42; a.map_y = 17; a.map_z = 1;             // 地下

    const unsigned long long fp = ComputeBattleFingerprint(a);
    Check(fp != 0, "fingerprint nonzero");

    // 槽位重排 / 乱序：同内容同指纹。
    BattleFingerprintInput b = a;
    b.side_types[0][0] = 13; b.side_counts[0][0] = 5;
    b.side_types[0][1] = 1;  b.side_counts[0][1] = 20;
    Check(ComputeBattleFingerprint(b) == fp, "slot order irrelevant");

    // 数量差 1：不同战斗。
    BattleFingerprintInput c = a;
    c.side_counts[1][0] = 41;
    Check(ComputeBattleFingerprint(c) != fp, "one fewer monster differs");

    // 地上层同坐标：不同战斗（z 区分地上/地下）。
    BattleFingerprintInput d = a;
    d.map_z = 0;
    Check(ComputeBattleFingerprint(d) != fp, "surface vs underground differs");

    // 触发点挪 1 格：不同战斗。
    BattleFingerprintInput e = a;
    e.map_x = 43;
    Check(ComputeBattleFingerprint(e) != fp, "adjacent trigger tile differs");

    // 攻守互换：不同战斗。
    BattleFingerprintInput f = a;
    for (int i = 0; i < 21; ++i) {
        const int t = f.side_types[0][i]; f.side_types[0][i] = f.side_types[1][i];
        f.side_types[1][i] = t;
        const int n = f.side_counts[0][i]; f.side_counts[0][i] = f.side_counts[1][i];
        f.side_counts[1][i] = n;
    }
    const int h = f.hero_id[0]; f.hero_id[0] = f.hero_id[1]; f.hero_id[1] = h;
    Check(ComputeBattleFingerprint(f) != fp, "attacker/defender swap differs");

    // 空槽不参与：补 type=0 槽同指纹。
    BattleFingerprintInput g = a;
    g.side_types[0][20] = 99;  // type>0 但 count=0 → 仍不算
    Check(ComputeBattleFingerprint(g) == fp, "count-zero slot ignored");
}

void TestForceFieldFields()
{
    const ForceFieldProfileFields def = MakeDefaultForceFieldFields();
    Check(def.anchor_hex[0] == -1 && def.anchor_hex[1] == -1,
        "force field defaults have two unset anchors");
    ForceFieldProfileFields fields = def;
    NormalizeForceFieldFields(&fields);
    Check(fields.anchor_hex[0] == -1 && fields.anchor_hex[1] == -1,
        "force field defaults normalize unchanged");
    NormalizeForceFieldFields(nullptr);
    const ForceFieldProfileFields inputs[] = {
        {{86, 87}}, {{86, 86}}, {{-1, 87}}, {{0, 185}},
        {{16, 17}}, {{185, 1}}, {{-9, 186}}, {{1, 170}},
    };
    const ForceFieldProfileFields expected[] = {
        {{86, 87}}, {{86, -1}}, {{87, -1}}, {{185, -1}},
        {{-1, -1}}, {{185, 1}}, {{-1, -1}}, {{1, -1}},
    };
    for (int i = 0; i < 8; ++i) {
        fields = inputs[i];
        NormalizeForceFieldFields(&fields);
        Check(fields.anchor_hex[0] == expected[i].anchor_hex[0]
                && fields.anchor_hex[1] == expected[i].anchor_hex[1],
            "force field normalization removes duplicates and compacts anchors");
        NormalizeForceFieldFields(&fields);
        Check(fields.anchor_hex[0] == expected[i].anchor_hex[0]
                && fields.anchor_hex[1] == expected[i].anchor_hex[1],
            "force field normalization is idempotent");
    }
    for (int anchor = -2; anchor <= 187; ++anchor) {
        const bool valid = anchor >= 1 && anchor <= 185
            && anchor % 17 != 0 && anchor % 17 != 16;
        fields = {{anchor, -1}};
        NormalizeForceFieldFields(&fields);
        Check(fields.anchor_hex[0] == (valid ? anchor : -1)
                && fields.anchor_hex[1] == -1,
            "force field first anchor normalizes battlefield borders and bounds");
        fields = {{-1, anchor}};
        NormalizeForceFieldFields(&fields);
        Check(fields.anchor_hex[0] == (valid ? anchor : -1)
                && fields.anchor_hex[1] == -1,
            "force field second anchor normalizes and compacts into first slot");
        Check(IsValidForceFieldAnchor(anchor) == valid,
            "force field anchor validation matches normalized value");
    }
}

void TestForceFieldSelection()
{
    const ForceFieldPresence states[] = {FF_UNKNOWN, FF_ABSENT, FF_PRESENT};
    const ForceFieldProfileFields configurations[] = {
        {{-1, -1}}, {{86, -1}}, {{-1, 87}}, {{86, 87}}, {{87, 86}},
    };
    for (const ForceFieldProfileFields& fields : configurations) {
        for (ForceFieldPresence first : states) {
            for (ForceFieldPresence second : states) {
                const ForceFieldPresence presence[2] = {first, second};
                const int expected = fields.anchor_hex[0] != -1 && first != FF_PRESENT
                    ? fields.anchor_hex[0]
                    : fields.anchor_hex[1] != -1 && second != FF_PRESENT
                        ? fields.anchor_hex[1] : -1;
                Check(SelectForceFieldAnchor(fields, presence) == expected,
                    "force field selects first configured missing or unknown anchor");
            }
        }
    }
    const int invalid_anchors[] = {0, 16, 17, 33, 170, 186};
    for (int anchor : invalid_anchors) {
        const ForceFieldProfileFields fields = {{anchor, 87}};
        for (ForceFieldPresence first : states) {
            for (ForceFieldPresence second : states) {
                const ForceFieldPresence presence[2] = {first, second};
                Check(SelectForceFieldAnchor(fields, presence)
                        == (second == FF_PRESENT ? -1 : 87),
                    "force field skips invalid slot and returns anchor rather than slot index");
            }
        }
    }
}

void TestBattleStoreRecord()
{
    // 基准记录：方案 2 激活，方案 1 的 0 号槽有非默认动作。
    BattleStoreRecord r1 = {};
    strcpy(r1.time, "20261004-164530");
    r1.active = 1;
    r1.rules[0][0].action = AA_DEFEND;
    r1.rules[3][20].action = AA_RANGED_ATTACK;
    r1.stop_turns[4] = 7;
    r1.summon[2].enabled = 1;
    r1.summon[2].count_th = 3;
    r1.summon[2].hp_th = 800;
    for (int p = 0; p < 5; ++p)
        r1.forcefield[p] = MakeDefaultForceFieldFields();
    r1.forcefield[2] = {{86, 87}};

    // 完全相同（时间戳相同）→ 内容相同。
    BattleStoreRecord r2 = r1;
    Check(BattleStoreRecordContentEquals(r1, r2), "identical records equal");

    // 仅时间戳不同 → 内容仍相同（去重依据）。
    strcpy(r2.time, "20261004-180001");
    Check(BattleStoreRecordContentEquals(r1, r2),
        "timestamp ignored in compare");

    // 每类字段单独改动都算内容变化。
    BattleStoreRecord r3 = r1; r3.active = 3;
    Check(!BattleStoreRecordContentEquals(r1, r3), "active change differs");
    BattleStoreRecord r4 = r1; r4.rules[3][20].action = AA_DEFEND;
    Check(!BattleStoreRecordContentEquals(r1, r4), "one rule change differs");
    BattleStoreRecord r5 = r1; r5.summon[2].enabled = 0;
    Check(!BattleStoreRecordContentEquals(r1, r5), "summon enable differs");
    BattleStoreRecord r6 = r1; r6.stop_turns[4] = 8;
    Check(!BattleStoreRecordContentEquals(r1, r6), "stop turns change differs");
    BattleStoreRecord r7 = r1; r7.summon[2].hp_th = 801;
    Check(!BattleStoreRecordContentEquals(r1, r7), "summon field change differs");
    for (int p = 0; p < 5; ++p) {
        for (int slot = 0; slot < 2; ++slot) {
            BattleStoreRecord changed = r1;
            changed.forcefield[p].anchor_hex[slot] = 88;
            Check(!BattleStoreRecordContentEquals(r1, changed),
                "either force field anchor change differs in every profile");
            changed.forcefield[p].anchor_hex[slot] = r1.forcefield[p].anchor_hex[slot];
            Check(BattleStoreRecordContentEquals(r1, changed),
                "restored force field anchor deduplicates in every profile");
        }
    }

    // 时间戳格式校验。
    Check(BattleStoreStampValid("20261004-164530"), "valid stamp");
    Check(!BattleStoreStampValid("20261004-246530"), "hour 24 invalid");
    Check(!BattleStoreStampValid("20261004-164660"), "second 60 invalid");
    Check(!BattleStoreStampValid("20261304-164530"), "month 13 invalid");
    Check(!BattleStoreStampValid("20261004 164530"), "dash required");
    Check(!BattleStoreStampValid("20261004-1645"), "too short");
    Check(!BattleStoreStampValid("20261004-1645300"), "too long");
    Check(!BattleStoreStampValid(""), "empty invalid");
    Check(!BattleStoreStampValid(nullptr), "null invalid");
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
    // 原版口径：baseValue[等级] + 力量 × spEffect。表值非法时退回旧估算。
    Check(ResurrectionRestoreHp(40, 50, 3, 33) == 1690,
        "spell table restore is base plus power times effect");
    Check(ResurrectionRestoreHp(-1, 50, 3, 33) == 3300,
        "invalid spell table falls back to level times power");

    // 保活方式（部队级，三选一，默认不保活）。
    Check(!ProtectShouldCast(PM_NONE, 500, 600, 0, 20),
        "protect off never casts");
    Check(!ProtectShouldCast(PM_NONE, 500, 600, 5, 0),
        "protect off ignores count threshold 0");
    Check(ProtectShouldCast(PM_LOSS_GT_RESTORE, 500, 501, 999, 20),
        "loss above restorable casts");
    Check(!ProtectShouldCast(PM_LOSS_GT_RESTORE, 500, 500, 999, 20),
        "loss equal to restorable does not cast");
    Check(!ProtectShouldCast(PM_LOSS_GT_RESTORE, 0, 500, 0, 20),
        "unlearned spell never casts");

    // 剩余数量方式：剩余数量 ≤ 该队阈值才救（阈值默认 2，范围 0..INT_MAX）。
    Check(ProtectShouldCast(PM_COUNT_BELOW, 500, 600, 15, 20),
        "count at threshold qualifies");
    Check(ProtectShouldCast(PM_COUNT_BELOW, 500, 600, 5, 20),
        "count below threshold qualifies");
    Check(!ProtectShouldCast(PM_COUNT_BELOW, 500, 600, 21, 20),
        "count above threshold does not qualify");
    Check(!ProtectShouldCast(PM_COUNT_BELOW, 500, 0, 5, 20),
        "undamaged stack never qualifies");
    Check(ProtectShouldCast(PM_COUNT_BELOW, 500, 600, 0, 0),
        "count zero qualifies at threshold 0");
    Check(ProtectShouldCast(PM_COUNT_BELOW, 500, 600,
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

    // 默认规则：保活默认不开启（三选一），阈值默认 2。
    const AutoStackRule def = MakeDefaultRule();
    Check(def.protectMode == PM_NONE,
        "default rule does not protect");
    Check(def.protectCountBelow == 2, "default protect threshold is 2");
}

void TestProfileStoreRoundtrip()
{
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
    rules[7].protectMode = PM_LOSS_GT_RESTORE;
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

    SummonProfileFields summon = MakeDefaultSummonFields();
    summon.enabled = 1;
    summon.count_th = 3;
    summon.hp_th = 900;
    summon.spell_pick = 2;
    summon.cond_combine = SUMMON_COMBINE_OR;
    summon.stop_enemy_mana = 1;
    summon.stop_mana_th = 258;
    summon.summon_rule.action = AA_SCATTER;
    summon.summon_rule.allowDefendFallback = true; // 散开须清掉

    Check(PROFILE_STORE_INTS == 1370, "h3ap9 profile store has 1370 ints");
    Check(summon.stop_mana_th == 258
        && MakeDefaultSummonFields().stop_mana_th == 6,
        "summon mana threshold default 6");
    Check(MakeDefaultSummonFields().enabled == 0,
        "summon disabled by default");
    Check(MakeDefaultSummonFields().cond_combine == SUMMON_COMBINE_AND,
        "summon conditions default to AND");

    char text[32 * 1024] = {};
    const int written = EncodeProfileStoreText(army_types, army_counts,
        rules, stop_turns, summon, text, sizeof(text));
    Check(written > 0, "profile store encodes");

    uint16_t out_stop = 0;
    int out_types[PROFILE_STORE_SLOTS] = {};
    int out_counts[PROFILE_STORE_SLOTS] = {};
    AutoStackRule out_rules[PROFILE_STORE_SLOTS] = {};
    SummonProfileFields out_summon = {};
    Check(DecodeProfileStoreText(text, out_types, out_counts,
            out_rules, &out_stop, &out_summon),
        "profile store decodes");
    Check(out_types[0] == 10 && out_counts[0] == 20
        && out_types[2] == 12 && out_counts[2] == 40,
        "army table roundtrip");
    Check(out_types[3] == -1 && out_counts[3] == 0, "empty slot roundtrip");
    Check(out_summon.enabled == 1, "summon enable roundtrip");
    Check(out_rules[7].action == AA_MELEE_ATTACK, "rule action roundtrip");
    Check(out_rules[7].target.meleeStandHex == 125, "melee stand roundtrip");
    Check(out_rules[7].target.meleeAttackHex == 108, "melee attack roundtrip");
    Check(out_rules[7].target.moveWaypoints[0] == 42, "waypoint roundtrip");
    Check(out_rules[7].target.moveWaypoints[15] == -1, "empty waypoint roundtrip");
    Check(out_rules[7].target.moveWaypointCount == 1, "waypoint count roundtrip");
    Check(out_rules[7].spellSlots[0] == 3, "spell slot roundtrip");
    Check(out_rules[7].spellSlotCount == 1, "spell count roundtrip");
    Check(out_rules[7].protectMode == PM_LOSS_GT_RESTORE,
        "protect mode roundtrip");
    Check(out_rules[7].protectCountBelow == 123, "protect count threshold roundtrip");
    Check(out_rules[20].protectMode == PM_NONE,
        "protect mode default is off");
    Check(out_rules[20].protectCountBelow == 2,
        "protect count default is 2");
    Check(out_rules[7].allowDefendFallback, "fallback roundtrip");
    Check(out_rules[20].action == AA_RANGED_ATTACK, "last slot action roundtrip");
    Check(out_rules[20].target.selector == SEL_RANGED_SPEED,
        "last slot selector roundtrip");
    Check(out_rules[0].action == AA_MANUAL, "default slot stays manual");
    Check(out_stop == stop_turns, "stop turns roundtrip");
    Check(out_summon.count_th == 3 && out_summon.hp_th == 900
        && out_summon.spell_pick == 2 && out_summon.cond_combine == SUMMON_COMBINE_OR
        && out_summon.stop_enemy_mana == 1
        && out_summon.stop_mana_th == 258,
        "summon fields roundtrip");
    Check(out_summon.summon_rule.action == AA_SCATTER,
        "summon rule action roundtrip");
    Check(!out_summon.summon_rule.allowDefendFallback,
        "scatter clears fallback on decode");
    Check(out_summon.summon_rule.protectMode == PM_NONE,
        "summon rule uses protect off");

    Check(!DecodeProfileStoreText("H3AP3 1 2 3", out_types, out_counts,
            out_rules, &out_stop, &out_summon),
        "truncated store rejected");
    Check(!DecodeProfileStoreText("H3AP2 1 2 3", out_types, out_counts,
            out_rules, &out_stop, &out_summon),
        "legacy five-slot magic rejected");
    text[4] = '5'; // H3AP9 -> H3AP5：旧格式（无保活方式字段）拒绝
    Check(!DecodeProfileStoreText(text, out_types, out_counts,
            out_rules, &out_stop, &out_summon),
        "h3ap5 store rejected after format bump");
    text[4] = '7'; // H3AP7：旧格式（无保活方式字段）拒绝
    Check(!DecodeProfileStoreText(text, out_types, out_counts,
            out_rules, &out_stop, &out_summon),
        "h3ap7 store rejected after format bump");
    text[4] = '8'; // H3AP8：上一版格式（6 召唤整数、无条件组合）拒绝
    Check(!DecodeProfileStoreText(text, out_types, out_counts,
            out_rules, &out_stop, &out_summon),
        "h3ap8 store rejected after format bump");
    text[4] = '9';
    // H3AP9 头 + 少 1 个整数（模拟旧版无 protectMode 的规则）→ 拒绝。
    {
        const char* src = text + 5;
        static char trimmed[32 * 1024] = "H3AP9";
        int vlen = 5, seen = 0, dropped = 0;
        while (*src) {
            while (*src == ' ') ++src;
            if (!*src) break;
            const char* tok = src;
            while (*src && *src != ' ') ++src;
            ++seen; // 1-based；规则区从第 50 位起，每条 60 个的末位
            if (seen >= 50 && ((seen - 50) % 60 == 59)) { ++dropped; continue; }
            trimmed[vlen++] = ' ';
            for (const char* q = tok; q < src; ++q) trimmed[vlen++] = *q;
        }
        trimmed[vlen] = 0;
        Check(dropped == 22, "trimmed text drops one int per rule");
        Check(!DecodeProfileStoreText(trimmed, out_types, out_counts,
                out_rules, &out_stop, &out_summon),
            "wrong int count rejected");
    }
    text[0] = 'X';
    Check(!DecodeProfileStoreText(text, out_types, out_counts,
            out_rules, &out_stop, &out_summon),
        "bad magic rejected");
    Check(AutoStopShouldYield(10, 1000, 100, 9), "nine turns of damage projects within ten");
    Check(!AutoStopShouldYield(10, 1000, 990, 1), "slow damage stays running");
    Check(!AutoStopShouldYield(0, 1000, 1, 9), "zero threshold disables stop");
    Check(!AutoStopShouldYield(10, 1000, 1000, 5), "no damage does not stop");
    Check(!AutoStopShouldYield(10, 1000, 1100, 5), "enemy hp gain does not stop");
    Check(ProjectEnemyTurnsLeft(10, 1000, 100, 9) == 1, "remaining turns round up");

    const int durations[] = {3, 1, -1, 0};
    Check(ChooseBuffToRefresh(durations, 4, 1) == 3,
        "buff refresh picks the shortest remaining duration");
    Check(ChooseBuffToRefresh(durations, 2, 1) == 1,
        "buff at one turn still refreshes");
    const int fresh[] = {2, 4};
    Check(ChooseBuffToRefresh(fresh, 2, 1) == -1,
        "buff above one turn is not refreshed");
    // 玩家可调阈值：阈值 3 时剩余 2/3 都达标，取最少者。
    Check(ChooseBuffToRefresh(fresh, 2, 3) == 0,
        "wider threshold refreshes the two-turn buff first");
    Check(ChooseBuffToRefresh(fresh, 2, 2) == 0,
        "threshold two refreshes the two-turn buff");
    Check(ChooseBuffToRefresh(fresh, 2, 0) == -1,
        "no buff qualifies above the threshold");
    Check(ChooseBuffToRefresh(durations, 4, 0) == 3,
        "missing buff at zero turns is the most urgent");
    Check(MakeDefaultStatusFields().refresh_turns == 1,
        "status refresh threshold defaults to one");
    const int mixed[] = {0, 5, 0};
    const int representative = RepresentativeMassDuration(mixed, 3);
    Check(representative == 5,
        "mass buff uses one stack that already has it");
    const int missing[] = {0, 0, -1};
    Check(RepresentativeMassDuration(missing, 3) == 0,
        "mass buff is missing only when nobody has it");
    Check(RepresentativeMassDuration(nullptr, 3) == -1,
        "mass buff without stacks is not refreshable");
    Check(!CreatureCanReceiveStatusSpell(41, 0, 0x40000u, 5, 56),
        "undead cannot receive bless");
    Check(!CreatureCanReceiveStatusSpell(41, 0, 0, 0, 148),
        "zero-damage creatures cannot receive bless");
    Check(CreatureCanReceiveStatusSpell(41, 0, 0, 50, 13),
        "living attackers can receive bless");
    Check(!CreatureCanReceiveStatusSpell(49, 0, 0x20000u, 50, 13),
        "no-morale creatures cannot receive mirth");
    Check(CreatureCanReceiveStatusSpell(49, 0, 0, 50, 6),
        "ordinary creatures can receive mirth");
    Check(!CreatureCanReceiveStatusSpell(44, 0, 0, 10, 6),
        "non-shooters cannot receive precision");
    Check(CreatureCanReceiveStatusSpell(44, 0, 0x4u, 10, 6),
        "shooters can receive precision");
    Check(!CreatureCanReceiveStatusSpell(51, 0, 0, 0, 147),
        "zero-damage creatures cannot receive fortune");
    Check(!CreatureCanReceiveStatusSpell(55, 0, 0, 0, 147),
        "zero-damage creatures cannot receive slayer");
    Check(!CreatureCanReceiveStatusSpell(27, 0x1000u, 0x40u, 0, 145),
        "siege weapons cannot receive a cannot-target-siege spell");
    Check(CreatureCanReceiveStatusSpell(27, 0, 0x40u, 0, 145),
        "siege weapons can receive a spell that allows them");
    Check(!CreatureCanReceiveStatusSpell(27, 0, 0, 20, 149),
        "arrow towers receive no status spell");
    Check(CreatureCanReceiveStatusSpell(54, 0, 0x40000u, 5, 56),
        "slow has no undead exclusion");
    Check(ChooseBuffToRefresh(&representative, 1, 1) == -1,
        "representative duration above threshold is not refreshed");
    // 语义 A：敌方已有任意一队带迟缓（剩余 >1）即达标，不再施全群体
    // （抵抗术场景下部分命中就算完成，避免连续回合补群体）。
    const int enemy_durations[] = {4, 1, 0};
    const int enemy_hexes[] = {10, 20, 30};
    Check(ChooseSlowTarget(true, enemy_durations, enemy_hexes, 3, 1).spell_id == -1,
        "slow: any covered enemy means no cast");
    Check(ChooseSlowTarget(false, enemy_durations, enemy_hexes, 3, 1).spell_id == -1,
        "slow without expert mass does nothing");
    const int uncovered[] = {1, 0, -1};
    const StatusMaintainChoice slow =
        ChooseSlowTarget(true, uncovered, enemy_hexes, 3, 1);
    Check(slow.spell_id == 54 && slow.target_hex == 20 && slow.mass == 1,
        "slow: fully uncovered enemy side casts on the fewest-turns enemy");
    Check(ChooseSlowTarget(true, uncovered, enemy_hexes, 0, 1).spell_id == -1,
        "slow: no enemies means no cast");
    // 阈值同步作用于减速覆盖判定：剩余 2 > 阈值 1 视为已覆盖。
    const int covered2[] = {2, 0};
    Check(ChooseSlowTarget(true, covered2, enemy_hexes, 2, 1).spell_id == -1,
        "slow: two turns left above threshold one counts covered");
    Check(ChooseSlowTarget(true, covered2, enemy_hexes, 2, 3).spell_id == 54,
        "slow: wider threshold recasts at two turns left");
}

void TestSummonChannel()
{
    // 时机判定：队数严格小于阈值、血量 ≤ 阈值，按「和/或」组合，加施法/已学/法力守卫。
    // 默认「和」：两个条件都满足才召。
    Check(!SummonShouldCast(2, 2, 751, 750, SUMMON_COMBINE_AND, 30, 15, false, true),
        "AND: count equal and hp above does not cast");
    Check(SummonShouldCast(1, 2, 750, 750, SUMMON_COMBINE_AND, 30, 15, false, true),
        "AND: both conditions met casts");
    Check(!SummonShouldCast(1, 2, 4000, 750, SUMMON_COMBINE_AND, 30, 15, false, true),
        "AND: count alone does not cast");
    Check(!SummonShouldCast(5, 2, 750, 750, SUMMON_COMBINE_AND, 30, 15, false, true),
        "AND: hp alone does not cast");
    Check(SummonShouldCast(0, 1, 0, 0, SUMMON_COMBINE_AND, 30, 15, false, true),
        "AND: both zero thresholds met with nothing alive");
    Check(!SummonShouldCast(0, 0, 5000, 0, SUMMON_COMBINE_AND, 30, 15, false, true),
        "AND: zero count threshold never satisfied");
    // 「或」：任一满足即召。
    Check(SummonShouldCast(1, 2, 4000, 750, SUMMON_COMBINE_OR, 30, 15, false, true),
        "OR: stack count alone below threshold casts");
    Check(SummonShouldCast(5, 2, 750, 750, SUMMON_COMBINE_OR, 30, 15, false, true),
        "OR: hp total alone at threshold casts");
    Check(!SummonShouldCast(3, 2, 751, 750, SUMMON_COMBINE_OR, 30, 15, false, true),
        "OR: neither threshold met does not cast");
    Check(SummonShouldCast(0, 0, 0, 0, SUMMON_COMBINE_OR, 30, 15, false, true),
        "OR: zero count threshold still casts via hp zero");
    // 守卫与组合方式无关。
    Check(!SummonShouldCast(1, 2, 750, 750, SUMMON_COMBINE_AND, 30, 15, true, true),
        "already casted this turn blocks");
    Check(!SummonShouldCast(1, 2, 750, 750, SUMMON_COMBINE_OR, 14, 15, false, true),
        "not enough mana blocks");
    Check(SummonShouldCast(1, 2, 750, 750, SUMMON_COMBINE_AND, 30, 0, false, true),
        "zero mana cost never blocks mana check");
    Check(!SummonShouldCast(1, 2, 750, 750, SUMMON_COMBINE_OR, 30, 15, false, false),
        "unlearned spell never casts");

    // 元素判定：气112/土113/火114/水115。
    Check(IsSummonedElemental(112) && IsSummonedElemental(113)
        && IsSummonedElemental(114) && IsSummonedElemental(115),
        "four elementals recognized");
    Check(!IsSummonedElemental(111) && !IsSummonedElemental(116)
        && !IsSummonedElemental(0x95),
        "non-elemental ids rejected");

    // 选法术：固定（含未学拒绝）、自动（量最大、锁定优先、平手靠前、全未学）。
    const int amounts[SUMMON_ELEMENT_COUNT] = {100, 250, 250, 90};
    const bool all_learned[SUMMON_ELEMENT_COUNT] = {true, true, true, true};
    const bool none_learned[SUMMON_ELEMENT_COUNT] = {false, false, false, false};
    const bool partial[SUMMON_ELEMENT_COUNT] = {true, false, true, false};
    Check(PickSummonSpell(amounts, all_learned, 1, -1) == 0,
        "fixed air picks air");
    Check(PickSummonSpell(amounts, none_learned, 1, -1) == -1,
        "fixed unlearned refuses");
    Check(PickSummonSpell(amounts, all_learned, 0, -1) == 1,
        "auto picks largest amount");
    Check(PickSummonSpell(amounts, partial, 0, -1) == 2,
        "auto picks largest among learned");
    Check(PickSummonSpell(amounts, all_learned, 0, 3) == 3,
        "locked element overrides auto");
    Check(PickSummonSpell(amounts, none_learned, 0, -1) == -1,
        "auto with nothing learned refuses");
    const int tie[SUMMON_ELEMENT_COUNT] = {200, 200, 200, 200};
    Check(PickSummonSpell(tie, all_learned, 0, -1) == 0,
        "tie keeps front element");

    // 侧统计：含召唤物、调用方负责剔除战争机器与死亡槽。
    const TargetCandidate side[] = {
        {10, 10, 20, 0, 0, 0, 0, 0},   // 200
        {5, 5, 30, 30, 0, 0, 0, 0},    // 120
        {0, 7, 25, 0, 0, 0, 0, 0},     // 全灭不计
    };
    Check(CountAliveSideStacks(side, 3) == 2, "alive count skips dead stacks");
    Check(SumSideRemainingHp(side, 3) == 320, "hp sum skips dead stacks");

    // 自动停止第二条件。
    Check(ShouldStopOnEnemyMana(1, true, true, 6, 6),
        "mana at threshold stops");
    Check(ShouldStopOnEnemyMana(1, true, true, 0, 6),
        "drained mana stops");
    Check(!ShouldStopOnEnemyMana(1, true, true, 7, 6),
        "mana above threshold continues");
    Check(!ShouldStopOnEnemyMana(0, true, true, 0, 6),
        "disabled flag never stops");
    Check(!ShouldStopOnEnemyMana(1, false, true, 0, 6),
        "no enemy hero never stops");
    Check(!ShouldStopOnEnemyMana(1, true, false, 0, 6),
        "no spellbook never stops");
    Check(!ShouldStopOnEnemyMana(1, true, true, -1, 6),
        "negative mana read never stops");
}

void TestSummonMoveHex()
{
    // 六格距离：同行相邻 1；跨行邻居 1；隔一格 2；无效格无穷远。
    Check(HexCoordDistance(0, 0) == 0, "same hex distance zero");
    Check(HexCoordDistance(0, 1) == 1, "same row neighbour is one");
    Check(HexCoordDistance(0, 15) == 1, "row below neighbour is one");
    Check(HexCoordDistance(0, 2) == 2, "same row skip is two");
    Check(HexCoordDistance(0, 16) == 2, "odd row next column is two away");
    Check(HexCoordDistance(0, 17) == 3, "odd row second column is three away");
    Check(HexCoordDistance(0, 45) == 3, "third row distance three");
    Check(HexCoordDistance(15, 16) == 1, "cross row diagonal is one");
    Check(HexCoordDistance(-1, 5) > 10000, "invalid hex is far away");

    // 散开：own 在 hex 0，自身在 hex 30（距离 2，已达标）。
    const int own1[1] = {0};
    const int far_cands[3] = {58, 59, 60};
    Check(ChooseSummonMoveHex(AA_SCATTER, far_cands, 3, 30, own1, 1, 0) == -1,
        "satisfied tier stays put");

    // 当前格相邻（距离 1），有 ≥2 候选 → 挑 min-dist 最大者。
    const int near_cands[2] = {17, 90};
    Check(HexCoordDistance(15, 0) == 1, "setup: current is adjacent");
    Check(HexCoordDistance(17, 0) == 3, "setup: first candidate tier two");
    Check(HexCoordDistance(90, 0) == 6, "setup: second candidate farther");
    Check(ChooseSummonMoveHex(AA_SCATTER, near_cands, 2, 15, own1, 1, 0) == 90,
        "scatter picks farthest candidate in tier");

    // ≥2 全场做不到 → 降级 ≥1：当前格与其余部队距离恒 ≥1，原地防御。
    // current=3 与 own 2/18 相邻；候选 1/5 也都只满足 ≥1。
    const int crowd_own[3] = {2, 4, 18};
    const int crowd_cands[2] = {1, 5};
    Check(HexCoordDistance(3, 2) == 1 && HexCoordDistance(3, 18) == 1,
        "setup: crowded adjacency");
    Check(HexCoordDistance(1, 2) == 1 && HexCoordDistance(5, 4) == 1,
        "setup: candidates also adjacent");
    Check(ChooseSummonMoveHex(AA_SCATTER, crowd_cands, 2, 3, crowd_own, 3, 0)
        == -1,
        "no tier two achievable defends in place");
    // 没有任何其它己方部队：任意格都满足 ≥2，当前即最优，原地。
    Check(ChooseSummonMoveHex(AA_SCATTER, crowd_cands, 2, 3, own1, 0, 0) == -1,
        "no other stacks stays put");

    // 随机移动：排除当前格，均匀取一；无候选 → 原地。
    const int rnd_cands[3] = {10, 20, 30};
    Check(ChooseSummonMoveHex(AA_RANDOM_MOVE, rnd_cands, 3, 30, own1, 1, 0) == 10,
        "random move uses injected rng");
    Check(ChooseSummonMoveHex(AA_RANDOM_MOVE, rnd_cands, 3, 30, own1, 1, 1) == 20,
        "random move uses next bucket");
    Check(ChooseSummonMoveHex(AA_RANDOM_MOVE, rnd_cands, 1, 10, own1, 1, 0) == -1,
        "random without other candidates stays");
    Check(ChooseSummonMoveHex(AA_MANUAL, rnd_cands, 3, 30, own1, 1, 0) == -1,
        "manual never moves");
}

void TestSummonRuleNormalization()
{
    // 普通部队行动集不包含散开/随机移动（回归）。
    AutoActionKind actions[AA_COUNT] = {};
    const int n = GetAllowedActions(1, false, false, false, actions);
    bool has_summon_action = false;
    for (int i = 0; i < n; ++i)
        if (actions[i] == AA_SCATTER || actions[i] == AA_RANDOM_MOVE)
            has_summon_action = true;
    Check(!has_summon_action, "ordinary stack cannot pick summon actions");
    Check(ActionShowsFallback(1, AA_RANDOM_MOVE),
        "random move shows fallback checkbox");
    Check(!ActionShowsFallback(1, AA_SCATTER),
        "scatter hides fallback checkbox");
    Check(!ActionNeedsTarget(AA_SCATTER) && !ActionNeedsTarget(AA_RANDOM_MOVE),
        "summon actions need no target rule");

    // 召唤行动集与规范化。
    AutoActionKind summon_actions[4] = {};
    const int m = GetAllowedSummonActions(summon_actions);
    Check(m == 4 && summon_actions[0] == AA_MANUAL
        && summon_actions[1] == AA_DEFEND && summon_actions[2] == AA_SCATTER
        && summon_actions[3] == AA_RANDOM_MOVE,
        "summon action set is manual/defend/scatter/random");

    AutoStackRule rule = MakeDefaultRule();
    rule.action = AA_SCATTER;
    rule.allowDefendFallback = true;
    rule.protectMode = PM_LOSS_GT_RESTORE;
    rule.quickCastFirst = true;
    rule.spellSlotCount = 2;
    rule.spellSlots[0] = 1;
    rule.spellSlots[1] = 2;
    NormalizeSummonRule(&rule);
    Check(rule.action == AA_SCATTER, "scatter survives normalization");
    Check(!rule.allowDefendFallback, "scatter loses fallback");
    Check(rule.protectMode == PM_NONE, "summon rule resets protect mode");
    Check(!rule.quickCastFirst && rule.spellSlotCount == 0,
        "summon rule has no quick cast");

    rule = MakeDefaultRule();
    rule.action = AA_RANDOM_MOVE;
    rule.allowDefendFallback = true;
    NormalizeSummonRule(&rule);
    Check(rule.action == AA_RANDOM_MOVE && rule.allowDefendFallback,
        "random move keeps fallback");

    rule = MakeDefaultRule();
    rule.action = AA_MELEE_ATTACK;
    NormalizeSummonRule(&rule);
    Check(rule.action == AA_MANUAL, "non-summon action snaps to manual");

    const SummonProfileFields def = MakeDefaultSummonFields();
    Check(def.count_th == 2 && def.hp_th == 750 && def.spell_pick == 0
        && def.stop_enemy_mana == 1 && def.stop_mana_th == 6
        && def.summon_rule.action == AA_DEFEND,
        "default summon fields");

    // 普通规则 NormalizeRule 仍会把召唤动作裁掉（双保险）。
    AutoStackRule leak = MakeDefaultRule();
    leak.action = AA_SCATTER;
    NormalizeRule(&leak, 1, false, false, false);
    Check(leak.action == AA_MANUAL, "normalize rule strips summon action");
}

} // namespace

namespace {

void TestCrashGuardCore()
{
    using namespace H3AutoGuard;

    // 噪音过滤：C++ throw / OutputDebugString / 线程命名 / 单步不进历史环。
    Check(IsNoiseExceptionCode(0xE06D7363), "cpp throw is noise");
    Check(IsNoiseExceptionCode(0x40010006), "OutputDebugStringA is noise");
    Check(IsNoiseExceptionCode(0x4001000A), "OutputDebugStringW is noise");
    Check(IsNoiseExceptionCode(0x406D1388), "thread name is noise");
    Check(IsNoiseExceptionCode(0x80000004), "single step is noise");
    Check(!IsNoiseExceptionCode(0xC0000005), "access violation is not noise");
    Check(!IsNoiseExceptionCode(0xC00000FD), "stack overflow is not noise");
    Check(!IsNoiseExceptionCode(0x80000003), "breakpoint is not noise");

    // 异常码命名。
    Check(ExceptionCodeName(0xC0000005) != nullptr
        && std::string(ExceptionCodeName(0xC0000005)) == "访问冲突",
        "av named");
    Check(std::string(ExceptionCodeName(0xC0000374)) == "堆损坏",
        "heap corruption named");
    Check(ExceptionCodeName(0x12345678) == nullptr, "unknown code unnamed");

    // 访问冲突操作类型。
    Check(std::string(AVOperationName(0)) == "读取", "av read");
    Check(std::string(AVOperationName(1)) == "写入", "av write");
    Check(std::string(AVOperationName(8)) == "执行(DEP)", "av dep");
    Check(std::string(AVOperationName(99)) == "访问", "av unknown op");

    // 历史环：覆盖式回绕，Get 按 最旧→最新。
    Ring ring;
    for (int i = 0; i < 12; ++i) {
        RingRecord r = {};
        r.code = 0xC0000005;
        r.addr = 0x1000 + static_cast<unsigned long long>(i);
        ring.Push(r);
    }
    Check(ring.Count() == kRingCap, "ring capped at capacity");
    RingRecord out = {};
    Check(ring.Get(0, &out) && out.addr == 0x1004,
        "ring oldest is 5th pushed (12-8)");
    Check(ring.Get(kRingCap - 1, &out) && out.addr == 0x100B,
        "ring newest is last pushed");
    Check(!ring.Get(kRingCap, &out), "ring rejects out of range");

    // 版本门卫指纹：实测 SoD 力场表特征值。
    Check(ForceFieldTableLooksLikeSod(2, 0, -16, 3, 0, -16, -34,
            "C15spE1.def", "C15spE10.def"),
        "sod force field table matches");
    Check(!ForceFieldTableLooksLikeSod(3, 0, -16, 3, 0, -16, -34,
            "C15spE1.def", "C15spE10.def"),
        "wrong basic count rejected");
    Check(!ForceFieldTableLooksLikeSod(2, 0, -16, 3, 0, -16, -33,
            "C15spE1.def", "C15spE10.def"),
        "wrong cell offset rejected");
    Check(!ForceFieldTableLooksLikeSod(2, 0, -16, 3, 0, -16, -34,
            "C09sxxxx.def", "C15spE10.def"),
        "wrong def prefix rejected");
    Check(!ForceFieldTableLooksLikeSod(2, 0, -16, 3, 0, -16, -34,
            nullptr, "C15spE10.def"),
        "null def rejected");
}

} // namespace

int main()
{
    TestPanelItemOwnership();
    TestPanelAdmission();
    TestBattleFingerprint();
    TestForceFieldFields();
    TestForceFieldSelection();
    TestBattleStoreRecord();
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
    TestSummonChannel();
    TestSummonMoveHex();
    TestSummonRuleNormalization();
    TestProfileStoreRoundtrip();
    TestCrashGuardCore();
    std::cout << "PolicyCoreTests: " << g_checks << " checks passed\n";
    return 0;
}
