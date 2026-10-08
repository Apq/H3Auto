# H3Auto · 总体架构与 Hook

[文档导航](<README.md>)

> 项目定位、模块职责、包含顺序与 SoD 钩子索引。
> 2026-10-08 按用途拆分，数字章节号沿用原设计，跨章节引用链接到对应分册。以实际代码为准；本次拆分不新增实机验收结论。

<a id="section-1"></a>
## 1. 项目目标与定位

打铁助手（工程名 H3Auto）是一个英雄无敌3（SoD / HD Mod）打铁助手 DLL 插件。核心定位：

- **辅助人类玩家，减少重复性操作。** 在漫长战斗中代替玩家发出重复的游戏操作。
- **两种实现途径**：直接调用原版函数，或模拟玩家操作（点击 / 按键）。
- **只辅助人类玩家，不影响电脑阵营。** 回放、隐藏战斗、AI 回合一律不接管。

<a id="section-1-1"></a>
### 1.1 核心原则

1. **不接管全部决策。** 只在玩家预设了策略的部队上代为发出操作，其余保持手动。
2. **技能条件化。** 战争机器的可控性取决于英雄是否拥有对应技能（见 [§2](<02-部队行动与目标策略.md#section-2>)）。
3. **降级安全。** 配置的行动无法实现时，按玩家意愿降级为防御，或转回手动（见 [§3.6](<02-部队行动与目标策略.md#section-3-6>)）。
4. **只辅助人类。** 电脑阵营、回放、隐藏战斗一律不接管。
5. **生效方案驻留内存、确认时另存 JSON。** 五套方案和各编号草稿驻留进程内存，规则不写回 INI；点勾号按战斗指纹另存方案 JSON（见[方案存档](<07-战斗指纹与方案存档.md>)）。INI 保存热键、日志、语言与上次生效编号等配置。上次勾号生效编号存 `H3Auto.user.ini [General] LastProfile`。

<a id="section-1-2"></a>
### 1.2 不负责的范围

- 敌方部队 / 电脑阵营的策略。
- 战斗 AI 逻辑（由游戏原版处理）。
- 生物信息窗口扩展、战斗价值显示等非自动化功能。

---

<a id="section-12"></a>
## 12. Hook 列表（实测以代码为准）

| 地址        | 类型     | 用途                        |
| --------- | ------ | ------------------------- |
| `0x600430` | LoHook | BltComplete：面板绘制 / 施法等待推进 / 热键轮询 / 状态机事件源（UI 出现·消失·P 键消费） |
| `0x4746B0` | LoHook | 战斗消息入口：面板输入屏蔽 / 提交部队主动作 |
| `0x4744D0` | HiHook | 是否自动执行判定（`HH_ShouldAutoExecute`，仅 `orig==0` 时介入）：施法通道判定（保活/保持状态/召唤）/ 战争机器交回 AI / 人工接管门 / 管线残留清理 |
| `0x4786B0` | HiHook | 原版动作执行入口（单次接管完成确认） |
| WH_KEYBOARD | 线程钩子 | 常驻键盘钩子：F9/J/P 热键与数字录入捕获（keydown 边沿） |
| WH_MOUSE | 线程钩子 | 面板鼠标：下拉悬停 / 屏蔽战斗 hover |
| `+0x132D4` | 字段     | `H3CombatManager::mouseCoord`（拾取当前格；面板开时写 -1 屏蔽 hover） |
| `0x493FC0` | 调用     | 战场重绘（撤销临时格子标示 / 屏蔽 hover 后重绘） |
| `0x0063D46C` | 虚表   | 原版 CPResult 结果窗（生命周期检测；另 `0x0063D528` 战斗窗、`0x0063DB40` 右键说明框） |
| `0x7802` / `0x1FB` | 控件 id | 结果窗确定 / HD 取消重打 |
| `0x5A0140` | 调用     | `H3CombatManager::CastSpell`（保活/保持状态/召唤三通道施法核心） |
| `0x475DC0` | 调用     | 原版移动目标判定（刷新 `accessibleSquares2`，循环移动可达性校验） |
| `0x46A080` | 调用     | 隐藏战斗判定（快速战斗/AI 对打/回放，执行链入口守卫） |
| `0x699650` | 全局     | 游戏窗口 HWND（热键前台判定 / 键盘钩子线程 / 唤醒消息投递） |

> 具体挂接以 [Entry.inc.cpp](<../modules/Entry.inc.cpp>) 与各模块实际代码为准；地址如与实测不符，以代码注释为准。
> **铠甲**：上表所有入口（4 个 patcher 钩子 + 键盘/鼠标回调）均经 `CrashGuard` 外壳包裹（[§18](<09-诊断日志与崩溃防御.md#section-18>) L2/L3），异常吞没并落盘 error，安全默认值放行原版逻辑；原版函数自身的异常不在吞没范围。

---

<a id="section-13"></a>
## 13. 文件结构

```
H3Auto/
├── H3Auto.cpp               # 主入口
├── H3Auto.vcxproj
├── H3Auto.default.ini       # 出厂默认配置（热键 / 语言 / 日志默认值）
├── H3Auto.user.ini          # 玩家改动层（日志级别；运行时生成，可不存在）
├── H3Auto.rc / resource.h   # 版本信息
├── build_and_deploy.bat     # 编译 + 部署（统一入口；build.ps1 存在但不要单独用）
├── deploy.ps1               # 部署脚本（被统一入口调用）
├── README.md / 设计文档.md   # 项目入口与分册索引
├── docs/                    # 功能设计、维护说明与历史实施记录
├── 使用说明.txt / LICENSE
├── lang/zh-CN.ini           # 界面文案（i18n，UTF-8 BOM；缺文件/缺键回落代码内置默认表）
├── lzma/                    # LZMA SDK 源码级集成（日志打包 .7z 用）
├── third_party/nlohmann/json.hpp  # 战斗存档 JSON（§17）
├── img/
│   ├── HA_bg.pcx            # 面板背景 680×548
│   ├── HA_cell.pcx          # 卡片背景 511×110（随卡片尺寸重生）
│   ├── HA_grid_frame.pcx    # 网格金框（金框线已不画，图保留备用）
│   ├── HA_icon_frame.pcx    # 图标金框（60×66）
│   ├── HA_ok_normal/pressed.pcx / HA_cancel_normal/pressed.pcx / HA_button_frame.pcx
│   └── 效果图.pcx
├── tools/
│   ├── gen_cell_frame.js    # 生成卡片背景（传宽高）
│   ├── gen_grid_frame.js / gen_icon_frame.js
│   └── extract_button_assets.js / inspect_cell.js / inspect_frame.js
├── tests/
│   ├── PolicyCoreTests.cpp  # 纯策略核心单测（与插件共用 PolicyCore.hpp，不依赖游戏进程）
│   ├── PolicyCoreTests.vcxproj
│   └── build-tests.ps1
└── modules/
    ├── PolicyCore.hpp       # 纯策略核心（枚举/结构/身份/编解码/选择器；生产与单测共用）
    ├── IniUtf8.inc.cpp      # UTF-8 ini 读写器
    ├── ConfigLog.inc.cpp    # 配置 / 日志分级 / 战斗存档库（§17）
    ├── CrashGuardCore.hpp   # 崩溃防御纯逻辑（异常码命名/噪音过滤/历史环/SoD 指纹；单测覆盖）
    ├── CrashGuard.inc.cpp   # 崩溃自记录 + 钩子铠甲 + 自熔断 + 版本门卫（§18）
    ├── UiTexts.inc.cpp      # 界面文案外置（T() / LoadUiTexts）
    ├── LogPack.inc.cpp      # 日志打包 .7z + CF_HDROP 剪贴板
    ├── Compat.inc.cpp       # 结构体与内存访问兼容层
    ├── PanelGfx.inc.cpp     # 面板图片资源
    ├── PanelLayout.hpp      # 面板布局常量
    ├── BattleState.hpp / .inc.cpp  # 战场状态机（SetPhase_ 唯一出口）
    ├── AutoExecute.inc.cpp  # 接管判定 / 动作提交 / 目标选择 / 保活 / 自动停止
    ├── ForceField.hpp / .inc.cpp # 两落点力盾配置与保持
    ├── TextLayoutCore.hpp   # 实际行高文字矩形纯几何核心
    ├── SettingsDlg.inc.cpp  # 设置面板 / 网格 / 滚动条 / 战场拾取
    ├── CellControl.inc.cpp  # 卡片控件绘制 / 命中 / 下拉几何
    ├── PanelInput.hpp / .inc.cpp   # 面板输入（鼠标 / 键盘钩子）
    ├── PanelDraw.hpp / .inc.cpp    # 面板绘制（Tab / 帮助 / tips / 状态栏）
    └── Entry.inc.cpp        # Hook 挂接与入口
```

---

## 编译组织

[H3Auto.cpp](<../H3Auto.cpp>) 按顺序将模块包含到同一翻译单元，共享静态状态：

```text
IniUtf8 → ConfigLog → CrashGuard → UiTexts → LogPack → Compat → PanelGfx
→ AutoExecute → ForceField → BattleState → CellControl → SettingsDlg
→ PanelInput → PanelDraw → Entry
```

包含顺序以入口文件为准，LZMA C 源独立参与编译；依赖及构建配置见[工程配置](<../H3Auto.vcxproj>)。
