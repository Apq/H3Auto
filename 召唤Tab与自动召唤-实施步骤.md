# 召唤 Tab 与自动召唤 — 实施步骤

## 0. 需求与用户决策

需求：设置面板新增「召唤」Tab 页，配置两件事：

1. **什么情况下自动召唤**：自动施放元素召唤法术的时机与条件。
2. **召唤的部队如何自动行动**：召唤物上场后的行动规则。

用户决策（2026-09-28）：

- **保活与召唤整合（后续补充决策）**：召唤策略本质与保活相同——都是「把本回合法术花在维持兵力上」，只是手段不同（复活/聚灵恢复既有部队 vs 元素召唤补充新部队）。整合为**单一策略体系**：现有「保活策略」下拉扩**第 5 项「兵力不足时召唤」**（单选，一个方案一种手段；想复活与召唤混用就配两个方案切换，不做组合模式）。队数/血量两个阈值框放**召唤页设置行**（保活行宽度装不下策略下拉 + 两组框 + 停止条件组；召唤页本就是召唤通道参数的家）。
- **游戏规则前提**：每场战斗只能召唤**一种**元素，可以多队；因此召唤物行动策略**共用一个设置**（不按元素、不按批次分开）。
- **召唤条件**（保活策略 = 兵力不足时召唤，两条**同时满足**才施放）：① 己方**尚存活部队数 ≤ 队数阈值**（默认 2；**战争机器不算，召唤物算**）；② 己方**剩余血量合计 ≤ 血量阈值**（默认 750）。血量口径与自动停止（§3.1.2）一致：己方每个存活槽 `存活数 × 单兵满血 − 顶层已损` 求和，同样**含召唤物、不含战争机器**（箭塔/弩车/急救帐篷/投石车/弹药车）。行为是**按损失补位**：兵力掉进阈值内召一队，召出后队数 +1、条件自然关闭，再损再触发——不是每回合囤兵。两个阈值拉满（队数 21、血量 2147483647）等价「每回合必召」。
- **法术选择**（召唤页）：下拉 = **自动**（已学法术中单次召唤总量最大者；本场第一次成功施放后锁定该元素）+ **四元素固定**（气/水/火/土；未学则不召）。
- **召唤物行动集（专用，不套用普通部队）**：手动 / 防御 / **散开** / **随机移动** +「允许降级为防御」复选框（**仅行动 = 随机移动时显示并生效**）。**散开 = 分级拉开距离**：先找「与己方其它所有存活部队（含战争机器）距离均 ≥ 2 格」的格（当前格已满足则不移动）；全场做不到降为「均 ≥ 1 格」；再做不到则原地防御。距离为战场六格距离。无选择器、无路径/组合槽、无快捷施法与保活。
- **自动停止的第二条件（战斗级，后续补充决策）**：「敌方法力耗尽前停手」从召唤物行动的结束条件**升级为与停止回合数同级的自动停止条件**：勾选后，敌方英雄**存在且有魔法书**且剩余法力 ≤ 6（约「最多再放一次」低阶法术的量）→ 自动战斗切回手动，全场自动化（复活/召唤/自动动作/停止监控）一并停止。复选框在保活行「停止:」框右侧，方案级、默认不勾；与停止回合数条件 **OR 组合，任一触发即停**；每次控制权交还玩家时判；敌方无英雄或无魔法书不触发（无书英雄法力常为 0，不应误触发）。阈值先取常量 6，要做成数字框后续再升级。
- **判定时机**：`TryProtectCast_` 按策略分派——策略 1-4 走复活通道（现状不变），策略 5 走召唤通道（**事件型**，每次控制权交玩家都判，施放后 `hero_casted` 自然挡住本回合后续，无需回合标记）。`HH_ShouldAutoExecute` 顺序保持 **TryProtectCast_ → TryAutoStop_ → 循环快捷施法**，不新增插桩点。
- **存档**：格式升 **H3AP6**；H3AP5 及更旧一律拒绝（沿用惯例，旧档读档失败需重新配置）。
- **部队页不再显示召唤物/克隆物卡片**：召唤物行动只由召唤页的共享规则控制，配置出口唯一；克隆物（类型不在四元素内）无规则、保持手动。

## 1. 现状（代码事实）

- **保活策略体系**：`ProtectStrategy`（`PS_NONE/PS_COUNT_BELOW/PS_FIRST_ACTION/PS_LOSS_GT_RESTORE`）方案级存于 `g_protect_strategy[5]`，草稿 `s_p.draft_protect_strategy[5]`，随 `CommitProfiles` 提交；`TryProtectCast_`（`AutoExecute.inc.cpp` 984-1117）统一守卫（`BP_COMBAT_CLOSED`+`CM_AUTO`）、`hero_casted`、法力（复活固定 10）、候选收集 + `ProtectShouldCast` 纯判定 + `SelectProtectTargetIndex` 选目标 + `CastSpell(法术, 尸体格, 0, -1, 等级, 力量)` SEH 包裹。保活策略行为全局常显（`PROTECT_DD_Y=85`，PanelDraw 下拉、SettingsDlg 交互、tips 跟随选中项），行内右端是自动停止框。
- **Tab 机制**：`PanelLayout.hpp` 的 `kTabOrder`/`TAB_VISIBLE_COUNT` 预留扩展位（当前仅 `PAGE_ARMY`）；`PanelDraw` 画 Tab、`SettingsDlg` 点击切页、`SwitchPanelPage_` 跨页收尾，全部现成。
- **召唤物身份**：`STACK_ID_SUMMON`（类型 + 同类序号，本场内有效，重打丢弃）。当前行为：面板提交时已存在的召唤物按槽位绑定、可在部队页配置；提交**之后**新出现的召唤物不进 `g_stack_track` → `ActiveStackMatchesTrack_` 失败 → 保持玩家手动。**无任何自动施召唤入口。**
- **血量口径现成**：`StackRemainingHp`（自动停止同款），求和即可。
- **方案存档**：H3AP5 = 1304 整数（`EncodeProfileStoreText`/`DecodeProfileStoreText`，`PROFILE_STORE_INTS` 表达式自校验）。
- **相关 ID**（SoD，H3API eSpell/eCreature）：法术 火 66 / 土 67 / 水 68 / 气 69；生物 气 112 / 土 113 / 火 114 / 水 115。HotA 心灵/魔法等元素是地图常规部队（有 `source_army_slot`），不属召唤身份，不受影响。
- **顺带问题**：部队页目前放行召唤物卡片（`IsConfigurablePanelStack_`），存档部队表也会混入召唤物，干扰读档四轮关联的轮 4 同类型匹配——本次一并移除。

