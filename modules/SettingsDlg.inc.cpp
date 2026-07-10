// ========== SettingsDlg.inc.cpp ==========
// 战场自动化 - 设置对话框
// 简化版：4列 × 3行，每格含生物图标 + 位置/数量 + 行动策略 + 目标策略

static void WriteLog(const char* fmt, ...);

// ============================================================
// 策略枚举（与 AutoExecute.inc.cpp 保持一致）
// ============================================================
static const char* g_action_labels[] = {
    "手动", "防御", "近战攻击", "随机射击", "顺序射击", "循环移动",
};

static const char* g_target_labels[] = {
    "无", "指定位置", "远程和高速优先", "数量优先",
};

// ============================================================
// 布局常量
// ============================================================
static const int DLG_W       = 700;
static const int DLG_H       = 440;
static const int COL_COUNT   = 4;
static const int ROW_COUNT   = 3;
static const int CELL_W      = 160;
static const int CELL_H      = 72;
static const int CELL_GAP_X  = 8;
static const int CELL_GAP_Y  = 4;
static const int TABLE_X     = 20;
static const int TABLE_Y     = 36;
static const int TITLE_H     = 28;
static const int BTN_W       = 80;
static const int BTN_H       = 24;
static const int BTN_Y       = DLG_H - 44;
static const int CANCEL_X    = DLG_W - BTN_W * 2 - 30;
static const int OK_X        = DLG_W - BTN_W - 15;
static const int ID_CELL_MAX = 12;

// ============================================================
// 工具函数
// ============================================================
// 战区坐标转换：hex_ix → "A01" ~ "H15"
static void HexIxToCoord(int hex_ix, char* out)
{
    if (hex_ix < 0 || hex_ix >= 40) { strcpy(out, "--"); return; }
    int col_ix = hex_ix % 8;
    int row_ix = hex_ix / 8 + 1;
    if (col_ix < 0) col_ix = 0;
    if (col_ix > 7) col_ix = 7;
    if (row_ix < 1) row_ix = 1;
    if (row_ix > 15) row_ix = 15;
    char col_c = 'A' + col_ix;
    _snprintf(out, 16, "%c%02d", col_c, row_ix);
}

