// MagicChannelUi.inc.cpp - 攻击魔法页（PAGE_ATTACK）攻击行、下拉、滚动及战场位置编辑。
// 在 SettingsDlg 的 HidePanelForPick_ 之后包含；所有配置只修改草稿。
static int s_attack_first = 0;
#include "MagicChannelInputCore.hpp"
static H3AutoPolicy::MagicChannelGesture s_attack_gesture = {};
static int s_attack_dd_row = -1;
static int s_attack_dd_kind = -1; // 0=法术 1=单体目标（范围落点走直接拾取，无浮层）
static int s_attack_dd_first = 0;
static int s_attack_dd_hover = -1;
static int s_attack_dd_ids[70] = {};
static int s_attack_dd_count = 0;

static void CancelAttackMagicGesture_() {
    H3AutoPolicy::CancelMagicChannelGesture(&s_attack_gesture);
}

// 攻击行索引 → 视觉行/大列。两个攻击项并排成一行（左右两大列）。
static int AttackMagicCol_(int row) { return row % 2; }
static int AttackMagicColX_(int col) {
    return col == 0 ? GRID_FRAME_X
                    : GRID_FRAME_X + MAGIC_ATT_COL_W + MAGIC_ATT_COL_GAP;
}
static int AttackMagicSpellX_(int row) {
    return AttackMagicColX_(AttackMagicCol_(row));
}
static int AttackMagicCtrlX_(int row) {
    return AttackMagicSpellX_(row) + MAGIC_ATT_SPELL_W + MAGIC_ATT_SUB_GAP;
}
// 攻击魔法页（PAGE_ATTACK）独立布局：说明行→标题行→列表，不随保持
// 状态页的力盾行/状态槽位置推导。
static int AttackMagicHeaderY_() { return ATT_HEADER_Y; }
static int AttackMagicListY_() { return ATT_LIST_Y; }
static int AttackMagicRowHeight_() { return SmallFontRowHeight_(18) + 8; }
static int AttackMagicVisibleRows_() {
    int n = H3AutoPolicy::MagicChannelVisibleRows(AttackMagicListY_(), MAGIC_LIST_BOTTOM, AttackMagicRowHeight_());
    return n > 0 ? n : 1;
}
static void ClampAttackMagicScroll_() {
    const int count = s_p.draft_status[s_p.selected_profile].attack_count;
    const int total = count + (count < H3AutoPolicy::kAttackMagicCapacity ? 1 : 0);
    // 滚动单位为视觉行：两个攻击项并排一行，向上取整覆盖新增“+”行。
    const int visual = (total + 1) / 2;
    s_attack_first = H3AutoPolicy::ClampMagicChannelFirst(s_attack_first, visual, AttackMagicVisibleRows_());
}
static int AttackMagicRowY_(int row) {
    return AttackMagicListY_() + (row / 2 - s_attack_first) * AttackMagicRowHeight_();
}
static void CloseAttackMagicUi_() {
    s_attack_dd_row = -1;
    s_attack_dd_kind = -1;
    s_attack_dd_hover = -1;
    s_attack_dd_first = 0;
    s_attack_first = 0;
    H3AutoPolicy::CancelMagicChannelGesture(&s_attack_gesture);
}
static const char* AttackMagicTargetLabel_(int mode) {
    static const char* keys[] = { "magic.target_priority", "magic.target_random" };
    return T(keys[mode >= 0 && mode < 2 ? mode : 0]);
}
static int BuildAttackMagicCandidates_() {
    s_attack_dd_count = 0;
    H3CombatManager* cm = H3CombatManager::Get();
    const int side = ResolveHumanSide_(o_BattleMgr);
    if (!cm || side < 0 || side > 1 || !cm->hero[side]) return 0;
    H3Hero* hero = cm->hero[side];
    // HasSpell = learnedSpells|availableSpell（原版施法面板同口径）；
    // availableSpell 仅由穿戴装备派生，放背包不算。
    for (int id : H3AutoPolicy::kAttackMagicSpellIds)
        if (hero->HasSpell(id)) s_attack_dd_ids[s_attack_dd_count++] = id;
    return s_attack_dd_count;
}
static void AttackMagicDropdownRect_(int* x, int* y, int* w, int* rows) {
    const int item_h = SmallFontRowHeight_(18);
    // 法术下拉列全白名单；目标下拉只在单体法术打开，仅两项。
    const int count = s_attack_dd_kind == 0 ? s_attack_dd_count : 2;
    int n = count < 8 ? count : 8;
    if (n < 1) n = 1;
    int yy = AttackMagicRowY_(s_attack_dd_row) + item_h;
    if (yy + n * item_h > MAGIC_LIST_BOTTOM) yy = AttackMagicRowY_(s_attack_dd_row) - n * item_h;
    if (yy < ATT_LIST_Y) yy = ATT_LIST_Y;
    if (x) *x = s_attack_dd_kind == 0
        ? AttackMagicSpellX_(s_attack_dd_row) : AttackMagicCtrlX_(s_attack_dd_row);
    if (y) *y = yy;
    if (w) *w = s_attack_dd_kind == 0 ? MAGIC_ATT_SPELL_W : MAGIC_ATT_CTRL_W;
    if (rows) *rows = n;
}
static bool UpdateAttackMagicHover_(int px, int py) {
    if (s_attack_dd_row < 0) return false;
    int x, y, w, rows; AttackMagicDropdownRect_(&x, &y, &w, &rows);
    int hover = -1;
    const int h = SmallFontRowHeight_(18);
    if (PointInRect_(px, py, x, y, w, rows * h))
        hover = s_attack_dd_first + (py - y) / h;
    if (hover == s_attack_dd_hover) return false;
    s_attack_dd_hover = hover;
    return true;
}
static bool AttackMagicWheel_(int delta) {
    if (s_p.active_page != PAGE_ATTACK) return false;
    H3AutoPolicy::CancelMagicChannelGesture(&s_attack_gesture);
    if (s_attack_dd_row >= 0 && s_attack_dd_kind == 0) {
        int rows; AttackMagicDropdownRect_(nullptr, nullptr, nullptr, &rows);
        s_attack_dd_first += delta < 0 ? 1 : -1;
        if (s_attack_dd_first < 0) s_attack_dd_first = 0;
        // 候选可能少于可见行（如只学了 2 个全场法术）；上界为负时夹回 0。
        const int last = s_attack_dd_count - rows;
        if (s_attack_dd_first > last) s_attack_dd_first = last < 0 ? 0 : last;
        s_attack_dd_hover = -1;
    } else if (s_attack_dd_row >= 0) return true;
    else {
        s_attack_first += delta < 0 ? 1 : -1;
        ClampAttackMagicScroll_();
    }
    DrawPanelToBuffer_();
    return true;
}
static bool HandleAttackMagicMouse_(int command, int px, int py) {
    if (s_p.active_page != PAGE_ATTACK || s_panel_hidden_for_pick || s_battle_dd_open || s_status_dd_open >= 0) return false;
    if (command == 8) s_attack_gesture = {};
    if (command == 16 && s_attack_gesture.held) {
        const H3AutoPolicy::MagicChannelGesture gesture = s_attack_gesture;
        bool activate = false;
        if (gesture.held == 1 && s_attack_dd_row < 0) {
            activate = PointInRect_(px, py, AttackMagicSpellX_(gesture.row),
                AttackMagicRowY_(gesture.row), MAGIC_ATT_SPELL_W, SmallFontRowHeight_(18));
            activate = H3AutoPolicy::FinishMagicChannelGesture(&s_attack_gesture, activate ? 1 : 0, gesture.row, gesture.option);
            if (activate && gesture.row == s_p.draft_status[s_p.selected_profile].attack_count
                && H3AutoPolicy::AddAttackMagicRule(&s_p.draft_status[s_p.selected_profile])) {
                if (BuildAttackMagicCandidates_() > 0) {
                    AttackMagicRule& added =
                        s_p.draft_status[s_p.selected_profile].attacks[gesture.row];
                    added.spell_id = s_attack_dd_ids[0];
                    // 类别可能带来控件切换（如从单体改为范围），按新类别归一。
                    H3AutoPolicy::NormalizeAttackMagicRule(&added);
                }
                s_attack_first = gesture.row / 2 - AttackMagicVisibleRows_() + 1;
                ClampAttackMagicScroll_();
            }
        } else if (gesture.held == 2 && s_attack_dd_row == gesture.row) {
            int x, y, w, rows; AttackMagicDropdownRect_(&x, &y, &w, &rows);
            const int option = s_attack_dd_first + (py - y) / SmallFontRowHeight_(18);
            activate = PointInRect_(px, py, x, y, w, rows * SmallFontRowHeight_(18));
            activate = H3AutoPolicy::FinishMagicChannelGesture(&s_attack_gesture, activate ? 2 : 0, s_attack_dd_row, option);
            if (activate) {
                AttackMagicRule& rule = s_p.draft_status[s_p.selected_profile].attacks[s_attack_dd_row];
                if (s_attack_dd_kind == 0) rule.spell_id = s_attack_dd_ids[option];
                else rule.target_mode = option;
                // 换法术可能改变类别（单体/范围/全体），控件与 target 归一。
                H3AutoPolicy::NormalizeAttackMagicRule(&rule);
                s_attack_dd_row = -1; s_attack_dd_kind = -1;
            }
        } else s_attack_gesture = {};
        DrawPanelToBuffer_();
        return true;
    }
    StatusProfileFields& status = s_p.draft_status[s_p.selected_profile];
    const int h = SmallFontRowHeight_(18);
    const bool right = command == 64 || command == static_cast<int>(eMsgCommand::RBUTTON_UP);
    if (s_attack_dd_row >= status.attack_count) { s_attack_dd_row = -1; s_attack_dd_kind = -1; }
    if (s_attack_dd_row >= 0) {
        AttackMagicRule& rule = status.attacks[s_attack_dd_row];
        if (command == 4) { if (UpdateAttackMagicHover_(px, py)) DrawPanelToBuffer_(); return true; }
        int x, y, w, rows; AttackMagicDropdownRect_(&x, &y, &w, &rows);
        if (PointInRect_(px, py, x, y, w, rows * h)) {
            if (command == 8)
                H3AutoPolicy::BeginMagicChannelGesture(&s_attack_gesture, 2, s_attack_dd_row,
                    s_attack_dd_first + (py - y) / h);
            return true;
        }
        if (command == 8 || right) { if (command == 8) H3AutoPolicy::BeginMagicChannelGesture(&s_attack_gesture, 3, -1, -1); s_attack_dd_row = -1; s_attack_dd_kind = -1; DrawPanelToBuffer_(); }
        return true; // 关闭浮层的点击完整吞掉，防止释放穿透新增/保存。
    }
    ClampAttackMagicScroll_();
    const int visible = AttackMagicVisibleRows_();
    for (int v = 0; v < visible * 2; ++v) {
        const int row = s_attack_first * 2 + v, y = AttackMagicRowY_(row);
        if (row > status.attack_count) break;
        if (row == status.attack_count) {
            if (row < H3AutoPolicy::kAttackMagicCapacity
                && PointInRect_(px, py, AttackMagicSpellX_(row), y, MAGIC_ATT_SPELL_W, h)) {
                if (command == 8) H3AutoPolicy::BeginMagicChannelGesture(&s_attack_gesture, 1, row, -1);
                return true;
            }
            continue;
        }
        if (PointInRect_(px, py, AttackMagicSpellX_(row), y, MAGIC_ATT_SPELL_W, h)) {
            if (right) { H3AutoPolicy::RemoveAttackMagicRule(&status, row); ClampAttackMagicScroll_(); DrawPanelToBuffer_(); }
            else if (command == 8) {
                const int keep = s_attack_first; CloseSummonDropdowns_(); s_attack_first = keep;
                s_attack_dd_row = row; s_attack_dd_kind = 0; s_attack_dd_first = 0;
                if (!BuildAttackMagicCandidates_()) { s_attack_dd_row = -1; SetStatusText_(T("magic.no_spells"), 3000); }
                DrawPanelToBuffer_();
            }
            return true;
        }
        // 条件控件列：单体法术才出现目标下拉，范围法术出现落点“+”
        // （点击进入战场拾取，与力盾落点同机制但只能选一个），
        // 全体法术两者皆无（控件列留空）。
        const int kind = H3AutoPolicy::AttackMagicKind(status.attacks[row].spell_id);
        if (kind >= 0 && kind <= 1
            && PointInRect_(px, py, AttackMagicCtrlX_(row), y, MAGIC_ATT_CTRL_W, h)) {
            if (kind == 0 && command == 8) {
                const int keep = s_attack_first; CloseSummonDropdowns_(); s_attack_first = keep;
                s_attack_dd_row = row; s_attack_dd_kind = 1; s_attack_dd_first = 0;
                DrawPanelToBuffer_();
            } else if (kind == 1 && command == 8) {
                // 范围法术：直接进入单落点拾取（左键重选/新增）。
                s_attack_pick_row = row;
                s_attack_pick_position = 0;
                s_panel_hidden_for_pick = true;
                s_pick_wait_button_release = true;
                const int keep = s_attack_first; CloseSummonDropdowns_(); s_attack_first = keep;
                HidePanelForPick_();
            } else if (kind == 1 && right) {
                // 右键删除唯一落点。
                H3AutoPolicy::RemoveAttackMagicPosition(&status.attacks[row], 0);
                ClampAttackMagicScroll_();
                DrawPanelToBuffer_();
            }
            return true;
        }
    }
    return false;
}