## 2. 设计

### 2.1 数据结构（PolicyCore / ConfigLog）

```cpp
// 保活策略扩第 5 项：召唤通道（0..4；存档策略整数值域随之扩为 0..4）
enum ProtectStrategy : uint8_t {
    PS_NONE = 0,          // 无：不保活
    PS_COUNT_BELOW,       // 按数量：剩余数量 ≤ 该队阈值才救（阈值随规则存储，默认 2；0=只救全灭）
    PS_FIRST_ACTION,      // 回合内首动：队列中有损失的部队即救
    PS_LOSS_GT_RESTORE,   // 损失量大于恢复量：已损 HP 超过一次可恢复量才救
    PS_SUMMON_LOW_FORCE,  // 兵力不足时召唤：存活队数 ≤ 阈值 且 血量合计 ≤ 阈值 → 召唤通道
    PS_COUNT
};

static constexpr int SUMMON_ELEMENT_COUNT = 4;   // 固定顺序：气/水/火/土（与法术下拉一致）
static constexpr int kSummonSpellIds[SUMMON_ELEMENT_COUNT]   = {69, 68, 66, 67};  // 气/水/火/土
static constexpr int kSummonCreatureIds[SUMMON_ELEMENT_COUNT] = {112, 115, 114, 113};
static constexpr int kSummonStopEnemyMana = 6;   // 「最多再放一次」判定阈值（常量，暂不做数字框）

// 召唤配置（方案级）：时机在 g_protect_strategy（PS_SUMMON_LOW_FORCE），其余为召唤通道参数。
uint8_t g_summon_count_th[5];    // 队数阈值，默认 2，0..21（战争机器不计、召唤物计）
int32_t g_summon_hp_th[5];       // 血量阈值，默认 750，0..2147483647（口径同上）
uint8_t g_summon_spell_pick[5];  // 法术选择：0=自动；1..SUMMON_ELEMENT_COUNT=固定元素
AutoStackRule g_summon_rule[5];  // 召唤物共享行动规则（每方案一条）

// 自动停止第二条件（方案级）：与 g_stop_turns[5] 同级、OR 组合。
uint8_t g_stop_enemy_mana[5];    // 敌方法力停止勾选（0/1，默认 0）
```

行动枚举 `AutoActionKind` 新增 `AA_SCATTER`（散开）、`AA_RANDOM_MOVE`（随机移动）：**仅召唤共享规则可选**，普通部队行动下拉不显示（`GetAllowedActions` 普通分支维持原集合）。散开不必然移动：分散度已达标（无可达格能严格改善）就原地防御。

本场运行态（`AutoExecute`）：`g_summon_locked_spell`（自动模式本场锁定的法术，重打/重置清空；事件型判定无需回合标记）。

### 2.2 UI

**保活行改造（部队页全局行，`PROTECT_DD_Y=85`）**：

```text
保活策略: [兵力不足时召唤 ▾]          停止: [10]   ☑ 敌方法力≤6 时停
```

- 下拉由 4 项扩 5 项（第 5 项「兵力不足时召唤」，i18n `panel.protect_opt4`）；展开列表、tips 跟随选中项、切方案收起等机制全部沿用。
- 行右侧为**自动停止条件组**：`停止: [数字框]`（现状）+ 新复选框「敌方法力≤6 时停」（小字号，方案级、默认不勾）；两者 OR 组合（§2.3）。宽度核算：下拉 ≈130 + 停止框 ≈66 + 勾选 ≈110 ≈ 380px < 531px，余量充足。

**召唤页（`PAGE_SUMMON`，内容区 531×342）**：

```text
设置行：法术: [自动 ▾]    队数≤ [2]    血量≤ [750]
说明行：本场只召一种元素 · 没学不召 · 行动规则全场共享
「召唤物行动」卡一张：
    行动 [散开 ▾]   （随机移动时追加显示：☐ 允许降级为防御，勾选框画在文字前）
```

- 队数/血量两个数字框随法术下拉放召唤页设置行，**恒显示**（同「剩≤」惯例），仅保活策略 = 兵力不足时召唤时参与判定；数字录入复用停止框编辑器（点击录入、点框外/Enter 写回、ESC 放弃）。

