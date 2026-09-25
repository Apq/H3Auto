// PanelDraw.hpp - 面板内容绘制的函数声明（实现在 PanelDraw.inc.cpp，
// H3Auto.cpp 中排在 SettingsDlg.inc.cpp 之后）。仅声明 SettingsDlg 的
// 输入处理/钩子/生命周期直接调用的函数；其余绘制函数是 PanelDraw 内部静态。
#pragma once

#include "PanelLayout.hpp"

static void DrawMeleePickMarker_();
static void GetHelpModalCloseRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void GetHelpLogLevelDdRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void GetHelpLogLevelItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h);
static void GetHelpPackBtnRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void GetSpellKeyModalRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void GetProtectDdItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h);
static void GetSpellKeyModalCancelRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void DrawSpellKeyModal_(H3LoadedPcx16* scr);
static void DrawPanelToBuffer_();
