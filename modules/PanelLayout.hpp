// PanelLayout.hpp - 设置面板布局常量（SettingsDlg / PanelGfx / PanelDraw 共用）。
#pragma once

static const int PANEL_W    = 680;
// 底部状态栏 +26，背景图 HA_bg.pcx 同步为 680×528。
static const int PANEL_H    = 528;
static const int COLS       = 1;   // 一行一格（单列宽格）
static const int VISIBLE_ROWS = 3; // 金框内刚好 3 行
static const int CELL_W     = 568; // 横向占满金框宽（右侧留滚动条）
// 卡片恢复旧版高度 110：金框 624×342，底边 y=414，表格上方不再留设置行。
// SCROLL_H = CELL_H + 2*(CELL_H-2) = 110 + 2*108 = 326。
static const int CELL_H     = 110;
static const int CELL_STEP_X = CELL_W - 2;
static const int CELL_STEP_Y = CELL_H - 2;
static const int GRID_X      = 41;
static const int GRID_Y      = 102;
static const int SCROLL_X    = GRID_X + CELL_W + (COLS - 1) * CELL_STEP_X + 18;
static const int SCROLL_Y    = GRID_Y;
static const int SCROLL_W    = 16;
static const int SCROLL_H    = CELL_H + (VISIBLE_ROWS - 1) * CELL_STEP_Y;  // 326
static const int MARGIN      = 20;
static const int TITLE_H    = 44;
static const int BTN_W      = 64;
static const int BTN_H      = 30;
static const int BTN_FRAME_W = 66;
static const int BTN_FRAME_H = 32;
static const int BTN_GAP    = 24;
static const int BTN_Y      = PANEL_H - 83;
static const int OK_X       = (PANEL_W - BTN_GAP) / 2 - BTN_W;
static const int CANCEL_X   = (PANEL_W + BTN_GAP) / 2;
static const int CELL_COUNT = COLS * VISIBLE_ROWS;
static const int MAX_STACKS = 21;
static const int PROFILE_COUNT = 5;
static const int PROFILE_BTN_W = 104;
static const int PROFILE_BTN_H = 18;
static const int PROFILE_BTN_GAP = 8;
static const int PROFILE_BTN_Y = 47;
static const int PROFILE_BTNS_W = PROFILE_COUNT * PROFILE_BTN_W
    + (PROFILE_COUNT - 1) * PROFILE_BTN_GAP;
static const int PROFILE_BTN_X = (PANEL_W - PROFILE_BTNS_W) / 2;

// 右上角帮助按钮（点击弹使用说明模态框）
static const int HELP_BTN_SIZE = 24;
static const int HELP_BTN_X = PANEL_W - HELP_BTN_SIZE - 20; // 再左收 1px
static const int HELP_BTN_Y = 20; // 再下收 2px

// 「读档」「存档」：? 按钮左侧，同高 24，宽 44，间距 6。
static const int STORE_BTN_W = 44;
static const int STORE_BTN_GAP = 6;
static const int SAVE_BTN_X = HELP_BTN_X - STORE_BTN_GAP - STORE_BTN_W;
static const int LOAD_BTN_X = SAVE_BTN_X - STORE_BTN_GAP - STORE_BTN_W;

// 网格金框：宽 624，高 342（底边 y=436）。
static const int GRID_FRAME_W = 624;
static const int GRID_FRAME_H = 342;
static const int GRID_FRAME_X = 29;
static const int GRID_FRAME_Y = 94;

// 方案行与金框之间的保活策略行（方案级）：label + 下拉框。
static const int PROTECT_DD_Y        = 69;
static const int PROTECT_DD_H        = 18;
static const int PROTECT_DD_LABEL_X  = GRID_FRAME_X;
static const int PROTECT_DD_LABEL_W  = 76;   // 「保活策略:」
static const int PROTECT_DD_X        = GRID_FRAME_X + 80;
static const int PROTECT_DD_W        = 150;
static const int PROTECT_DD_ITEM_H   = 18;

// 保活行最右侧：自动停止回合数。点击后用数字键录入（可编辑文本框：
// 预填当前值、光标可左右移动、退格删除；见 s_stop_turns_* 状态）。
static const int STOP_BOX_W = 52;
static const int STOP_BOX_X = GRID_FRAME_X + GRID_FRAME_W - STOP_BOX_W;
static const int STOP_LABEL_W = 42;
static const int STOP_LABEL_X = STOP_BOX_X - 4 - STOP_LABEL_W;

// 保活策略项（顺序同 ProtectStrategy）
static const char* PROTECT_STRATEGY_LABELS[H3AutoPolicy::PS_COUNT] = {
    "无", "部队全灭后", "回合内首动", "损失量大于恢复量",
};

// 硬编码默认标签（INI 加载失败时使用）
static const char* DEFAULT_ACTION_LABELS[AA_COUNT] = {
    "手动", "防御", "等待", "循环移动", "循环近战", "远程攻击", "急救治疗",
};
static const char* DEFAULT_SELECTOR_LABELS[SEL_COUNT] = {
    "随机", "远程飞兵高速优先", "数量最多", "失血比例", "失血数值",
};

// 运行时标签
