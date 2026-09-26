// PanelLayout.hpp - 设置面板布局常量（SettingsDlg / PanelGfx / PanelDraw 共用）。
#pragma once

static const int PANEL_W    = 680;
// 底部状态栏 +26，背景图 HA_bg.pcx 同步为 680×528。
static const int PANEL_H    = 528;
static const int COLS       = 1;   // 一行一格（单列宽格）
static const int VISIBLE_ROWS = 3; // 金框内刚好 3 行
static const int CELL_W     = 464; // 多级导航：左侧 Tab 条+竖线后内容区收窄
// 卡片高度 110 不变：金框 514×342，底边 y=414，表格上方不再放设置行。
// SCROLL_H = CELL_H + 2*(CELL_H-2) = 110 + 2*108 = 326。
static const int CELL_H     = 110;
static const int CELL_STEP_X = CELL_W - 2;
static const int CELL_STEP_Y = CELL_H - 2;
// 网格金框：宽 514，高 342（右缘 x=653 与旧版一致，左侧让位给 Tab 条）。
static const int GRID_FRAME_W = 514;
static const int GRID_FRAME_H = 342;
static const int GRID_FRAME_X = 139;
static const int GRID_FRAME_Y = 94;
static const int GRID_X      = GRID_FRAME_X + 12;
static const int GRID_Y      = 102;
static const int SCROLL_X    = GRID_FRAME_X + GRID_FRAME_W - 26; // 右对齐金框
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

// ---- 多级导航：左侧 Tab 条 + 金框色分隔竖线（方案行以下、确定/取消以上）----
enum {
    PAGE_ARMY    = 0,   // 部队：21 槽卡片表
    PAGE_PROFILE = 1,   // 方案：方案级设置（保活策略/自动停止）
    PAGE_COUNT   = 2,
};
static const int TAB_X       = 17;   // Tab 条左缘
static const int TAB_W       = 108;  // Tab 条总宽
static const int TAB_ITEM_W  = 100;
static const int TAB_ITEM_H  = 24;
static const int TAB_GAP     = 8;
static const int TAB_FIRST_Y = 80;
static const int TAB_SEP_X   = 119;  // 分隔竖线（2px 金框色）
static const int TAB_SEP_Y0  = 67;   // 与方案行下横线衔接
static const int TAB_SEP_Y1  = 441;  // 与按钮上横线衔接
// 两条横向金线（2px，与竖线同色）：上线接竖线上端、下线接竖线下端，
// 框住 Tab 条与内容区。
static const int TAB_HLINE_X0 = 17;
static const int TAB_HLINE_X1 = GRID_FRAME_X + GRID_FRAME_W + 10;
static const int TAB_HLINE_Y  = TAB_SEP_Y0;  // 上线：接竖线上端
static const int TAB_HLINE2_Y = TAB_SEP_Y1;  // 下线：接竖线下端（确定/取消上方）

// 方案行与金框之间的保活策略行（方案级）：label + 下拉框。
static const int PROTECT_DD_Y        = 76;   // 方案行下横线(67..68)之下
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

// 保活策略项文案已外置（UiTexts panel.protect_opt0..3）。

// 硬编码默认标签（INI 加载失败时使用）
static const char* DEFAULT_ACTION_LABELS[AA_COUNT] = {
    "手动", "防御", "等待", "循环移动", "循环近战", "远程攻击", "急救治疗",
};
static const char* DEFAULT_SELECTOR_LABELS[SEL_COUNT] = {
    "随机", "远程飞兵高速优先", "数量最多", "失血比例", "失血数值",
};

// 运行时标签
