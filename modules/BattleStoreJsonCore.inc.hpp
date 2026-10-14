#pragma once

// Production JSON codec shared by the plugin and standalone tests.
#include "PolicyCore.hpp"
#include <cstring>
#include "../third_party/nlohmann/json.hpp"

using BattleJson = nlohmann::json;

static int JsonInt_(const BattleJson& obj, const char* key, int fallback)
{
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_number_integer())
        return fallback;
    return obj[key].get<int>();
}

static bool ParseRuleJson_(const BattleJson& obj, AutoStackRule* rule)
{
    if (!obj.is_object() || !rule || obj.contains("spells")) return false;
    AutoStackRule r = MakeDefaultRule();
    const int action = JsonInt_(obj, "action", -1);
    if (action < AA_MANUAL || action >= AA_COUNT) return false;
    r.action = static_cast<AutoActionKind>(action);
    const int kind = JsonInt_(obj, "kind", (int)r.target.kind);
    if (kind >= AT_NONE && kind < AT_COUNT)
        r.target.kind = static_cast<AutoTargetKind>(kind);
    const int side = JsonInt_(obj, "side", (int)r.target.side);
    if (side >= ATS_OWN && side < ATS_COUNT)
        r.target.side = static_cast<AutoTargetSide>(side);
    const int selector = JsonInt_(obj, "selector", (int)r.target.selector);
    if (selector >= SEL_RANDOM && selector < SEL_COUNT)
        r.target.selector = static_cast<AutoTargetSelector>(selector);
    if (obj.contains("waypoints") && obj["waypoints"].is_array()) {
        int count = 0;
        for (const BattleJson& item : obj["waypoints"]) {
            if (!item.is_number_integer() || count >= MOVE_WAYPOINT_CAPACITY)
                break;
            r.target.moveWaypoints[count++] =
                static_cast<int16_t>(item.get<int>());
        }
        r.target.moveWaypointCount = static_cast<int8_t>(count);
    }
    if (obj.contains("melee") && obj["melee"].is_array()) {
        int count = 0;
        for (const BattleJson& pair : obj["melee"]) {
            if (!pair.is_array() || pair.size() < 2 || count >= MELEE_PAIR_CAPACITY)
                break;
            if (!pair[0].is_number_integer() || !pair[1].is_number_integer())
                break;
            r.target.meleeStandHexes[count] =
                static_cast<int16_t>(pair[0].get<int>());
            r.target.meleeAttackHexes[count] =
                static_cast<int16_t>(pair[1].get<int>());
            ++count;
        }
        r.target.meleePairCount = static_cast<int8_t>(count);
        if (count > 0) {
            r.target.meleeStandHex = r.target.meleeStandHexes[0];
            r.target.meleeAttackHex = r.target.meleeAttackHexes[0];
        }
    }
    r.allowDefendFallback = JsonInt_(obj, "fallback", 0) != 0;
    const int wait_mode = JsonInt_(obj, "waitMode", (int)AW_NONE);
    r.waitMode = (wait_mode >= 0 && wait_mode < (int)AW_COUNT)
        ? static_cast<uint8_t>(wait_mode) : (uint8_t)AW_NONE;
    const int mode = JsonInt_(obj, "protectMode", (int)PM_NONE);
    r.protectMode = (mode == (int)PM_COUNT_BELOW
            || mode == (int)PM_LOSS_GT_RESTORE)
        ? static_cast<ProtectMode>(mode) : PM_NONE;
    const int protect_count = JsonInt_(obj, "protectCount", r.protectCountBelow);
    r.protectCountBelow = protect_count < 0 ? 0 : protect_count;
    *rule = r;
    return true;
}

static BattleJson RuleToJson_(const AutoStackRule& rule)
{
    BattleJson obj = {
        {"action", (int)rule.action},
        {"kind", (int)rule.target.kind},
        {"side", (int)rule.target.side},
        {"selector", (int)rule.target.selector},
        {"fallback", rule.allowDefendFallback ? 1 : 0},
        {"waitMode", (int)rule.waitMode},
        {"protectMode", (int)rule.protectMode},
        {"protectCount", rule.protectCountBelow},
    };
    if (rule.target.moveWaypointCount > 0) {
        BattleJson waypoints = BattleJson::array();
        for (int i = 0; i < rule.target.moveWaypointCount; ++i)
            waypoints.push_back((int)rule.target.moveWaypoints[i]);
        obj["waypoints"] = waypoints;
    }
    if (rule.target.meleePairCount > 0) {
        BattleJson pairs = BattleJson::array();
        for (int i = 0; i < rule.target.meleePairCount; ++i)
            pairs.push_back(BattleJson::array({
                (int)rule.target.meleeStandHexes[i],
                (int)rule.target.meleeAttackHexes[i]}));
        obj["melee"] = pairs;
    }
    return obj;
}

