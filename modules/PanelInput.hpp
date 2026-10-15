// PanelInput.hpp - 面板输入捕获层的函数声明（实现在 PanelInput.inc.cpp，
// H3Auto.cpp 中排在 SettingsDlg.inc.cpp 之后）。仅声明 SettingsDlg 直接
// 调用的函数；钩子与拾取内部函数保持文件静态。
// FindDialogByVtable_/GetBattleItemUnderCursor_ 批次2 起定义在
// BattleLifecycle.inc.cpp（本文件更早包含），两边按同 TU 顺序取用。
#pragma once

#include "PanelLayout.hpp"
#include "PanelInputCore.hpp"

static LRESULT CALLBACK PanelKbHook_(int code, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK PanelMouseHook_(int code, WPARAM wParam, LPARAM lParam);
static void ForcePanelDefaultCursor_();
static H3CombatManager* GetCombatMgr();
static bool BlockBattleHover_();
static void RestoreBattleHover_();
static bool InstallBattleInputBlocker_();
static void RemoveBattleInputBlocker_();
static void ForcePanelModalDepth_(bool on);
static void DoPickCapture_(int hex, bool right_click);
static void UpdatePanelModalSuspension_();
