// Included inside PolicyCoreTests.cpp's test namespace, after Check is defined.
// Every parse/serialize call below uses the production BattleStoreJsonCore codec.

static BattleJson MagicJsonStatus_(const BattleJson& attack)
{
    return {{"buffs", BattleJson::array({27, 41})}, {"slow", 1},
        {"refreshTurns", 3}, {"attacks", BattleJson::array({attack})}};
}

static BattleJson MagicJsonAttack_()
{
    return {{"spell", 20}, {"target", 2},
        {"positions", BattleJson::array({0})}};
}

static void CheckMagicJsonRejected_(const BattleJson& status, const char* name)
{
    StatusProfileFields decoded = MakeDefaultStatusFields();
    Check(!ParseStatusJson_(status, &decoded), name);
}

static void TestMagicChannelJson()
{
    // Heap records avoid a large test stack and mirror the game's bounded stack.
    BattleStoreRecord* source = new BattleStoreRecord();
    BattleStoreRecord* decoded = new BattleStoreRecord();
    std::strcpy(source->time, "20261011-003527");
    source->active = 4;
    BattleJson profiles = BattleJson::array();
    for (int p = 0; p < 5; ++p) {
        for (int slot = 0; slot < 21; ++slot)
            source->rules[p][slot] = MakeDefaultRule();
        AutoStackRule& move = source->rules[p][p];
        move.action = AA_MOVE;
        move.target.kind = AT_POSITION;
        move.target.side = ATS_OWN;
        move.target.selector = SEL_COUNT_HIGH;
        move.target.moveWaypointCount = 3;
        move.target.moveWaypoints[0] = 1 + p;
        move.target.moveWaypoints[1] = 170;
        move.target.moveWaypoints[2] = 170;
        move.allowDefendFallback = true;
        move.waitMode = AW_EVERY_TURN;
        move.protectMode = PM_COUNT_BELOW;
        move.protectCountBelow = 10 + p;
        AutoStackRule& melee = source->rules[p][20 - p];
        melee.action = AA_MELEE_ATTACK;
        melee.target.kind = AT_POSITION;
        melee.target.meleePairCount = 2;
        melee.target.meleeStandHexes[0] = 40;
        melee.target.meleeAttackHexes[0] = 41;
        melee.target.meleeStandHexes[1] = 60;
        melee.target.meleeAttackHexes[1] = 61;
        melee.target.meleeStandHex = 40;
        melee.target.meleeAttackHex = 41;
        melee.waitMode = AW_SIEGE;
        melee.protectMode = PM_LOSS_GT_RESTORE;
        // 不反兵攻城（v7）：行动、等待方式与安全位置一起过真实序列化。
        AutoStackRule& siege = source->rules[p][(p + 1) % 20];
        siege.action = AA_SIEGE;
        siege.target.kind = AT_STACK;
        siege.target.side = ATS_ENEMY;
        siege.target.selector = SEL_RANGED_SPEED;
        siege.target.siegeSafeHex = static_cast<int16_t>(17 + p);
        siege.waitMode = AW_SIEGE;
        source->stop_turns[p] = static_cast<uint16_t>(p * 100);
        SummonProfileFields& summon = source->summon[p];
        summon = MakeDefaultSummonFields();
        summon.enabled = p % 2;
        summon.count_th = 2 + p;
        summon.hp_th = 700 + p;
        summon.spell_pick = p;
        summon.cond_combine = p % 2;
        summon.stop_enemy_mana = p % 2;
        summon.stop_mana_th = 20 + p;
        summon.summon_rule.action = AA_DEFEND;
        NormalizeSummonRule(&summon.summon_rule);
        StatusProfileFields& status = source->status[p];
        status = MakeDefaultStatusFields();
        status.slot_count = 2;
        status.slots[0] = 27;
        status.slots[1] = 41;
        status.slow = p % 2;
        status.refresh_turns = 1 + p;
        status.attack_count = 3;
        status.attacks[0].spell_id = 20; // 范围法术：归一固定 target=2，唯一落点
        status.attacks[0].target_mode = 2;
        status.attacks[0].position_count = 1;
        status.attacks[0].positions[0] = 0;
        // Empty rows are configuration, not garbage.
        status.attacks[1] = MakeDefaultAttackMagicRule();
        status.attacks[2].spell_id = 57; // 单体：target 只允许 0/1
        status.attacks[2].target_mode = p % 2;
        status.attacks[2].position_count = 0;
        source->forcefield[p] = MakeDefaultForceFieldFields();
        source->forcefield[p].anchor_hex[0] = 20 + p;
        source->forcefield[p].anchor_hex[1] = 120 + p;
        profiles.push_back(ProfileToJson_(source->rules[p], source->stop_turns[p],
            summon, status, source->forcefield[p]));
    }
    const BattleJson record = {{"time", source->time}, {"active", source->active + 1},
        {"profiles", profiles}};
    const std::string text = record.dump();
    const BattleJson parsed = BattleJson::parse(text, nullptr, false);
    Check(!parsed.is_discarded() && ParseRecordJson_(parsed, decoded),
        "production JSON parses a five-profile serialized record");
    Check(std::strcmp(source->time, decoded->time) == 0,
        "record timestamp survives actual JSON text roundtrip");
    Check(BattleStoreRecordContentEquals(*source, *decoded),
        "all five profiles and every stored field survive production codec roundtrip");
    for (int p = 0; p < 5; ++p) {
        Check(decoded->status[p].attack_count == 3
                && decoded->status[p].attacks[1].spell_id == -1,
            "empty attack row and order survive each profile");
        Check(decoded->status[p].attacks[0].position_count == 1
                && decoded->status[p].attacks[0].positions[0] == 0,
            "area spell anchor survives each profile");
        Check(decoded->rules[p][p].action == AA_MOVE
                && decoded->rules[p][20 - p].target.meleePairCount == 2
                && decoded->forcefield[p].anchor_hex[1] == 120 + p,
            "army slot mapping and forcefield survive each profile");
        Check(decoded->rules[p][p].waitMode == AW_EVERY_TURN
                && decoded->rules[p][20 - p].waitMode == AW_SIEGE,
            "wait mode survives production codec roundtrip per slot");
        Check(decoded->rules[p][(p + 1) % 20].action == AA_SIEGE
                && decoded->rules[p][(p + 1) % 20].target.siegeSafeHex == 17 + p
                && decoded->rules[p][(p + 1) % 20].waitMode == AW_SIEGE,
            "siege action, safe hex and wait mode survive codec roundtrip");
        Check(ProfileToJson_(decoded->rules[p], decoded->stop_turns[p],
                decoded->summon[p], decoded->status[p], decoded->forcefield[p]) == profiles[p],
            "production decoded profile reserializes to the identical JSON object");
    }

    const BattleJson max_unsigned = static_cast<unsigned long long>(~0ULL);
    // -1 现在合法：全体法术无目标控件；越界与窄化仍拒绝。
    const BattleJson bad_modes[] = {-2, 3, true, 1.0, "1", max_unsigned,
        static_cast<long long>(2147483648LL), static_cast<long long>(-2147483649LL)};
    for (const BattleJson& bad : bad_modes) {
        BattleJson attack = MagicJsonAttack_();
        attack["target"] = bad;
        CheckMagicJsonRejected_(MagicJsonStatus_(attack),
            "attack target rejects range/type/unsigned/narrowing violations");
    }
    for (int spell : {13, 27}) {
        BattleJson attack = MagicJsonAttack_();
        attack["spell"] = spell;
        CheckMagicJsonRejected_(MagicJsonStatus_(attack),
            "unsupported firewall and non-attack spell excluded from attack JSON");
    }
    for (int spell : {20, 24, 25, 26, 57}) {
        BattleJson attack = MagicJsonAttack_();
        attack["spell"] = spell;
        StatusProfileFields status = MakeDefaultStatusFields();
        Check(ParseStatusJson_(MagicJsonStatus_(attack), &status)
                && status.attacks[0].spell_id == spell,
            "allowed area, field-wide or targeted attack spell parses");
    }
    // 类别归一：单体 target=2 归一为 0；范围/全体 target=0 归一为 2/-1，
    // 且全体不带落点，范围保留唯一合法落点。
    {
        BattleJson single = MagicJsonAttack_();
        single["spell"] = 17; single["target"] = 2;
        StatusProfileFields status = MakeDefaultStatusFields();
        Check(ParseStatusJson_(MagicJsonStatus_(single), &status)
                && status.attacks[0].target_mode == 0
                && status.attacks[0].position_count == 0,
            "single-target target mode normalizes to 0 without anchor");
    }
    {
        BattleJson area = MagicJsonAttack_();
        area["spell"] = 22; area["target"] = 1;
        StatusProfileFields status = MakeDefaultStatusFields();
        Check(ParseStatusJson_(MagicJsonStatus_(area), &status)
                && status.attacks[0].target_mode == 2
                && status.attacks[0].position_count == 1
                && status.attacks[0].positions[0] == 0,
            "area spell keeps its single anchor and fixed anchor mode");
    }
    {
        BattleJson field = MagicJsonAttack_();
        field["spell"] = 24; field["target"] = 1;
        StatusProfileFields status = MakeDefaultStatusFields();
        Check(ParseStatusJson_(MagicJsonStatus_(field), &status)
                && status.attacks[0].target_mode == -1
                && status.attacks[0].position_count == 0,
            "field-wide spell drops target mode and anchor");
    }
    for (const BattleJson& bad : {BattleJson(-1), BattleJson(187), max_unsigned,
            BattleJson(170.0), BattleJson(true), BattleJson("170"),
            BattleJson(static_cast<long long>(2147483648LL))}) {
        BattleJson attack = MagicJsonAttack_();
        attack["positions"] = BattleJson::array({bad});
        CheckMagicJsonRejected_(MagicJsonStatus_(attack),
            "attack position rejects invalid range and non-integer/narrowing values");
    }
    for (const BattleJson& bad : {BattleJson(true), BattleJson(20.0), BattleJson("20"),
            max_unsigned, BattleJson(static_cast<long long>(2147483648LL))}) {
        BattleJson attack = MagicJsonAttack_();
        attack["spell"] = bad;
        CheckMagicJsonRejected_(MagicJsonStatus_(attack),
            "attack spell rejects non-integer and narrowing values");
    }

    BattleJson one_position = BattleJson::array({0});
    BattleJson attack = MagicJsonAttack_();
    attack["positions"] = one_position;
    StatusProfileFields status = MakeDefaultStatusFields();
    Check(ParseStatusJson_(MagicJsonStatus_(attack), &status)
            && status.attacks[0].position_count == 1,
        "exactly one attack anchor accepted");
    attack["positions"].push_back(10);
    CheckMagicJsonRejected_(MagicJsonStatus_(attack), "second attack anchor rejected");
    BattleJson full = MagicJsonStatus_(MagicJsonAttack_());
    full["attacks"] = BattleJson::array();
    for (int i = 0; i < 8; ++i)
        full["attacks"].push_back({{"spell", -1}, {"target", i % 2},
            {"positions", BattleJson::array()}});
    Check(ParseStatusJson_(full, &status) && status.attack_count == 8,
        "exactly eight empty attack rows accepted");
    full["attacks"].push_back(MagicJsonAttack_());
    CheckMagicJsonRejected_(full, "ninth attack row rejected");
    BattleJson legacy = MagicJsonStatus_(MagicJsonAttack_());
    legacy.erase("attacks");
    CheckMagicJsonRejected_(legacy, "old status missing mandatory attacks rejected");
    attack = MagicJsonAttack_();
    attack["extra"] = 1;
    CheckMagicJsonRejected_(MagicJsonStatus_(attack), "extra attack key rejected");
    for (const char* missing : {"spell", "target", "positions"}) {
        attack = MagicJsonAttack_();
        attack.erase(missing);
        CheckMagicJsonRejected_(MagicJsonStatus_(attack), "missing attack field rejected");
    }
    BattleJson rule = RuleToJson_(MakeDefaultRule());
    rule["spells"] = BattleJson::array({1});
    AutoStackRule unit = MakeDefaultRule();
    Check(!ParseRuleJson_(rule, &unit), "removed per-army spells field rejected");
    // 等待方式：缺键=无（strict JSON 里旧对象也能读回默认），越界值钳到无。
    rule = RuleToJson_(MakeDefaultRule());
    Check(rule.contains("waitMode") && rule["waitMode"] == 0,
        "default rule serializes wait mode none");
    rule.erase("waitMode");
    unit = MakeDefaultRule();
    unit.waitMode = AW_EVERY_TURN;
    Check(ParseRuleJson_(rule, &unit) && unit.waitMode == AW_NONE,
        "missing wait mode falls back to none");
    for (int bad_wait : {-1, (int)AW_COUNT, 99}) {
        rule = RuleToJson_(MakeDefaultRule());
        rule["waitMode"] = bad_wait;
        unit = MakeDefaultRule();
        unit.waitMode = AW_SIEGE;
        Check(ParseRuleJson_(rule, &unit) && unit.waitMode == AW_NONE,
            "out-of-range wait mode clamps to none");
    }
    rule = RuleToJson_(MakeDefaultRule());
    rule["waitMode"] = AW_SIEGE;
    unit = MakeDefaultRule();
    Check(ParseRuleJson_(rule, &unit) && unit.waitMode == AW_SIEGE,
        "valid wait mode parses");
    // 安全位置：合法 1..185 落位，越界忽略回默认 -1，缺失不恢复。
    rule = RuleToJson_(MakeDefaultRule());
    rule["action"] = (int)AA_SIEGE;
    rule["kind"] = (int)AT_STACK;
    rule["selector"] = (int)SEL_RANGED_SPEED;
    rule["siegeSafeHex"] = 185;
    unit = MakeDefaultRule();
    Check(ParseRuleJson_(rule, &unit) && unit.action == AA_SIEGE
            && unit.target.siegeSafeHex == 185,
        "siege safe hex parses inside battle bounds");
    rule["siegeSafeHex"] = 186;
    unit = MakeDefaultRule();
    Check(ParseRuleJson_(rule, &unit)
            && unit.target.siegeSafeHex == MakeDefaultRule().target.siegeSafeHex,
        "out-of-bounds siege safe hex ignored");
    BattleJson siege_json = RuleToJson_(decoded->rules[0][1]);
    Check(siege_json.contains("siegeSafeHex") && siege_json["siegeSafeHex"] == 17,
        "siege rule serializes its safe hex");
    BattleJson bad_record = record;
    bad_record["profiles"][0]["status"].erase("attacks");
    Check(!ParseRecordJson_(bad_record, decoded),
        "record rejects legacy status through the production profile path");
    bad_record = record;
    bad_record["profiles"][0]["army"][0]["rule"]["spells"] = BattleJson::array();
    Check(!ParseRecordJson_(bad_record, decoded),
        "record rejects removed army spells field through production profile path");

    Check(IsBattleStoreJsonVersion_(BattleJson{{"version", 7}}),
        "production root version helper accepts schema v7");
    for (const BattleJson& version : {BattleJson(4), BattleJson(5), BattleJson(6),
            BattleJson(8), BattleJson(true), BattleJson(7.0), BattleJson("7"), max_unsigned})
        Check(!IsBattleStoreJsonVersion_(BattleJson{{"version", version}}),
            "production root version helper rejects v6 and earlier plus wrong types");
    Check(!IsBattleStoreJsonVersion_(BattleJson::object()), "missing root version rejected");
    Check(!IsBattleStoreJsonVersion_(BattleJson::array()), "nonobject root rejected");
    delete decoded;
    delete source;
}