// 攻击配置只接受有界整数，避免 unsigned/64-bit 数值窄化后伪装成合法值。
static bool ParseAttackMagicInt_(const BattleJson& value, int lo, int hi, int* out)
{
    if (!out || !value.is_number_integer()) return false;
    if (value.is_number_unsigned()) {
        const unsigned long long n = value.get<unsigned long long>();
        if (n > static_cast<unsigned long long>(hi)) return false;
        const int v = static_cast<int>(n);
        if (v < lo) return false;
        *out = v;
    } else {
        const long long n = value.get<long long>();
        if (n < lo || n > hi) return false;
        *out = static_cast<int>(n);
    }
    return true;
}

static bool ParseStatusJson_(const BattleJson& obj, StatusProfileFields* status)
{
    if (!status || !obj.is_object() || !obj.contains("attacks")
        || !obj["attacks"].is_array()
        || obj["attacks"].size() > kAttackMagicCapacity)
        return false;
    StatusProfileFields fields = MakeDefaultStatusFields();
    for (const BattleJson& attack : obj["attacks"]) {
        if (!attack.is_object() || attack.size() != 3
            || !attack.contains("spell") || !attack.contains("target")
            || !attack.contains("positions") || !attack["positions"].is_array()
            || attack["positions"].size() > kAttackMagicPositionCapacity)
            return false;
        AttackMagicRule rule = MakeDefaultAttackMagicRule();
        // target：-1=全体无目标控件、0=远程飞兵速度优先、1=随机、
        // 2=固定落点（范围法术唯一落点）；越界组合由 NormalizeAttackMagicRule 按法术类别归一。
        if (!ParseAttackMagicInt_(attack["spell"], -1, 80, &rule.spell_id)
            || (rule.spell_id != -1 && !IsAttackMagicSpellId(rule.spell_id))
            || !ParseAttackMagicInt_(attack["target"], -1, 2, &rule.target_mode))
            return false;
        for (const BattleJson& position : attack["positions"]) {
            int hex = -1;
            if (!ParseAttackMagicInt_(position, 0, 186, &hex)) return false;
            rule.positions[rule.position_count++] = hex;
        }
        fields.attacks[fields.attack_count++] = rule;
    }
    fields.slow = JsonInt_(obj, "slow", 0) != 0;
    fields.refresh_turns = JsonInt_(obj, "refreshTurns", H3AutoPolicy::kStatusRefreshTurns);
    if (obj.contains("buffs") && obj["buffs"].is_array()) {
        for (const BattleJson& item : obj["buffs"]) {
            if (!item.is_number_integer() || fields.slot_count >= kStatusSlotCapacity) break;
            const int spell = item.get<int>();
            if (spell <= 0 || spell >= 81) continue;
            fields.slots[fields.slot_count++] = spell;
        }
    }
    NormalizeStatusFields(&fields);
    *status = fields;
    return true;
}

static BattleJson StatusToJson_(const StatusProfileFields& status)
{
    StatusProfileFields fields = status;
    NormalizeStatusFields(&fields);
    BattleJson buffs = BattleJson::array();
    for (int i = 0; i < fields.slot_count; ++i) buffs.push_back(fields.slots[i]);
    BattleJson attacks = BattleJson::array();
    for (int i = 0; i < fields.attack_count; ++i) {
        const AttackMagicRule& rule = fields.attacks[i];
        BattleJson positions = BattleJson::array();
        for (int j = 0; j < rule.position_count; ++j) positions.push_back(rule.positions[j]);
        attacks.push_back({{"spell", rule.spell_id}, {"target", rule.target_mode},
            {"positions", positions}});
    }
    return {{"buffs", buffs}, {"slow", fields.slow},
        {"refreshTurns", fields.refresh_turns}, {"attacks", attacks}};
}

static bool ParseForceFieldJson_(const BattleJson& obj,
    ForceFieldProfileFields* forcefield)
{
    if (!forcefield || !obj.is_object() || obj.size() != 1
        || !obj.contains("anchor_hex") || !obj["anchor_hex"].is_array()
        || obj["anchor_hex"].size() != 2)
        return false;
    const BattleJson& anchors = obj["anchor_hex"];
    ForceFieldProfileFields fields = MakeDefaultForceFieldFields();
    for (int i = 0; i < 2; ++i) {
        if (!anchors[i].is_number_integer()) return false;
        if (anchors[i].is_number_unsigned()) {
            const unsigned long long value = anchors[i].get<unsigned long long>();
            fields.anchor_hex[i] = value <= 185 ? static_cast<int>(value) : -1;
        } else {
            const long long value = anchors[i].get<long long>();
            fields.anchor_hex[i] = value >= 1 && value <= 185
                ? static_cast<int>(value) : -1;
        }
    }
    NormalizeForceFieldFields(&fields);
    *forcefield = fields;
    return true;
}

static BattleJson ForceFieldToJson_(const ForceFieldProfileFields& forcefield)
{
    ForceFieldProfileFields fields = forcefield;
    NormalizeForceFieldFields(&fields);
    return {{"anchor_hex", BattleJson::array({fields.anchor_hex[0], fields.anchor_hex[1]})}};
}

