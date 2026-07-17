# 打铁助手

手动战斗自动化插件（原名“战场自动化”）。

## 战斗中人工接管

全部部队都配置了自动动作后，可用热键临时拿回控制权（不改 5 套方案）：

- `F11`：切换本场「自动 / 全手动」
- 左 `Ctrl`：锁定当前或下一支部队，人工行动一次后恢复自动

热键可在 `H3Auto.ini` 的 `[Hotkeys]` 修改。

## 依赖

- 英雄无敌3 HD Mod / patcher_x86
- Visual Studio 18 / v145 工具集
- Win32 / x86 Release 构建

## 编译

```powershell
.\build.ps1
```

输出：`Release\H3Auto.dll`

## 部署

```powershell
.\deploy.ps1
```

默认部署到：

```
D:\Heroes3\Heroes3_2026.05.01\_HD3_Data\Packs\打铁助手
```