// ============================================================
// 直接读文件解码 PCX（绕过游戏资源管理器，支持中文路径）
// 参考 BattleValueInfo 的实现
// ============================================================
static H3LoadedPcx16* LoadPcxFromFile(const char* absPath)
{
    unsigned char* data = nullptr;
    int fileSize = 0;

    HANDLE h = CreateFileA(absPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        WriteLog("[SettingsDlg] CreateFileA failed: %s", absPath);
        return nullptr;
    }
    DWORD sz = GetFileSize(h, nullptr);
    if (!sz || sz > 32 * 1024 * 1024) {
        CloseHandle(h);
        WriteLog("[SettingsDlg] Invalid file size: %u", sz);
        return nullptr;
    }
    data = (unsigned char*)malloc(sz);
    if (!data) { CloseHandle(h); return nullptr; }
    DWORD read = 0;
    bool ok = ReadFile(h, data, sz, &read, nullptr) && read == sz;
    CloseHandle(h);
    if (!ok) { free(data); return nullptr; }
    fileSize = (int)sz;

    // 解析 PCX 头部
    int bpp = data[3];
    int xmin = *(short*)(data + 4);
    int ymin = *(short*)(data + 6);
    int xmax = *(short*)(data + 8);
    int ymax = *(short*)(data + 10);
    int nplanes = data[65];
    int bpl = *(short*)(data + 66);
    int w = xmax - xmin + 1;
    int h2 = ymax - ymin + 1;
    if (w <= 0 || h2 <= 0 || w > 4096 || h2 > 4096) {
        free(data); return nullptr;
    }
    if (!(nplanes == 1 && bpp == 8)) {
        free(data); return nullptr;
    }

    // 解码 RLE
    int rawSize = bpl * h2;
    unsigned char* raw = (unsigned char*)malloc(rawSize);
    if (!raw) { free(data); return nullptr; }
    int pos = 128, rp = 0;
    while (rp < rawSize && pos < fileSize) {
        unsigned char b = data[pos++];
        if ((b & 0xC0) == 0xC0) {
            int cnt = b & 0x3F;
            if (pos >= fileSize) break;
            unsigned char val = data[pos++];
            for (int i = 0; i < cnt && rp < rawSize; ++i) raw[rp++] = val;
        } else {
            raw[rp++] = b;
        }
    }
    if (rp < rawSize) { free(raw); free(data); return nullptr; }

    // 读调色板
    unsigned char pal[768] = {0};
    bool palFound = false;
    if (fileSize >= 769 && data[fileSize - 769] == 0x0C) {
        memcpy(pal, data + (fileSize - 768), 768);
        palFound = true;
    }
    if (!palFound) {
        for (int i = fileSize - 769; i >= 0 && !palFound; --i) {
            if (data[i] == 0x0C && i + 768 < fileSize) {
                memcpy(pal, data + i + 1, 768);
                palFound = true;
            }
        }
    }
    free(data);
    if (!palFound) { free(raw); return nullptr; }

    // 创建 H3LoadedPcx16 并填充像素
    H3LoadedPcx16* pcx = H3LoadedPcx16::Create(w, h2);
    if (!pcx || !pcx->buffer) { free(raw); return nullptr; }

    if (H3BitMode::Get() == 4) {
        for (int y = 0; y < h2; ++y) {
            _dword_* row = (_dword_*)(pcx->buffer + y * pcx->scanlineSize);
            _byte_* s = raw + y * bpl;
            for (int x = 0; x < w; ++x) {
                unsigned char idx = s[x];
                row[x] = (0xFFu << 24)
                       | ((unsigned)pal[idx * 3]     << 16)
                       | ((unsigned)pal[idx * 3 + 1] <<  8)
                       |  (unsigned)pal[idx * 3 + 2];
            }
        }
    } else {
        for (int y = 0; y < h2; ++y) {
            _word_* row = (_word_*)(pcx->buffer + y * pcx->scanlineSize);
            _byte_* s = raw + y * bpl;
            for (int x = 0; x < w; ++x) {
                unsigned char idx = s[x];
                int r = pal[idx * 3];
                int g = pal[idx * 3 + 1];
                int b2 = pal[idx * 3 + 2];
                row[x] = (_word_)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b2 >> 3));
            }
        }
    }
    free(raw);
    WriteLog("[SettingsDlg] PCX loaded: %dx%d mode=%s", w, h2,
        H3BitMode::Get() == 4 ? "32bpp" : "16bpp");
    return pcx;
}

// ============================================================
// 外部声明
// ============================================================
extern int  g_action_strategies[21];
extern int  g_target_strategies[21];
extern void CommitStrategies(int side, int* actions, int* targets);

// ============================================================
// 下拉列表：用 Win32 ListBox + 嵌套消息循环，同步等待选择
// ============================================================
static HWND GetGameWindow()
{
    return GetForegroundWindow();
}

