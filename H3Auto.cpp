// H3Auto.cpp
// 英雄无敌3 SoD 插件：战场自动化。
// 目标版本：Shadow of Death（SOD = 0xFFFFE403），仅 x86。

#define _H3API_PATCHER_X86_
#include <H3API.hpp>
#include <ddraw.h>
#include <stdarg.h>
#include <wchar.h>
#include <stdint.h>

using namespace h3;

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

// 模块按顺序包含到同一个翻译单元，保证 patcher 全局对象和静态辅助函数共享同一份状态。
#include "modules/ConfigLog.inc.cpp"
#include "modules/Compat.inc.cpp"
#include "modules/AutoExecute.inc.cpp"
#include "modules/Entry.inc.cpp"