- 行动可选集（**召唤物专用，不套用普通部队**）：手动 / 防御 / **散开**（分级拉开距离：≥2 格优先，做不到降 ≥1 格，再做不到原地防御；当前格已达标则不移动）/ **随机移动**（从可达格不含当前格均匀随机）。移动复用循环移动的寻路与可达性校验；「允许降级为防御」复选框仅行动 = 随机移动时显示并生效（随机无可达格/提交失败 → 勾选则防御、不勾交回玩家；散开的降级链固定终于防御）。
- 共享规则卡**没有**选择器、路径/组合槽、快捷施法行、保活勾选、「剩≤」框——卡片只剩行动下拉 + 允许降级复选框 + 结束条件复选框。
- `kTabOrder = { PAGE_ARMY, PAGE_SUMMON }`、`TAB_VISIBLE_COUNT = 2`；文案键 `panel.tab_summon`、tip `tips.tab_summon`；法术下拉复用保活策略下拉的展开机制；切页/切方案收尾同现有（收下拉与编辑态）；召唤页无需战场拾取。
- `PanelTipAt_` 现有「非部队页」分支（原方案页遗留）改为召唤页提示表；tips 命中顺序不变。

### 2.3 执行（`AutoExecute`）

**`TryProtectCast_` 内分派**（`HH_ShouldAutoExecute` 顺序不变：`TryProtectCast_` → `TryAutoStop_` → 快捷施法管线）：

1. 共享守卫（现状）：`BP_COMBAT_CLOSED` + `CM_AUTO` + `strategy != PS_NONE` + `hero_casted` 检查 + side/hero 解析。
2. 策略分派：`PS_FIRST_ACTION` 保留回合标记；**`PS_SUMMON_LOW_FORCE` 转入召唤通道**（事件型，每次控制权交玩家都判）；其余走现有复活通道（候选收集 → `ProtectShouldCast` → 选目标 → `CastSpell` 聚灵/复活）。
3. **召唤通道**（子函数 `SummonChannel_`）：
   - 选法术：固定 → 该法术（`GetSpellExpertise <= 0` 跳过）；自动 → 本场已锁定用锁定值，否则已学法术中**单次召唤总量最大**者（召唤量 = 每点魔力 HP × 力量 × 等级系数，常量表待逆向确认，见 §5），无已学跳过。
   - 法力 ≥ 该法术消耗（消耗常量待逆向，见 §5；复活通道固定 10 的检查在分派前，召唤通道自查）。
   - 条件：己方存活部队数（不含战争机器、含召唤物）≤ `g_summon_count_th[生效方案]` **且** 剩余血量合计（§0 口径）≤ `g_summon_hp_th[生效方案]`。
   - 施放：`CastSpell(spell, hex, 0, -1, GetSpellExpertise(spell, terrain), power)`，SEH 包裹；成功后记录本场锁定。召唤法术 target hex 实参语义待上机验证（先按保活同参对照原版，见 §5）。

**召唤物运行绑定**（`UpdateStackTracking_` 扩展）：

- 刷新时发现人类侧、身份 `STACK_ID_SUMMON`、类型 ∈ 四元素、**未绑定**的槽 → 绑定跟踪条目（游标归零）+ 把 `g_summon_rule[生效方案]` 写入 `g_active_rules[槽]`；之后行动提交/降级全部走现有管线。
- 召唤物动作提交：手动 → 交回玩家；防御 → `SubmitDefend_`；**散开 → 分级求解目标格**（对档位 d ∈ {2,1} 依次：在可达格 ∪ {当前格} 中找「与己方其它所有存活部队（含战争机器）距离均 ≥ d」的候选，当前格达标则不移动、`SubmitDefend_`；否则取候选中「最小距离」最大者（平手取稳定序，便于单测）复用 `SubmitMove_` 的寻路与合法性校验（原版目标判定 + `accessibleSquares2`）移动；两档均无候选 → `SubmitDefend_` 原地防御）；随机移动 → 从可达格（不含当前格）均匀随机选一格提交，无可达格或提交失败按「允许降级为防御」勾选降级或交回玩家。距离取战场六格距离（实施时用 H3API/游戏现成函数并确认）。
- **自动停止扩展（战斗级第二停止条件）**：`TryAutoStop_` 内新增判定——勾选 `g_stop_enemy_mana[生效方案]` 且敌方英雄**存在且有魔法书**且剩余法力 ≤ `kSummonStopEnemyMana`（6）→ 与停止回合数条件 OR 组合，任一触发即按同款机制切回手动，日志 `[AutoStop] enemy mana low`；每次控制权交还玩家时判（与停止回合数同评估点）；敌方无英雄或无魔法书不触发。敌方英雄指针、法力与魔法书标志的读取路径实施时确认（§5）。
- 克隆物（`clone_id>0`，类型不在四元素）不绑定，保持手动（现状）。
- 战斗结束/重打/状态重置：清 `g_summon_locked_spell`；策略与共享规则随 5 套方案保留，天然跨重打。

**日志**：`[Summon]` request/cast/条件快照（debug），配置加载（info），法术没学/条件不满足静默跳过（debug）。

### 2.4 存档（H3AP6）

- 文本行 `"H3AP6 "` + H3AP5 全部字段（1304；策略整数值域 0..4）+ 4 个召唤整数（队数阈值/血量阈值/法术选择/敌方法力停止勾选）+ 1×60 共享规则 = **1368 整数**。
- `EncodeProfileStoreText`/`DecodeProfileStoreText` 扩展签名；`PROFILE_STORE_INTS` 重算并保留表达式自校验；Decode 拒绝一切非 H3AP6（含 H3AP5），策略值 ≥ 5 拒绝。
- `SaveProfileSlot`/`LoadProfileSlot`、面板草稿（`s_p.draft_summon_*`、`draft_protect_strategy` 值域）、`CommitProfiles` 同步扩展。