static INT DoModalListBox(H3Dlg* parent, int abs_x, int abs_y, int w, int h,
    const char* const* items, int count, int selected)
{
    HWND hwndParent = GetGameWindow();
    if (!hwndParent) return -1;

    static ATOM s_atom = 0;
    if (!s_atom) {
        WNDCLASSA wc = {};
        wc.style = CS_SAVEBITS | CS_DBLCLKS;
        wc.lpfnWndProc = DefWindowProcA;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "H3A_DropList";
        s_atom = RegisterClassA(&wc);
    }

    DWORD listStyle = WS_POPUP | WS_BORDER | WS_VISIBLE | LBS_NOTIFY |
                      LBS_WANTKEYBOARDINPUT | WS_VSCROLL | WS_SYSMENU;
    HWND hwndList = CreateWindowExA(
        WS_EX_TOOLWINDOW, "H3A_DropList", "", listStyle,
        abs_x, abs_y, w, h,
        hwndParent, nullptr, GetModuleHandle(nullptr), nullptr
    );
    if (!hwndList) return -1;

    HFONT hFontOld = (HFONT)SendMessage(hwndParent, WM_GETFONT, 0, 0);
    if (hFontOld) SendMessage(hwndList, WM_SETFONT, (WPARAM)hFontOld, 0);

    for (int i = 0; i < count; ++i) {
        int idx = SendMessageA(hwndList, LB_ADDSTRING, 0, (LPARAM)items[i]);
        SendMessage(hwndList, LB_SETITEMDATA, idx, i);
    }
    SendMessage(hwndList, LB_SETCURSEL, selected, 0);
    SetForegroundWindow(hwndList);
    SetFocus(hwndList);

    INT result = -1;
    MSG msg;
    BOOL done = FALSE;
    while (!done && GetMessageA(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_ESCAPE) {
                result = -1; done = TRUE;
            } else if (msg.wParam == VK_RETURN) {
                int sel = SendMessage(hwndList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) result = (int)SendMessage(hwndList, LB_GETITEMDATA, sel, 0);
                done = TRUE;
            } else {
                TranslateMessage(&msg); DispatchMessageA(&msg);
            }
        } else if (msg.message == WM_CHAR) {
            TranslateMessage(&msg); DispatchMessageA(&msg);
        } else if (msg.message == WM_LBUTTONUP) {
            POINT pt = { LOWORD(msg.lParam), HIWORD(msg.lParam) };
            RECT rc; GetWindowRect(hwndList, &rc);
            if (pt.x >= 0 && pt.x < rc.right - rc.left &&
                pt.y >= 0 && pt.y < rc.bottom - rc.top) {
                int idx = SendMessage(hwndList, LB_ITEMFROMPOINT, 0, msg.lParam);
                if (HIWORD(idx) == 0) {
                    int item = (int)SendMessage(hwndList, LB_GETITEMDATA, LOWORD(idx), 0);
                    result = item; done = TRUE;
                }
            } else { result = -1; done = TRUE; }
        } else if (msg.message == WM_MOUSEMOVE && (msg.wParam & MK_LBUTTON)) {
            POINT pt = { LOWORD(msg.lParam), HIWORD(msg.lParam) };
            int idx = SendMessage(hwndList, LB_ITEMFROMPOINT, 0, msg.lParam);
            if (HIWORD(idx) == 0) {
                int curr = LOWORD(idx);
                int prev = SendMessage(hwndList, LB_GETCURSEL, 0, 0);
                if (prev != curr) SendMessage(hwndList, LB_SETCURSEL, curr, 0);
            }
        } else if (msg.message == WM_DESTROY || msg.message == WM_CLOSE) {
            done = TRUE;
        } else {
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
    }
    DestroyWindow(hwndList);
    return result;
}

// ============================================================
// 设置对话框类
// ============================================================
class SettingsDlg : public H3Dlg {
public:
    SettingsDlg();
    ~SettingsDlg();

    virtual BOOL  OnLeftClick(INT itemId, H3Msg& msg) override;
    virtual BOOL  OnRightClick(H3DlgItem* it) override;
    virtual BOOL  OnKeyPress(eVKey key, eMsgFlag flag) override;
    virtual VOID  OnOK() override;
    virtual VOID  OnCancel() override;

    static INT __fastcall BtnActionProc(H3Msg* msg);
    static INT __fastcall BtnTargetProc(H3Msg* msg);
    static SettingsDlg* s_instance;

