// H3Auto.cpp
// 英雄无敌3 SoD 插件：打铁助手。
// 目标版本：Shadow of Death（SOD = 0xFFFFE403），仅 x86。

#define _H3API_PATCHER_X86_
#include <H3API.hpp>
#include <ddraw.h>
#include <shellapi.h> // DROPFILES（日志打包 CF_HDROP 文件式剪贴板）
#include <new>        // std::nothrow（日志打包大缓冲分配失败检查）
#include <stdarg.h>
#include <wchar.h>
#include <stdint.h>

using namespace h3;

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

// 模块按顺序包含到同一个翻译单元，保证 patcher 全局对象和静态辅助函数共享同一份状态。
#include "modules/IniUtf8.inc.cpp"
#include "modules/UiPortHost.inc.cpp"  // 界面契约宿主（g_uiPort）；须先于首个使用方 ConfigLog
#include "modules/ConfigLog.inc.cpp"
#include "modules/CrashGuard.inc.cpp"   // 依赖 ConfigLog 落盘函数；其后模块用钩子铠甲/熔断
#include "modules/UiTexts.inc.cpp"
#include "modules/LogPack.inc.cpp"
#include "modules/Compat.inc.cpp"
#if !defined(H3AUTO_UI_NULL)
// 无界面编译变体（诊断/自动化验证用，不进玩家分发包）：定义 H3AUTO_UI_NULL
// 时跳过全部界面实现包含，g_uiPort 保持 UiPortHost 的兜底空实现——
// 面板/拾取/悬浮物不存在，自动化与生命周期层照常运行（docs/13 批次3）。
#include "modules/PanelGfx.inc.cpp"
#endif
#include "modules/SpellRules.inc.cpp"
#include "modules/AutoExecute.inc.cpp"
#include "modules/ForceField.inc.cpp"
#include "modules/AttackMagic.inc.cpp"
#include "modules/BattleState.inc.cpp"
#include "modules/BattleLifecycle.inc.cpp"
#if !defined(H3AUTO_UI_NULL)
#include "modules/CellControl.inc.cpp"
#include "modules/SettingsDlg.inc.cpp"
#include "modules/PanelInput.inc.cpp"
#include "modules/PanelDraw.inc.cpp"
#endif
#include "modules/Entry.inc.cpp"