### 2.5 PolicyCore 与单测

新增纯函数与用例：

- `ProtectShouldCast` 现有语义不变；策略分派在执行层，`PS_SUMMON_LOW_FORCE` 不进该函数（召唤条件单独判）。
- `SummonShouldCast(stack_count, count_th, hp_total, hp_th, mana, mana_cost, hero_casted, learned)`。
- `PickSummonSpell(expertise[4], spell_pick, locked_index)`（自动 = 召唤总量最大；锁定沿用；平手取靠前）。
- `IsSummonedElemental(creature_id)`（四元素判定）。
- `SumSideRemainingHp(...)` / `CountAliveSideStacks(...)`（血量合计与存活队数，均含召唤物、不含战争机器）。
- `ChooseSummonMoveHex(kind, candidates, cur_hex, own_positions, rng)`（散开 = 分级满足：先「与所有己方部队距离 ≥2」、降「≥1」、两档均无则返回「原地防御」哨兵；当前格达标优先不移动；随机 = 均匀随机，不含当前格）。
- `ShouldStopOnEnemyMana(flag, has_enemy_hero, has_spellbook, enemy_mana, threshold)`（自动停止第二条件判定）。
- H3AP6 编解码 round-trip（含策略=4）、坏整数个数/旧 magic/策略值越界拒绝、共享规则 `Normalize`（行动集裁剪到手动/防御/散开/随机移动）。

`tests/PolicyCoreTests.cpp` 全部新增进现有框架；`build-tests.ps1` 跑全绿。

### 2.6 部队页准入与存档部队表

- 部队页枚举移除召唤物/克隆物卡片（集成层按稳定身份过滤 `STACK_ID_SUMMON`；`IsConfigurablePanelStack` 纯函数签名随之调整并更新单测）。
- 存档部队表只写开战部队 + 战争机器，**排除召唤身份槽**（修复召唤物混入干扰读档四轮关联的隐患）。

### 2.7 i18n / 帮助 / 文档

- 保活行：`panel.protect_opt4`（兵力不足时召唤）+ `tips.protect_opt4`（两条件与口径）；自动停止复选框 `panel.stop_mana_label` + `tips.stop_mana`。
- 召唤页：`panel.tab_summon`、`panel.summon_spell_label`、`panel.summon_spell_opt0..4`、`panel.summon_count_label`、`panel.summon_hp_label`、`panel.summon_action_label`、`tips.tab_summon`、`tips.summon_*`（法术下拉/队数·血量框/行动卡）、帮助模态新行；行动标签 `actions` 表扩「散开」「随机移动」两位（`g_action_labels` 同步，普通部队下拉不显示）。
- `kUiTextDefaults` 与 `lang/zh-CN.ini` 两处同步（惯例）。
- `使用说明.txt`：保活一节补第 5 种策略；自动停止一节补第二条件；召唤一节（法术/阈值/召唤物行动）。README 补一段；`设计文档.md` 落地后新增 §（保活策略扩项 + 自动停止第二条件 + 召唤页 UI + 执行 + H3AP6）。

## 3. 实施步骤（每步 Rebuild 0 error 再进下一步）

1. **PolicyCore**：`ProtectStrategy` 扩项、召唤常量/纯函数 + H3AP6 编解码 + 单测全绿（不接 UI/执行）。
2. **存储与面板数据流**：`g_summon_*` 全局与草稿、`CommitProfiles` 扩展、存读档 H3AP6、部队页移除召唤物卡片、存档部队表过滤。
3. **UI**：保活行扩第 5 项 + 队数/血量框（恒显示）；召唤页（`kTabOrder`/Tab 绘制切换、法术下拉、共享行动卡 + tips）。
4. **执行端**：`TryProtectCast_` 策略分派 + `SummonChannel_` + `UpdateStackTracking_` 召唤物绑定 + 状态清理（重打/结束边）。
5. **文案与文档**：i18n 两处同步、帮助模态、使用说明、README、设计文档。
6. **回归**：Rebuild 0 error；PolicyCoreTests 全过；上机按 §4/§5 验证。

## 4. 验收标准

- 保活策略 = 兵力不足时召唤：己方存活队数（含召唤物、不含战争机器）≤ 队数阈值 且 血量合计 ≤ 血量阈值 → 控制权交还时自动施放选定/自动选的召唤法术；任一不满足则不施、不记失败；召出后队数 +1 条件自然关闭；施放后本回合不再施（`hero_casted`）。
- 一个方案一种手段：策略 = 召唤时复活通道不触发（勾选的部队不救），策略 = 复活类时召唤通道不触发。
- 自动模式本场锁定元素：第一次成功施放后，后续自动选择不再变；固定元素未学则整场不召。
- 召唤物上场后按共享规则行动：散开按「2 格 → 1 格 → 防御」分级拉开距离，满足档位且当前格达标时不移动；随机移动各自随机选格，失败按「允许降级」勾选降级或交回玩家（复选框仅随机移动时显示/生效）。
- 勾选敌方法力停止条件时：敌方英雄（有魔法书）法力 ≤ 6 → 自动战斗切回手动（与停止回合数条件 OR，任一触发即停），全场自动化一并停止；不勾、敌方无英雄或无魔法书时不受影响。
- 部队页不再出现召唤物/克隆物卡片；存读档 H3AP6 正常（策略 0..4），H3AP5 读档失败提示不崩。
- 策略 1-4（复活类）行为与现状完全一致（回归）。
- 重打后：召唤物消失、元素锁定清空、策略与共享规则与 5 套方案保留。
- MSBuild Release Win32 0 error；PolicyCoreTests 全过；界面无 `!!…!!` 漏键。