    struct Cell {
        int                 stack_idx;
        int                 id_icon;
        int                 id_action;
        int                 id_target;
        H3DlgDef*           def_icon;
        H3DlgText*          txt_info;
        H3DlgCustomButton*  btn_action;
        H3DlgCustomButton*  btn_target;
        ActionStrategy      action_val;
        TargetStrategy      target_val;
    };
    Cell m_cells[ID_CELL_MAX];
    int  m_battle_side;
    int  m_stack_count;
    int  m_row_count;

private:
    void BuildCells();
    void RefreshCell(int idx);
    void ShowActionPopup(int cellIdx);
    void ShowTargetPopup(int cellIdx);
};

SettingsDlg* SettingsDlg::s_instance = nullptr;

// ============================================================
void ShowSettingsDlg()
{
    SettingsDlg dlg;
    dlg.Start();
}

// ============================================================
// 构造函数
// ============================================================
SettingsDlg::SettingsDlg()
    : H3Dlg(DLG_W, DLG_H, -1, -1, FALSE, FALSE, 0)
{
    s_instance = this;

    // 用直接读文件方式加载背景图（绕过游戏资源管理器，支持中文路径）
    char modulePath[MAX_PATH];
    GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
    char* slash = strrchr(modulePath, '\\');
    if (slash) slash[1] = 0;
    char bgPath[1024];
    _snprintf(bgPath, sizeof(bgPath) - 1,
        "%simg\\HA_bg.pcx", modulePath);
    bgPath[sizeof(bgPath) - 1] = 0;
    WriteLog("[SettingsDlg] Loading background: %s", bgPath);

    H3LoadedPcx16* bg = LoadPcxFromFile(bgPath);
    if (bg) {
        AddBackground(bg);
        WriteLog("[SettingsDlg] Background added OK");
    } else {
        AddBackground(TRUE, FALSE, 2);
        WriteLog("[SettingsDlg] Background load failed, using solid color");
    }

    // 标题
    H3DlgText* title = H3DlgText::Create(
        0, 4, DLG_W, TITLE_H,
        "部队自动行动设置",
        NH3Dlg::Text::MEDIUM,
        NH3Dlg::eTextColor::YELLOW,
        -1,
        NH3Dlg::eTextAlignment::MIDDLE_CENTER
    );
    if (title) AddItem(title);

    // 判断己方阵营
    if (!o_BattleMgr) {
        m_stack_count = 0;
        m_battle_side = 0;
    } else {
        m_battle_side = o_BattleMgr->hero[0] ? 0 : (o_BattleMgr->hero[1] ? 1 : 0);
        m_stack_count = 0;
        for (int i = 0; i < 21; ++i) {
            if (o_BattleMgr->stack[m_battle_side][i].count_current > 0 &&
                o_BattleMgr->stack[m_battle_side][i].count_at_start > 0) {
                ++m_stack_count;
            }
        }
    }
    m_row_count = (m_stack_count + COL_COUNT - 1) / COL_COUNT;
    if (m_row_count > ROW_COUNT) m_row_count = ROW_COUNT;
    if (m_row_count < 1) m_row_count = 1;

    WriteLog("[SettingsDlg] side=%d count=%d rows=%d", m_battle_side, m_stack_count, m_row_count);

    memset(m_cells, 0, sizeof(m_cells));
    for (int i = 0; i < ID_CELL_MAX; ++i) {
        m_cells[i].stack_idx  = -1;
        m_cells[i].action_val = AS_MANUAL;
        m_cells[i].target_val = TS_NONE;
    }

    BuildCells();

    H3DlgDefButton* btnCancel = CreateCancelButton(CANCEL_X, BTN_Y);
    if (btnCancel) btnCancel->AddHotkey(NH3VKey::H3VK_ESCAPE);
    H3DlgDefButton* btnOK = CreateOKButton(OK_X, BTN_Y);
    if (btnOK) btnOK->AddHotkey(NH3VKey::H3VK_ENTER);
}

SettingsDlg::~SettingsDlg()
{
    s_instance = nullptr;
}