static bool ParseProfileJson_(const BattleJson& obj, AutoStackRule rules[21],
    uint16_t* stop_turns, SummonProfileFields* summon,
    StatusProfileFields* status, ForceFieldProfileFields* forcefield)
{
    if (!obj.is_object() || !rules || !stop_turns || !summon || !status || !forcefield)
        return false;
    if (!obj.contains("forcefield")
        || !ParseForceFieldJson_(obj["forcefield"], forcefield))
        return false;
    const int turns = JsonInt_(obj, "stopTurns", -1);
    if (turns < 0 || turns > 999) return false;
    *stop_turns = static_cast<uint16_t>(turns);
    if (!obj.contains("summon") || !obj["summon"].is_object()) return false;
    const BattleJson& s = obj["summon"];
    *summon = MakeDefaultSummonFields();
    const int enabled = JsonInt_(s, "enabled", -1);
    const int count = JsonInt_(s, "count", -1);
    const int hp = JsonInt_(s, "hp", -1);
    const int spell = JsonInt_(s, "spell", -1);
    const int combine = JsonInt_(s, "combine", -1);
    const int stop_mana = JsonInt_(s, "stopEnemyMana", -1);
    const int mana = JsonInt_(s, "mana", -1);
    if (enabled < 0 || enabled > 1 || count < 2 || count > 21 || hp < 0
        || spell < 0 || spell > SUMMON_ELEMENT_COUNT
        || (combine != SUMMON_COMBINE_AND && combine != SUMMON_COMBINE_OR)
        || stop_mana < 0 || stop_mana > 1 || mana < 0 || mana > 32767)
        return false;
    summon->enabled = enabled;
    summon->count_th = count;
    summon->hp_th = hp;
    summon->spell_pick = spell;
    summon->cond_combine = combine;
    summon->stop_enemy_mana = stop_mana;
    summon->stop_mana_th = mana;
    if (!s.contains("rule") || !ParseRuleJson_(s["rule"], &summon->summon_rule))
        return false;
    NormalizeSummonRule(&summon->summon_rule);
    if (!obj.contains("status")
        || !ParseStatusJson_(obj["status"], status))
        return false;
    for (int i = 0; i < 21; ++i) rules[i] = MakeDefaultRule();
    if (!obj.contains("army") || !obj["army"].is_array()) return false;
    for (const BattleJson& unit : obj["army"]) {
        if (!unit.is_object()) return false;
        const int slot = JsonInt_(unit, "slot", -1);
        if (slot < 0 || slot >= 21 || !unit.contains("rule")
            || !ParseRuleJson_(unit["rule"], &rules[slot]))
            return false;
    }
    return true;
}

static BattleJson ProfileToJson_(const AutoStackRule rules[21],
    uint16_t stop_turns, const SummonProfileFields& summon,
    const StatusProfileFields& status, const ForceFieldProfileFields& forcefield)
{
    BattleJson army = BattleJson::array();
    for (int slot = 0; slot < 21; ++slot) {
        const AutoStackRule& rule = rules[slot];
        if (rule.action == AA_MANUAL
            && rule.waitMode == AW_NONE
            && rule.protectMode == PM_NONE
            && rule.target.moveWaypointCount == 0
            && rule.target.meleePairCount == 0)
            continue;
        army.push_back({
            {"slot", slot},
            {"rule", RuleToJson_(rule)},
        });
    }
    return {
        {"stopTurns", (int)stop_turns},
        {"summon", {
            {"enabled", summon.enabled ? 1 : 0},
            {"count", summon.count_th},
            {"hp", summon.hp_th},
            {"spell", summon.spell_pick},
            {"combine", summon.cond_combine},
            {"stopEnemyMana", summon.stop_enemy_mana ? 1 : 0},
            {"mana", summon.stop_mana_th},
            {"rule", RuleToJson_(summon.summon_rule)},
        }},
        {"army", army},
        {"status", StatusToJson_(status)},
        {"forcefield", ForceFieldToJson_(forcefield)},
    };
}

static bool ParseRecordJson_(const BattleJson& obj, BattleStoreRecord* rec)
{
    if (!obj.is_object() || !rec) return false;
    if (!obj.contains("time") || !obj["time"].is_string()) return false;
    const std::string time = obj["time"].get<std::string>();
    if (time.size() >= sizeof(rec->time)) return false;
    memcpy(rec->time, time.c_str(), time.size() + 1);
    const int active = JsonInt_(obj, "active", 0) - 1;
    if (active < 0 || active > 4) return false;
    rec->active = active;
    if (!obj.contains("profiles") || !obj["profiles"].is_array()
        || obj["profiles"].size() != 5) return false;
    for (int i = 0; i < 5; ++i) {
        if (!ParseProfileJson_(obj["profiles"][i], rec->rules[i],
                &rec->stop_turns[i], &rec->summon[i], &rec->status[i],
                &rec->forcefield[i]))
            return false;
    }
    return true;
}

static bool IsBattleStoreJsonVersion_(const BattleJson& root)
{
    return root.is_object() && root.contains("version")
        && root["version"].is_number_integer() && root["version"] == 6;
}

