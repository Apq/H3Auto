// MagicChannelDraw.inc.cpp - 攻击魔法页（PAGE_ATTACK）：说明、智能攻击行和浮层。
static void DrawAttackMagicPage_(H3LoadedPcx16* scr) {
    if (!scr) return;
    H3Font* font = GetSmallFont();
    const int h = SmallFontRowHeight_(18);
    // 说明行（停止行下方）+ 标题行，与保持状态页同风格但位置独立。
    DrawTxt(scr, font, T("magic.attack_note"), GRID_FRAME_X, ATT_NOTE_Y,
        GRID_FRAME_W, h, (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
    const int header_y = AttackMagicHeaderY_();
    DrawTxt(scr, font, T("magic.attack_label"), GRID_FRAME_X, header_y,
        GRID_FRAME_W, h, (INT32)eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT);
    ClampAttackMagicScroll_();
    const StatusProfileFields& status = s_p.draft_status[s_p.selected_profile];
    for (int v = 0; v < AttackMagicVisibleRows_() * 2; ++v) {
        const int row = s_attack_first * 2 + v;
        const int y = AttackMagicRowY_(row);
        if (row > status.attack_count) break;
        const int spell_x = AttackMagicSpellX_(row);
        if (row == status.attack_count) {
            if (row < H3AutoPolicy::kAttackMagicCapacity) {
                CellControl_DrawButtonBg(scr, spell_x, y, MAGIC_ATT_SPELL_W, h, false, false);
                CellControl_DrawPlusButton(scr, spell_x + (MAGIC_ATT_SPELL_W - 16) / 2,
                    y + (h - 16) / 2, 16, false);
            }
            continue;
        }
        const AttackMagicRule& rule = status.attacks[row];
        const int kind = H3AutoPolicy::AttackMagicKind(rule.spell_id);
        char name[160] = {};
        if (rule.spell_id < 0) _snprintf(name, sizeof(name), "%s", T("magic.select_spell"));
        else StatusSpellName_(rule.spell_id, name, sizeof(name));
        DrawSummonCombo_(scr, font, spell_x, y, MAGIC_ATT_SPELL_W, h, name,
            s_attack_dd_row == row && s_attack_dd_kind == 0);
        // 条件控件列：单体→目标下拉（优先/随机）；范围→落点按钮
        // （“+”进入战场拾取、单点，与力盾同机制）；全体→留空。
        if (kind == 0) {
            DrawSummonCombo_(scr, font, AttackMagicCtrlX_(row), y, MAGIC_ATT_CTRL_W, h,
                AttackMagicTargetLabel_(rule.target_mode),
                s_attack_dd_row == row && s_attack_dd_kind == 1);
        } else if (kind == 1) {
            char label[64] = {};
            if (rule.position_count > 0 && rule.positions[0] >= 0) {
                char hex[8] = {};
                CellControl_FormatPosition(hex, sizeof(hex), rule.positions[0]);
                _snprintf(label, sizeof(label), T("magic.attack_pos_set"), hex);
            } else {
                _snprintf(label, sizeof(label), "%s", T("magic.attack_pos_empty"));
            }
            CellControl_DrawButtonBg(scr, AttackMagicCtrlX_(row), y, MAGIC_ATT_CTRL_W, h,
                false, false);
            DrawTxt(scr, font, label, AttackMagicCtrlX_(row) + 4, y,
                MAGIC_ATT_CTRL_W - 8, h, (INT32)eTextColor::GOLD,
                eTextAlignment::MIDDLE_CENTER);
        }
    }
    // 列表滚轮提示在标题右侧，不占一条配置行。
    const int visual_total = (status.attack_count
        + (status.attack_count < H3AutoPolicy::kAttackMagicCapacity ? 1 : 0) + 1) / 2;
    if (visual_total > AttackMagicVisibleRows_())
        DrawTxt(scr, font, T("magic.scroll_hint"), GRID_FRAME_X + 230, header_y,
            GRID_FRAME_W - 230, h, (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_RIGHT);
}
static void DrawAttackMagicDropdown_(H3LoadedPcx16* scr) {
    if (!scr || s_p.active_page != PAGE_ATTACK || s_attack_dd_row < 0) return;
    H3Font* font = GetSmallFont();
    const int h = SmallFontRowHeight_(18);
    const AttackMagicRule& rule = s_p.draft_status[s_p.selected_profile].attacks[s_attack_dd_row];
    int x, y, w, rows; AttackMagicDropdownRect_(&x, &y, &w, &rows);
    for (int v = 0; v < rows; ++v) {
        const int option = s_attack_dd_first + v;
        const bool hover = option == s_attack_dd_hover;
        const bool selected = s_attack_dd_kind == 0
            ? s_attack_dd_ids[option] == rule.spell_id : option == rule.target_mode;
        Fill(scr, x, y + v * h, w, h, hover ? 184 : selected ? 136 : 68,
            hover ? 136 : selected ? 88 : 42, hover ? 48 : selected ? 24 : 18);
        scr->DrawFrame(x, y + v * h, w, h, (BYTE)210, (BYTE)170, (BYTE)72);
        char name[160] = {};
        const char* label = s_attack_dd_kind == 0
            ? StatusSpellName_(s_attack_dd_ids[option], name, sizeof(name)) : AttackMagicTargetLabel_(option);
        DrawTxt(scr, font, label, x + 6, y + v * h, w - 12, h,
            (INT32)eTextColor::GOLD, eTextAlignment::MIDDLE_LEFT);
    }
}