// ============================================================
void SettingsDlg::BuildCells()
{
    int indices[21];
    int n = 0;
    for (int i = 0; i < 21 && n < 21; ++i) {
        if (o_BattleMgr && o_BattleMgr->stack[m_battle_side][i].count_current > 0) {
            indices[n++] = i;
        }
    }

    int total_h = m_row_count * CELL_H + (m_row_count - 1) * CELL_GAP_Y;
    int table_y = TABLE_Y;
    int available = DLG_H - BTN_Y - BTN_H - 20;
    if (total_h < available)
        table_y = TABLE_Y + (available - total_h) / 2;

    int cellIdx = 0;
    for (int row = 0; row < m_row_count && cellIdx < ID_CELL_MAX; ++row) {
        for (int col = 0; col < COL_COUNT && cellIdx < ID_CELL_MAX; ++col) {
            int stack_idx = (cellIdx < n) ? indices[cellIdx] : -1;
            Cell& c = m_cells[cellIdx];
            c.stack_idx = stack_idx;

            int cell_x = TABLE_X + col * (CELL_W + CELL_GAP_X);
            int cell_y = table_y + row * (CELL_H + CELL_GAP_Y);

            if (stack_idx >= 0) {
                _BattleStack_* stk = &o_BattleMgr->stack[m_battle_side][stack_idx];

                if (stack_idx >= 0 && stack_idx < 21) {
                    c.action_val = (ActionStrategy)g_action_strategies[stack_idx];
                    c.target_val = (TargetStrategy)g_target_strategies[stack_idx];
                }

                char defName[64];
                _snprintf(defName, sizeof(defName), "C%03d.DEF", stk->creature_id);
                c.def_icon = CreateDef(
                    cell_x + (CELL_W - 36) / 2, cell_y,
                    36, 36, 100 + cellIdx,
                    defName, 0, 0, 0, FALSE
                );
                if (c.def_icon) AddItem(c.def_icon);

                char coord[16];
                HexIxToCoord(stk->hex_ix, coord);
                char info[64];
                _snprintf(info, sizeof(info), "%s  ×%d", coord, stk->count_current);
                c.txt_info = H3DlgText::Create(
                    cell_x, cell_y + 38, CELL_W, 14,
                    info,
                    NH3Dlg::Text::TINY,
                    NH3Dlg::eTextColor::WHITE,
                    -1,
                    NH3Dlg::eTextAlignment::MIDDLE_CENTER
                );
                if (c.txt_info) AddItem(c.txt_info);

                c.btn_action = CreateCustomButton(
                    cell_x + 4, cell_y + 54,
                    CELL_W - 8, 8,
                    200 + cellIdx,
                    NH3Dlg::Assets::DLGBOX,
                    (H3DlgButton_proc)&BtnActionProc,
                    0, 2
                );
                if (c.btn_action) {
                    c.btn_action->SetHint(g_action_labels[c.action_val]);
                    AddItem(c.btn_action);
                }

                c.btn_target = CreateCustomButton(
                    cell_x + 4, cell_y + 63,
                    CELL_W - 8, 8,
                    300 + cellIdx,
                    NH3Dlg::Assets::DLGBOX,
                    (H3DlgButton_proc)&BtnTargetProc,
                    0, 2
                );
                if (c.btn_target) {
                    c.btn_target->SetHint(g_target_labels[c.target_val]);
                    AddItem(c.btn_target);
                }
            }
            ++cellIdx;
        }
    }
}

void SettingsDlg::RefreshCell(int idx)
{
    if (idx < 0 || idx >= ID_CELL_MAX) return;
    Cell& c = m_cells[idx];
    if (c.btn_action) c.btn_action->SetHint(g_action_labels[c.action_val]);
    if (c.btn_target) c.btn_target->SetHint(g_target_labels[c.target_val]);
}

// ============================================================
// 按钮回调
// ============================================================
static INT32 GetMsgItemId(H3Msg* msg)
{
    if (!msg) return -1;
    return *(INT32*)((BYTE*)msg + 0x08);
}

