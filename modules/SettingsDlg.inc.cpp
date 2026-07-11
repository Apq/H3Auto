// ========== SettingsDlg.inc.cpp ==========
// 战场自动化 - 设置面板
// 渲染：LoHook 0x600430 画面板到 screenPcx16
// 输入捕获：窗口子类化

#define o_WndMgr (*reinterpret_cast<H3WindowManager**>(0x6992D0))

// ========================================================================
// 第一部分：数据声明
// ========================================================================

static const int PANEL_W    = 680;
static const int PANEL_H    = 380;
static const int COLS       = 4;
static const int CELL_W     = 158;
static const int CELL_H     = 60;
static const int GAP_X      = 8;
static const int GAP_Y      = 4;
static const int MARGIN     = 20;
static const int TITLE_H    = 28;
static const int BTN_W      = 80;
static const int BTN_H      = 24;
static const int BTN_Y      = PANEL_H - 44;
static const int OK_X       = PANEL_W - MARGIN - BTN_W;
static const int CANCEL_X   = OK_X - BTN_W - 10;
static const int CELL_COUNT = COLS * 3;

static const char* g_action_labels[] = {
    "手动", "防御", "近战攻击", "随机射击", "顺序射击", "循环移动",
};
static const char* g_target_labels[] = {
    "无", "指定位置", "远程和高速优先", "数量优先",
};

static const INT32 COL_TITLE_TEXT  = 0x1D;
static const INT32 COL_TEXT        = 0x01;
static const INT32 COL_ACTION_TEXT = 0x1A;
static const INT32 COL_TARGET_TEXT = 0x0D;
static const INT32 COL_OK_TEXT     = 0x0D;
static const INT32 COL_CANCEL_TEXT = 0x1B;

static struct Panel {
    bool active;
    int x, y;
    int action[21];
    int target[21];
    int stack_idx[CELL_COUNT];
    int count;
} s_p = {};

static bool s_subclass_installed = false;

// ========================================================================
// 第二部分：前向声明（所有被 SubclassWndProc 调用的函数）
// ========================================================================

static void DrawPanelToBuffer();
static void CloseSettingsPanel();
static bool HandlePanelClick(int sx, int sy);
static void OpenSettingsPanel();

// ========================================================================
// 第三部分：工具函数
// ========================================================================

static H3Font* GetPanelFont() { return H3Font::Load("bigfont.fnt"); }
static H3Font* GetSmallFont() { return H3Font::Load("smalfont.fnt"); }

static RECT CellRect(int idx)
{
    RECT rc = {};
    if (idx < 0 || idx >= CELL_COUNT) return rc;
    rc.left   = MARGIN + (idx % COLS) * (CELL_W + GAP_X);
    rc.top    = TITLE_H + MARGIN + (idx / COLS) * (CELL_H + GAP_Y);
    rc.right  = rc.left + CELL_W;
    rc.bottom = rc.top + CELL_H;
    return rc;
}

static RECT ActionBtnRect(int idx)
{
    RECT rc = CellRect(idx);
    rc.left += 4; rc.right -= 4;
    rc.top   = rc.bottom - 16;
    return rc;
}

static RECT TargetBtnRect(int idx)
{
    RECT rc = CellRect(idx);
    rc.left += 4; rc.right -= 4;
    rc.top   = rc.bottom - 14;
    return rc;
}

static void Fill(H3LoadedPcx16* scr, int x, int y, int w, int h, int r, int g, int b)
{
    if (w <= 0 || h <= 0) return;
    scr->FillRectangle(x, y, w, h, (BYTE)r, (BYTE)g, (BYTE)b);
}

static void DrawTxt(H3LoadedPcx16* scr, H3Font* fnt, const char* text,
    int x, int y, int w, int h, INT32 color,
    eTextAlignment align = eTextAlignment::MIDDLE_CENTER)
{
    if (!fnt || !text || w <= 0 || h <= 0) return;
    scr->TextDraw(fnt, text, x, y, w, h, (eTextColor)color, align);
}

// ========================================================================
// 第四部分：窗口子类（必须放在所有被调用函数声明之后）
// ========================================================================

