// ========== 兼容性层：战斗结构体占位 ==========
// 参考 H3BattleValueInfo 的 Compat.inc.cpp + HoMM3 反编译结果
// TODO: Hook 地址实测验证后补充字段偏移

using _bool8_  = char;

#define CALL_1(return_type, calling_convention, address, arg1) THISCALL_1(return_type, address, arg1)
#define CALL_2(return_type, calling_convention, address, arg1, arg2) THISCALL_2(return_type, address, arg1, arg2)
#define CALL_3(return_type, calling_convention, address, arg1, arg2, arg3) THISCALL_3(return_type, address, arg1, arg2, arg3)

// ===== 战斗相关结构体 =====

// 远程/近战判定（从 CreatureInfo 中取 is_ranged）
struct _CreatureInfoCompat {
    char _pad0[0x4C];
    int hit_points;     // 0x4C
    int speed;          // 0x50
    int attack;         // 0x54
    int defence;        // 0x58
    int damage_min;     // 0x5C
    int damage_max;     // 0x60
    int shots;          // 0x64
};

struct _BattleStack_ {
    char _pad0[0x34];
    int  creature_id;
    int  hex_ix;
    int  def_group_ix;      // 阵营：0=下方（玩家），1=上方（敌方）
    int  def_frame_ix;
    char _pad44[0x4C - 0x44];
    int  count_current;     // 当前数量
    int  count_before_attack;
    char _pad54[0x58 - 0x54];
    int  lost_hp;
    int  army_slot_ix;      // 在己方 army[21] 中的槽位索引
    int  count_at_start;    // 战斗开始时数量
    char _pad64[0x74 - 0x64];
    _CreatureInfoCompat creature;
    char _padDC[0x194 - 0xDC];
    int  active_spell_count;
    int  active_spell_duration[81];
    int  active_spell_level[81];
    char _pad420[0x454 - 0x420];
    int  retaliations;
    int  bless_damage;
    int  curse_damage;
    int  anti_magic;
    int  bloodlust_effect;
    int  precision_effect;
    int  weakness_effect;
    int  stone_skin_effect;
    int  _unknown_474;
    int  prayer_effect;
    int  mirth_effect;
    int  sorrow_effect;
    int  fortune_effect;
    int  misfortune_effect;
    int  slayer_type;
    int  hexes_traveled;
    int  counterstrike_effect;
    float frenzy_multiplier;
    float blind_effect;
    float fire_shield_effect;
    int  _unknown_4A4;
    float protection_air_effect;
    float protection_fire_effect;
    float protection_water_effect;
    float protection_earth_effect;
    float shield_effect;
    float air_shield_effect;
    unsigned char blinded;
    unsigned char paralyzed;
    char _pad4C2[0x4C4 - 0x4C2];
    int  forgetfulness_level;
    float slow_effect;
    int  haste_effect;
    int  disease_attack_effect;
    int  disease_defense_effect;
    char _pad4D8[0x4E8 - 0x4D8];
    int  morale;
    int  luck;
    unsigned char is_done;
    char _pad4F1[0x548 - 0x4F1];
    bool CanShoot(_BattleStack_* target) { return THISCALL_2(bool, 0x442610, this, target); }
    int Calc_Damage_Bonuses(_BattleStack_* target, int base_damage, int a4, int a5, int a6, int* fireshield_damage) { return THISCALL_7(int, 0x443C60, this, target, base_damage, a4, a5, a6, fireshield_damage); }
};

typedef char _BattleStack_size_check[(sizeof(_BattleStack_) == 0x548) ? 1 : -1];

// H3Artifact 原版是 8 字节：{INT32 id; INT32 mod}
struct _Artifact_ { int id; int mod; };

struct _Hero_ {
    char _pad0[0x18];
    short spell_points;
    int   id;
    int   id_wtf;
    char _pad22[0x55 - 0x22];
    short level;
    char _pad57[0xC9 - 0x57];
    unsigned char second_skill[28];
    char _padE5[0x12D - 0xE5];
    _Artifact_ doll_art[19];
    char _pad1C5[0x3EA - 0x1C5];
    unsigned char spell[70];
    unsigned char spell_level[70];
    char _pad478[0x47A - 0x478];
    unsigned char power;
    unsigned char knowledge;
    int GetLandModifierUnder() { return THISCALL_1(int, 0x4E5210, this); }
    bool DoesWearArtifact(int art_id) { return THISCALL_2(bool, 0x4E2C90, this, art_id); }
    int GetSpell_Specialisation_Bonuses(int spell_id, int skill_level, int damage) { return THISCALL_4(int, 0x4E6260, this, spell_id, skill_level, damage); }
};

struct _BattleMgr_ {
    char _pad0[0x53C0];
    int  spec_terr_type;
    char _pad53C4[0x53CC - 0x53C4];
    _Hero_* hero[2];           // [0]=下方玩家, [1]=上方敌方
    char _pad53D4[0x54B4 - 0x53D4];
    int  hero_casted[2];
    int  stacks_count[2];
    char _pad54C4[0x54CC - 0x54C4];
    _BattleStack_ stack[2][21];
    char _pad1329C[0x132A0 - 0x1329C];
    int  turns_since_last_enchanter_cast[2];
    char _pad132A8[0x132B8 - 0x132A8];
    int  current_mon_side;
    int  current_mon_index;
    int  current_active_side;  // 0=下方, 1=上方
    int  auto_combat;          // 非0=AI自动战斗模式
    _BattleStack_* active_stack;
    char _pad132CC[0x132D8 - 0x132CC];
    int  attacker_coord;
    int  move_type;
    char _pad132E0[0x132F8 - 0x132E0];
    int  finished;
    void* dlg;                 // 战斗对话框指针
};

// 全局对象指针
#define o_BattleMgr (*reinterpret_cast<_BattleMgr_**>(0x699420))