INT __fastcall SettingsDlg::BtnActionProc(H3Msg* msg)
{
    if (!msg || !s_instance) return FALSE;
    if (msg->IsLeftClick()) {
        INT32 itemId = GetMsgItemId(msg);
        for (int i = 0; i < ID_CELL_MAX; ++i) {
            if (itemId == 200 + i) {
                s_instance->ShowActionPopup(i);
                return TRUE;
            }
        }
    }
    return FALSE;
}

INT __fastcall SettingsDlg::BtnTargetProc(H3Msg* msg)
{
    if (!msg || !s_instance) return FALSE;
    if (msg->IsLeftClick()) {
        INT32 itemId = GetMsgItemId(msg);
        for (int i = 0; i < ID_CELL_MAX; ++i) {
            if (itemId == 300 + i) {
                s_instance->ShowTargetPopup(i);
                return TRUE;
            }
        }
    }
    return FALSE;
}

// ============================================================
void SettingsDlg::ShowActionPopup(int cellIdx)
{
    if (cellIdx < 0 || cellIdx >= ID_CELL_MAX) return;
    Cell& c = m_cells[cellIdx];
    if (c.stack_idx < 0 || !c.btn_action) return;

    INT sel = DoModalListBox(this,
        c.btn_action->GetAbsoluteX(),
        c.btn_action->GetAbsoluteY() + c.btn_action->GetHeight() + 1,
        c.btn_action->GetWidth(),
        (AS_CYCLE_MOVE + 1) * 14,
        g_action_labels, AS_CYCLE_MOVE + 1, c.action_val);
    if (sel >= 0 && sel <= AS_CYCLE_MOVE) {
        c.action_val = (ActionStrategy)sel;
        RefreshCell(cellIdx);
    }
}

void SettingsDlg::ShowTargetPopup(int cellIdx)
{
    if (cellIdx < 0 || cellIdx >= ID_CELL_MAX) return;
    Cell& c = m_cells[cellIdx];
    if (c.stack_idx < 0 || !c.btn_target) return;

    INT sel = DoModalListBox(this,
        c.btn_target->GetAbsoluteX(),
        c.btn_target->GetAbsoluteY() + c.btn_target->GetHeight() + 1,
        c.btn_target->GetWidth(),
        (TS_COUNT_PRI + 1) * 14,
        g_target_labels, TS_COUNT_PRI + 1, c.target_val);
    if (sel >= 0 && sel <= TS_COUNT_PRI) {
        c.target_val = (TargetStrategy)sel;
        RefreshCell(cellIdx);
    }
}

// ============================================================
BOOL SettingsDlg::OnLeftClick(INT itemId, H3Msg& msg)
{
    (void)msg;
    return H3Dlg::OnLeftClick(itemId, msg);
}

BOOL SettingsDlg::OnRightClick(H3DlgItem* it)
{
    (void)it;
    return FALSE;
}

BOOL SettingsDlg::OnKeyPress(eVKey key, eMsgFlag flag)
{
    if (key == NH3VKey::H3VK_ESCAPE) { Stop(); return TRUE; }
    if (key == NH3VKey::H3VK_ENTER)  { OnOK();   return TRUE; }
    return H3Dlg::OnKeyPress(key, flag);
}

VOID SettingsDlg::OnOK()
{
    WriteLog("[SettingsDlg] OK");
    int actions[21] = {0};
    int targets[21] = {0};
    for (int i = 0; i < ID_CELL_MAX; ++i) {
        if (m_cells[i].stack_idx >= 0 && m_cells[i].stack_idx < 21) {
            actions[m_cells[i].stack_idx] = m_cells[i].action_val;
            targets[m_cells[i].stack_idx] = m_cells[i].target_val;
        }
    }
    CommitStrategies(m_battle_side, actions, targets);
    Stop();
}

VOID SettingsDlg::OnCancel()
{
    WriteLog("[SettingsDlg] Cancel");
    Stop();
}
