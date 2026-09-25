// PanelInput.hpp - 面板输入捕获层的函数声明（实现在 PanelInput.inc.cpp，
// H3Auto.cpp 中排在 SettingsDlg.inc.cpp 之后）。仅声明 SettingsDlg 直接
// 调用的函数；钩子与拾取内部函数保持文件静态。
#pragma once

#include "PanelLayout.hpp"

static LRESULT CALLBACK PanelKbHook_(int code, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK PanelMouseHook_(int code, WPARAM wParam, LPARAM lParam);
static void ForcePanelDefaultCursor_();
static H3CombatManager* GetCombatMgr();
static bool BlockBattleHover_();
static void RestoreBattleHover_();
static H3BaseDlg* FindDialogByVtable_(UINT target_vtable);
static bool InstallBattleInputBlocker_();
static void RemoveBattleInputBlocker_();
static void ForcePanelModalDepth_(bool on);
static void EndSpellPick_();
static bool CommitSpellSlotPick_(int slot_value);
static void DoPickCapture_(int hex, bool right_click);
static void UpdatePanelModalSuspension_();
static INT32 GetBattleItemUnderCursor_(H3BaseDlg* battle_ui);