## 5. 上机逆向验证清单

- 召唤法术 `CastSpell` 的 target hex / cast_type 实参语义（对照原版施放时的调用参数）。
- 四召唤法术的法力消耗与各等级（基础/高级/专家）召唤量公式（每点魔力 HP × 系数），落实「自动 = 召唤总量最大」常量表。
- 我方槽位占满时施放召唤的表现（失败静默、不崩溃、不重复投递）。
- 游戏侧「每场一种元素」限制在直接 `CastSpell` 下是否同样生效（锁定逻辑可否依赖游戏兜底）。
- 召唤物 `source_army_slot`/`count_at_start` 实测值（确认绑定与血量口径的取数正确）。
- 敌方英雄法力与魔法书标志的读取路径（战斗管理器对方英雄指针）与实测对拍（无书英雄法力常为 0，需验证不误触发）。

## 6. 检查记录（实施时填写）

- [x] 步骤 1（PolicyCore + 单测）：ProtectStrategy 扩 PS_SUMMON_LOW_FORCE；召唤常量（法术/生物 ID、阈值 6、15×11 六格）与纯函数（SummonShouldCast / PickSummonSpell(amounts,learned,pick,locked) / IsSummonedElemental / CountAliveSideStacks / SumSideRemainingHp / HexCoordDistance / ChooseSummonMoveHex / GetAllowedSummonActions / NormalizeSummonRule / ShouldStopOnEnemyMana）；H3AP6 编解码（1368 整数，SummonProfileFields 随行，旧 magic/越界拒绝）。单测 218→221 项全绿。注意：§2.5 文档签名 PickSummonSpell(expertise[4],…) 实施改为 amounts[4]+learned[4]（纯函数不查等级）。
- [x] 步骤 2（存储/面板数据流）：`g_summon[5]`（SummonProfileFields，方案级）+ ClearConfirmedProfiles 重置；Save/LoadProfileStore_ 与 Encode/Decode 透传；CommitProfiles 扩参（钳制 + NormalizeSummonRule）；s_p.draft_summon 草稿随开面板/存读档/提交流动；IsConfigurablePanelStack 加 is_summon_clone 参数（与 MakeStableStackIdentity 同口径：克隆强制无军队槽、军队槽 0..6 外且非战争机器 = STACK_ID_SUMMON 不进部队页）；BuildPanelArmyTable_ 排除召唤身份槽（存档部队表只写开战部队+战争机器）。编译 0 error，单测 221 全过。
- [x] 步骤 3（保活行 + 召唤页 UI）：PAGE_SUMMON Tab（kTabOrder 双页）；保活行加第 5 项「兵力不足时召唤」与「敌方法力≤6 时停」复选框（框在文字前，停止组左移让位）；召唤页=设置行（法术下拉 自动/气水火土 + 队数≤2/血量≤750 数字框，与停止回合同款录入交互：预填/光标/点外提交/ESC 取消）+ 说明行 + 召唤物行动卡（手动/防御/散开/随机移动 专用下拉 + 降级复选框仅随机移动显示）；展开态命中含触发器矩形、悬停走 UpdateDropdownHover_、切 Tab/切方案/关面板/开帮助/读档收尾、滚轮/卡片点击/右键删除按页门控；文案 UiTexts+zh-CN.ini 镜像（actions 7/8、tips 按当前项动态键）。编译 0 error。
- [x] 步骤 4（执行端，AutoExecute.inc.cpp）：① `g_summon_locked_spell`（自动模式本场元素锁定，ResetAutoState 清 → 覆盖战斗结束/重打/兜底边）。② `TryProtectCast_` 在英雄已施法守卫之后、复活耗魔 10 门槛之前分派 `PS_SUMMON_LOW_FORCE → SummonChannel_`（复活门槛不再挡召唤）。③ `SummonChannel_`：`OwnSideForceStats_`（TargetCandidate 口径：含召唤物、不含战争机器）→ 已学四系（GetSpellExpertise）与暂定召唤量表（力量×{2,3,4}）→ `PickSummonSpell(amounts, learned, spell_pick, locked)` → `SummonShouldCast` → 锚定格=己方第一存活部队格（§5 待验证）→ `CastSpell` SEH 包裹 → 按法力扣减/hero_casted 翻转确认成功 → 自动模式首成锁元素；全程事件型无回合标记。④ `UpdateStackTracking_` 末尾动态补绑：未绑定槽 + STACK_ID_SUMMON + 四系元素 + 存活 → 绑条目（游标归零）+ 套 `g_summon[p].summon_rule`；`BindStackTrackingFromBattle_` 对绑定时刻已在场的召唤物同套（重开重勾号场景）；克隆不绑。⑤ `SubmitSummonMove_`（缓冲 static）：逐格 0x475DC0+accessibleSquares2 收集可达格、己方其它部队位置（含战争机器、宽体记头格）→ `ChooseSummonMoveHex` → BA_WALK 提交；散开无解内部降防御（链固定），随机无解走 fallback_or_fail（受「允许降级为防御」勾选控制）；`SubmitConfiguredUnitAction_` 加 AA_SCATTER/AA_RANDOM_MOVE 分支。⑥ `TryAutoStop_` 前置敌方法力条件（勾选 + 敌方有英雄 + `DoesWearArtifact(0)` 判魔法书 + 法力 ≤6 → 同款切手动，日志 `[AutoStop] enemy mana low`；SEH 包裹），后走原回合投影（threshold 0 跳过）。增量编译 0 error。常量暂定项（法力 15、召唤量公式、锚定格语义）已列 §5/设计文档 §11.9 待上机对拍。
- [x] 步骤 5（文案与文档）：帮助模态加第 9 行 `help.line_summon` 并改 `help.line3`（模态 412→440，行 A 公式 8→9 行），line_summon/line3 双侧（kUiTextsDefaults + zh-CN.ini）同步；`使用说明.txt` 保活第 5 策略 + 新「七、召唤」章节 + 自动停止补敌方法力条件（后续章节顺延编号至十四）；README 加「自动召唤」段；`设计文档.md`：§3.1.1 策略表加第 5 项、§3.1.2 加第二条件、新增 §3.7 召唤通道、§4.1 枚举 AA_SCATTER=7/AA_RANDOM_MOVE=8、§4.3 g_summon[5]、§4.4 动态部队补绑口径、§8.6 存档 H3AP6（1368 整数）与帮助行、§8.7 五项下拉 + 敌方法力复选框、新增 §8.8 召唤页、§10.2 散开/随机移动提交、§10.4 分派与 TryAutoStop_ 顺序、§11.9 上机对拍项。
- [x] 步骤 6（回归）：MSBuild `-t:Rebuild` Release Win32 全量 0 error（642 函数全编译）；PolicyCoreTests 221 项全过；T() 键静态三方核对（modules 引用 76 键 / kUiTextsDefaults / lang/zh-CN.ini）无缺失；`[actions]` 7=散开/8=随机移动 与 DEFAULT_ACTION_LABELS 双侧齐备。设计文档/使用说明/README/§6 记录同步完成（本文件含 §2 与代码差异勘误）。剩余：上机 §5 清单（法力消耗/召唤量公式/target hex 语义/锁定兜底/敌方法力读取对拍）。
- [x] 上机验证（部分，2026-10-05 实测）：召唤通道整体已多轮实测——保活优先次序、`heroCasted` 挡施、`cast_ok` 判定（法力下降∨casted 翻转）、嵌套重入门卫 `g_cast_in_flight`、施法方校正 `CastSideGuardEnter_/Leave_`（详见 H3Note《战斗英雄一回合施法限制逆向笔记》§5.1/§5.2 与设计文档 §3.1.1）；敌方魔力停止（`DoesWearArtifact(0)` 判书）实战在用。
- [ ] 上机对拍遗留（设计文档 §11.9 跟踪）：召唤法力消耗（暂定 15，`kSummonManaCostProvisional`）、召唤量公式（暂定 力量×{2,3,4}，`SummonCountAmount_`）、`CastSpell` target hex 实参语义（暂用己方第一存活部队格锚定，`anchor_hex`）三项仍为暂定口径，需对拍原版后修订常量；「每场一种元素」锁定逻辑为插件自记 `g_summon_locked_spell`，游戏侧是否兜底未验证。