// 前向声明（供 TryInstallSubclassOnce 使用）
static LRESULT CALLBACK SubclassWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void TryInstallSubclassOnce()
{
    // 暂时禁用子类化，诊断游戏冻结问题
    return;
    if (s_subclass_installed) return;
    HWND hwnd = H3Hwnd::Get();
    if (!hwnd || !IsWindow(hwnd)) return;
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)SubclassWndProc);
    s_subclass_installed = true;
    WriteLog("[Subclass] 窗口子类已注册 hwnd=0x%X。", (UINT_PTR)hwnd);
}

static LRESULT CALLBACK SubclassWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_RBUTTONUP && !s_p.active) {
        WriteLog("[Subclass] WM_RBUTTONUP at (%d,%d)", LOWORD(lParam), HIWORD(lParam));
        OpenSettingsPanel();
        return 0;
    }
    if (s_p.active && msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        CloseSettingsPanel();
        WriteLog("[Subclass] ESC, panel closed.");
        return 0;
    }
    if (s_p.active && msg == WM_LBUTTONDOWN) {
        if (HandlePanelClick(LOWORD(lParam), HIWORD(lParam))) return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ========================================================================
// 第五部分：LoHook
// ========================================================================

INT __stdcall Hook_BltComplete(LoHook* h, HookContext* c)
{
    (void)h; (void)c;
    TryInstallSubclassOnce();
    if (s_p.active) DrawPanelToBuffer();
    return EXEC_DEFAULT;
}

// ========================================================================
// 第六部分：绘制
// ========================================================================

static void DrawPanelToBuffer()
{
    if (!s_p.active) return;
    H3LoadedPcx16* scr = o_WndMgr ? o_WndMgr->screenPcx16 : nullptr;
    if (!scr) return;

    int px = s_p.x, py = s_p.y;

    Fill(scr, px, py, PANEL_W, PANEL_H, 20, 20, 60);
    scr->DrawFrame(px, py, PANEL_W, PANEL_H, (BYTE)80, (BYTE)80, (BYTE)140);
    Fill(scr, px + 1, py + 1, PANEL_W - 2, TITLE_H - 1, 40, 40, 100);
    DrawTxt(scr, GetPanelFont(), "部队自动行动设置",
        px + 4, py + 3, PANEL_W - 8, TITLE_H - 4, COL_TITLE_TEXT, eTextAlignment::MIDDLE_LEFT);

    H3Font* fntS = GetSmallFont();
    for (int i = 0; i < s_p.count && i < CELL_COUNT; ++i) {
        int si = s_p.stack_idx[i];
        if (si < 0) continue;
        RECT cRc = CellRect(i);
        RECT aRc = ActionBtnRect(i);
        RECT tRc = TargetBtnRect(i);

        Fill(scr, cRc.left, cRc.top, CELL_W, CELL_H, 10, 30, 70);
        scr->DrawFrame(cRc.left, cRc.top, CELL_W, CELL_H, (BYTE)50, (BYTE)80, (BYTE)160);

        char coord[16] = "--";
        if (o_BattleMgr && si >= 0 && si < 21) {
            int hx = o_BattleMgr->stack[0][si].hex_ix;
            if (hx >= 0 && hx < 40)
                _snprintf(coord, sizeof(coord), "%c%02d", 'A' + (hx % 8), hx / 8 + 1);
        }
        DrawTxt(scr, fntS, coord, cRc.left + 4, cRc.top + 2, CELL_W - 8, 16,
            COL_TEXT, eTextAlignment::TOP_LEFT);

        Fill(scr, aRc.left, aRc.top, aRc.right - aRc.left, aRc.bottom - aRc.top, 30, 50, 100);
        DrawTxt(scr, fntS, g_action_labels[s_p.action[si]],
            aRc.left, aRc.top, aRc.right - aRc.left, aRc.bottom - aRc.top,
            COL_ACTION_TEXT, eTextAlignment::MIDDLE_CENTER);

        Fill(scr, tRc.left, tRc.top, tRc.right - tRc.left, tRc.bottom - tRc.top, 25, 40, 80);
        DrawTxt(scr, fntS, g_target_labels[s_p.target[si]],
            tRc.left, tRc.top, tRc.right - tRc.left, tRc.bottom - tRc.top,
            COL_TARGET_TEXT, eTextAlignment::MIDDLE_CENTER);
    }

    Fill(scr, px + OK_X, py + BTN_Y, BTN_W, BTN_H, 0, 100, 0);
    DrawTxt(scr, GetSmallFont(), "确定", px + OK_X, py + BTN_Y, BTN_W, BTN_H,
        COL_OK_TEXT, eTextAlignment::MIDDLE_CENTER);
    Fill(scr, px + CANCEL_X, py + BTN_Y, BTN_W, BTN_H, 100, 0, 0);
    DrawTxt(scr, GetSmallFont(), "取消", px + CANCEL_X, py + BTN_Y, BTN_W, BTN_H,
        COL_CANCEL_TEXT, eTextAlignment::MIDDLE_CENTER);
}

void RefreshSettingsPanel() { if (s_p.active) DrawPanelToBuffer(); }

// ========================================================================
// 第七部分：面板控制
// ========================================================================

void OpenSettingsPanel()
{
    s_p.active = true;
    s_p.count  = 0;
    memset(s_p.action, 0, sizeof(s_p.action));
    memset(s_p.target, 0, sizeof(s_p.target));

    if (o_WndMgr && o_WndMgr->screenPcx16) {
        s_p.x = (o_WndMgr->screenPcx16->width  - PANEL_W) / 2;
        s_p.y = (o_WndMgr->screenPcx16->height - PANEL_H) / 2;
    } else {
        s_p.x = (800 - PANEL_W) / 2; s_p.y = (600 - PANEL_H) / 2;
    }
    if (s_p.x < 0) s_p.x = 0; if (s_p.y < 0) s_p.y = 0;

    if (o_BattleMgr) {
        for (int i = 0; i < 21 && s_p.count < CELL_COUNT; ++i) {
            if (o_BattleMgr->stack[0][i].count_current > 0) {
                s_p.stack_idx[s_p.count] = i;
                s_p.action[i] = g_action_strategies[i];
                s_p.target[i] = g_target_strategies[i];
                ++s_p.count;
            }
        }
    }
    DrawPanelToBuffer();
    WriteLog("[Panel] 打开设置面板 count=%d at (%d,%d)", s_p.count, s_p.x, s_p.y);
}

void CloseSettingsPanel()
{
    if (!s_p.active) return;
    s_p.active = false;
    WriteLog("[Panel] 设置面板已关闭。");
}

bool IsPanelActive() { return s_p.active; }

// ========================================================================
// 第八部分：点击处理
// ========================================================================

bool HandlePanelClick(int sx, int sy)
{
    if (!s_p.active) return false;
    int px = sx - s_p.x, py = sy - s_p.y;

    if (px >= OK_X && px < OK_X + BTN_W && py >= BTN_Y && py < BTN_Y + BTN_H) {
        for (int i = 0; i < s_p.count && i < CELL_COUNT; ++i) {
            int si = s_p.stack_idx[i];
            if (si >= 0 && si < 21) {
                g_action_strategies[si] = s_p.action[si];
                g_target_strategies[si] = s_p.target[si];
            }
        }
        extern void CommitStrategies(int side, int* actions, int* targets);
        CommitStrategies(0, g_action_strategies, g_target_strategies);
        WriteLog("[Panel] OK");
        CloseSettingsPanel();
        return true;
    }
    if (px >= CANCEL_X && px < CANCEL_X + BTN_W && py >= BTN_Y && py < BTN_Y + BTN_H) {
        WriteLog("[Panel] Cancel");
        CloseSettingsPanel();
        return true;
    }
    for (int i = 0; i < s_p.count && i < CELL_COUNT; ++i) {
        int si = s_p.stack_idx[i];
        if (si < 0) continue;
        RECT aRc = ActionBtnRect(i);
        RECT tRc = TargetBtnRect(i);
        if (px >= aRc.left && px < aRc.right && py >= aRc.top && py < aRc.bottom) {
            WriteLog("[Panel] 点击行动按钮 stack=%d", si); return true;
        }
        if (px >= tRc.left && px < tRc.right && py >= tRc.top && py < tRc.bottom) {
            WriteLog("[Panel] 点击目标按钮 stack=%d", si); return true;
        }
    }
    return false;
}
