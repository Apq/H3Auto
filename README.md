# 打铁助手

手动战斗自动化插件（名称：打铁助手）。

## 战斗中人工接管

全部部队都配置了自动动作后，可用热键临时拿回控制权（不改 5 套方案）：

- `F9`：切换本场「自动 / 全手动」
- `J`：锁定当前或下一支部队，人工行动一次后恢复自动

热键可在 `H3Auto.ini` 的 `[Hotkeys]` 修改。

设置面板右上角 `?` 可查看完整使用说明。

## 依赖

- 英雄无敌3 HD Mod / patcher_x86
- Visual Studio 18 / v145 工具集
- Win32 / x86 Release 构建

## 编译

```powershell
.\build.ps1
```

输出：`Release\H3Auto.dll`

## 单元测试

纯策略核心位于 `modules/PolicyCore.hpp`，生产插件和 `tests/PolicyCoreTests.cpp` 共用，不依赖游戏进程。

```powershell
.\tests\build-tests.ps1
.\Release\tests\PolicyCoreTests.exe
```

覆盖：战争机器技能分支、面板准入、远程/急救独立选择器、候选评分、非法行动规范化、旧施法槽兼容、结果窗接受/取消生命周期、稳定部队身份重排。

## 部队身份与快速战斗重打

插件不使用 `_BattleStack_` 指针或战场数组下标作为跨重打身份。配置身份与本轮运行定位分开：

- 普通开战部队：使用 `side + source_army_slot`；数量、位置、伤亡和战斗效果变化不会改变身份。
- 战争机器：使用战争机器类型和同类序号；战争机器通常没有有效的 `source_army_slot`。
- 召唤物和克隆物：没有跨重打配置身份，不继承原部队规则。
- `army_slot_ix`、指针和行动队列：只用于当前战斗中的定位和执行，不作为持久配置键。

HD 快速战斗点“取消/重打”后，5 套方案保留；插件按稳定身份把方案重排到新战场槽位，清除旧指针、等待状态和循环游标，再重新绑定当前战场。点击“确定/接受”才清空方案。

## 部署

```powershell
.\deploy.ps1
```

默认部署到：

```
D:\Heroes3\Heroes3_2026.05.01\_HD3_Data\Packs\打铁助手
```