## 勘误（后续升级记录）

- **敌方魔力阈值可调 + 默认勾选（升级 §0 第二条件）**：「敌方法力≤6 时停」升级为「[✓] 敌方魔力≤[输入框] 时停」——复选框保留为启用开关（**默认勾上**），固定常量 6 改为数字框（默认 6，范围 0..32767，随方案存档）。存档格式 H3AP6→H3AP7（召唤整数 4→5 个，共 1369 个整数）；兼容读旧 H3AP6 档（阈值缺省 6，勾选态按档内值）。§0 中「阈值先取常量 6，要做成数字框后续再升级」已完成；「默认不勾」已改为「默认勾选」。
- **g_profiles 驻留默认（修复）**：原 `g_profiles[5][21] = {}` 全零与 `MakeDefaultRule()` 默认不一致（剩≤ 显示 0 并随勾号固化）；改为同 TU 静态初始化器 `DefaultRulesInit_` 回填默认规则。
- **召唤物默认行动改防御**：`MakeDefaultSummonFields()` 的 `summon_rule.action` 由默认手动（AA_MANUAL）改为默认防御（AA_DEFEND，在 `GetAllowedSummonActions` 允许集内，`NormalizeSummonRule` 不裁）；存档内行动按档内值还原。
- **输入屏障改为每次全新安装（修复重开面板鼠标失效）**：0.3.2026.930 实测日志——同一场战斗内停用(HideDeactivate)→重激活(ShowActivate)后鼠标点击不再到达屏障 item（8 个重激活会话零点击响应、3 个全新安装会话全部正常；键盘钩子全局独立不受影响，表现为「面板开着方案切换不了，Enter/ESC 仍可提交/取消」）。推测 Hide/Show 未恢复 `ItemAtPosition` 命中所依赖的完整状态、或战斗进行中游戏动态添加的 item 把旧位置的屏障压到下层。`InstallBattleInputBlocker_` 删除重激活分支：每次打开全新 Create + AddItem 到链尾（恒为最上层），旧 item 还原 vtable 并隐藏留链、待 BattleUI 析构由游戏回收；同时屏障处理器加 `[Panel] blocker hit` 到达性 debug 日志（点击没到屏障时日志缺失即可定位上游问题）。
- **方案生效范围改为「面板所见」+ 阵亡部队可配（修复跨场/移除部队规则残留）**：两处根因——① `ClearConfirmedProfiles` 清了 g_profiles 但**面板草稿 draft_rules 没清**：接受结果后下一场打开面板仍显示上一场部队的旧规则，点「确定」旧规则复活生效；② 非本场槽位的驻留草稿全量 memcpy 进 g_profiles，随确定生效/被存档按钮写进 ini。修复：① `ResetPanelDrafts()`（SettingsDlg 定义、ConfigLog 前向声明调用）随接受结果清 5 套草稿（规则/策略/停止/召唤参数），跨场沿用配置走存/读档（四轮关联按类型+数量对位）；② 打开面板时草稿快照：`IsConfigurablePanelStack_` 判定外的槽位 5 套草稿回默认（日志 `[Panel] 草稿快照：清掉 N 条非本场部队驻留规则`）；③ 面板准入由「当前数量>0」改为「`numberAtStart>0`（本场存在过）」——**阵亡的己方非召唤部队仍列出卡片**（数量显示 `--`），规则可配、复活后生效；空槽/召唤克隆/弹药车不列、投石车需弹道术不变。`IsConfigurablePanelStack` 第二参语义 number_alive→count_initial，单测补阵亡/空槽用例（228 项）。
- **战斗指纹与方案自动恢复（新增，§17）**：给每场战斗算稳定标识——FNV-1a 64 over 开局态（双方 21 槽 type+count_at_start 排序后喂入、双方英雄 id、地形、攻城类型、**冒险地图触发点 x/y/z**，z 区分地上/地下；取被攻击方坐标：守方英雄格 → 人类英雄 dest 相邻则取 dest（主动攻击/攻城）→ 英雄格兜底（被撞）；不用攻击发起格——换方向攻击仍是同一场）。挂点 `BE_BATTLE_UI_APPEARED` 边（重打走 RESULT_RETRY 不重算；快速战斗无战斗 UI 不算不恢复）。`_Hero_` 补 x/y/z（0x00-0x05）与 dest_x/y/z（0x35/0x39/0x3D），`_BattleMgr_` 补 land_type（0x5394）/siege_kind（0x53A4），两结构加 `#pragma pack(1)`（默认对齐下 int id 落 0x1C 非 0x1A；pack 后既有使用点偏移不变）。官方 `mapitem`@0x53BC 存战斗发生格但 `GetCoordinates` 是 H3API.dll 导入、裸结构取不到坐标，故走 dest 推导。单测 7 例（槽序无关/数量差一/地上地下/邻格/攻守互换/零数量槽忽略），235 项全过。
- **智能存读档大改（§17 重做，替代上一条的自动恢复机制）**：用户定案 5 点——①去掉「存档/读档」按钮：点√确定时自动存档，文件名=`<指纹>.json`；②同场多次确定各存一条（确认时间为准）；③原「存档」按钮位置改**本场存档下拉框**（时间 `yyyymmdd-hhmmss` 降序）；④开面板时自动填下拉框，本场未载入过则自动把**最新档载入草稿**（不生效，确定才生效；战斗开始不再自动恢复——不打面板=空方案）；⑤每场保留最近 30 条。实现：ConfigLog 重写战斗存档库为 JSON 多记录（`{"version":1,"battle":hex,"entries":[{time,active,p:[5×H3AP7]}]}` 升序，手写轻量解析 `JsonFindValue_/JsonReadString_/ParseRecordText_`——H3AP7 文本仅数字+空格无需转义；追加时与末条**内容比对**（`BattleStoreRecordContentEquals`，忽略时间戳）相同跳过、满 30 丢最老整文件重写；跨文件仍 mtime LRU 30 场，文件名严格匹配 16hex+.json）；`OnBattleAppearedFingerprint_` 瘦身为只算指纹+日志（恢复链/`g_profiles_dirty`/`g_restoring_battle`/`ApplyCommittedToDrafts` 全删）；SettingsDlg 新增 `s_battle_records[30]`(.bss)/`s_battle_loaded_fp`（按指纹记「本会话已载入」，重打/S&L 同指纹不重载）/下拉框（原读+存档合并区 94px 按钮宽、展开 160px×14px/行浮层，hover 由 WH_MOUSE 钩子驱动同保活下拉）/`LoadBattleRecordIntoDrafts_`（5 套全量+激活号进草稿，收尾同旧读档：取消录入/收起下拉/`LoadSelectedProfileIntoCells_`）；旧 `SaveProfileStore_/LoadProfileStore_/ProfileSlotPath/SaveProfilesToDisk_/LoadProfilesFromDisk_/BuildPanelArmyTable_` 及 profiles1-5.ini 入口整体移除（磁盘文件不动）。坑：`BattleStoreRecord` 需放 `SummonProfileFields` 定义之后且 PolicyCore 尾部 `using` 表要补三项，否则 ConfigLog 里 C2061；大缓冲（单条 entry ≈160KB、单方案文本 32KB、记录 10KB）一律堆分配防游戏线程栈溢出；`_snprintf` 分段拼接 offset 累加（头段 160 上限 vs 实际 ~45 字符，勿加长头段）。单测 +16 例（内容比较 7/时间戳校验 9，含 hour24/sec60/month13 拒收），251 项全过。
- **帮助模态加开源地址行（非按钮样式，点击复制）**：打包日志按钮与关闭按钮之间新增一行纯文本「开源地址（点击复制）：https://github.com/Apq/H3Auto」——smalfont + 18px 紧凑行高 + 富文本灰标签/白 URL（游戏字体只有 bigfont/smalfont 两档，`tiny.fnt` 实测非标准 H3 调色板字体格式，解析不出 ASCII 字形表，不可用），无 Fill/DrawFrame 按钮框。点击热区 `GetHelpLinkRect_`（PanelDraw.hpp 导出给 SettingsDlg），`CopyTextToClipboard_`（CF_UNICODETEXT，OpenClipboard×5 重试同 LogPack 节奏）复制 `help.link_url` 键的纯 URL；成功后地址行原位闪绿 1.2s（`s_help_link_flash_tick`，绘制端按 GetTickCount 差值选 `help.link_flash`/`help.link` 键——状态栏在帮助模态百叶窗遮暗层下不够醒目，故做原位反馈），同时状态栏+LogInfo。
- **修复：战斗存档读写必炸 0xC0000005（点√后仍「无存档」的根因）**：上机日志实锤——`[BattleStore] 追加战斗存档异常 code=0xC0000005`/`读战斗存档异常`，SEH 吞掉后表现为存档从不落盘、面板永远「无存档」。根因：`BattleStorePath_`/`PruneBattleStore_LRU_` 里 `char dir[kPathCap_]`/`pattern`/`full`——`kPathCap_` 在 IniUtf8 里是 **4MB**（为日志路径兜底设计），放栈上直接超游戏线程 ~1MB 栈（写路径函数时只盯着「调用方 path 已堆分配」，忘了函数内部的目录缓冲也要看 kPathCap_ 的量级）。修复：三处全改 `new(std::nothrow) char[kPathCap_]()`+函数尾 delete；`grep 'char \w+\[kPathCap_\]'` 全模块归零确认无同类。教训已并入 h3auto-panel-dropdown-store 技能的「栈预算」条目：**看一个缓冲的量级要看常量定义值，不能只看「别的函数也这么写」**。
- **本场存档下拉框：样式对齐 + 选中项记忆**：①样式改与保活/卡片下拉统一——按钮底色 74,52,24（展开/按下 104,70,28）、金框 210,170,72、右侧像素三角箭头（`CellControl_DrawArrow`，收起▼/展开▲）、文字金色左对齐；列表每项独立底色+边框（悬停 184,136,48 / 选中 136,88,24 / 普通 68,42,18），选中项金色、其余黄色。旧的「按钮式双层金框+居中文字+单层 hover 底色」是把下拉框画成了按钮，玩家认不出能展开。②选中项记忆：`s_battle_sel_fp`+`s_battle_sel_time`（进程内，记**时间戳**不记下标——新档追加到末尾会改变下标，时间戳稳定），`CommitAndCloseSettingsPanel_` 在 `CommitProfiles` 后记下当前选中档时间戳（内容未变不新增时选中档不变；内容变了新档即新选中），`RefreshBattleRecordsAndAutoload_` 每次打开按时间戳找回对应条恢复选中，找不到/换指纹默认最新（0）。自动载入目标从「恒最新」改为「当前选中项」，首开载入与选中项一致。不落盘，进程重启归零。
- **停止条件组 1px 金框**：保活行右侧「停止 [框] 回合」+「[✓] 敌方魔力≤ [框] 时停」是**一起生效**的停止条件组，但视觉上无分组提示。加 `STOP_GROUP_*` 布局常量（PanelLayout）+ `DrawProtectStrategyRow_` 开头先画 `DrawFrame`（1px，210,170,72 金框色）再画控件。坑两条：①外框必须在控件之前绘制——数字框/复选框的 Fill 底色会盖掉后画的框线；②右内边距必须为 0——「时停」文字右缘已贴内容区右缘 660，再外扩 5px 就超出内容区、撞右上按钮区。最终几何 x=391..660（恰对齐内容区右缘）、y=81..107（PROTECT_DD_H=18 + 上下 4px）。
- **修复：本场存档解析推进差一（存档成功但重开面板「无存档」）**：上机现象——`[BattleStore] 已存档` 且 json 文件正常落盘（19KB），重开面板却显示「无存档」。根因 `ParseRecordText_` 的「跳到下一个字符串」推进逻辑：`JsonReadString_(pv,…)` **不动 pv**，pv 仍指向当前串的开引号；原代码 `pv=strchr(pv,'"')` 在 pv 已是开引号时返回**原地**（不是闭引号），`strchr(pv+1,'"')` 找到闭引号，`++pv` 落到 `','` → 下一次 `JsonReadString_` 检查 `*v!='"'` 立即失败 → 第 1 串成功、第 2 串起全挂 → 整条 entry 判坏 → count=0。修复：闭引号 = `strchr(pv+1,'"')`，下一个开引号 = 从闭引号后继续 find；找不到时区分「第 5 串读完（正常收尾）」与「还有方案没读（格式坏）」。教训（并入技能 Pitfalls）：**手写扫描器里「定位函数不动入参指针」是隐形契约**——`JsonReadString_(v, …)` 的 v 语义是「指向开引号的只读起点」，循环推进必须自己管理；凡是 `strchr(p, c)` 且 `*p==c` 的场景都要先想「返回的是不是原地」。另：**真实落盘文件是最好的测试夹具**——用 Python 逐字符模拟 C 逻辑复现 bug 与验证修复，一次到位（单测覆盖不到 ConfigLog 集成层）。
