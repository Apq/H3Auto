// ========== 界面文案（i18n 外置） ==========
// 全部界面文本的唯一定义点：内置默认表 + lang\<语言>.ini 覆盖。
// 加载链：H3Auto.default.ini [General] Language（缺省 zh-CN，玩家层可覆盖）→ DLL 同目录
// lang\<语言>.ini 覆盖命中键；缺文件/缺键回落内置默认（删光配置不崩）。
// 键 = "节.键名"；值支持富文本颜色标记（{金} {#RRGGBB} 等）与 %s/%d
// 格式串。行内注释只认 ';'，'#' 只在行首当注释（保护 {#RRGGBB}）。
// 仅状态栏键（panel.status_* / help.pack_*）走富文本绘制，其他位置的
// 键带标记会原样显示。

#include "PanelLayout.hpp"

// 运行时标签（原 SettingsDlg 注入，现由 LoadUiTexts 填充；CellControl 按枚举序号索引）
const char* g_action_labels[AA_COUNT] = {};
const char* g_selector_labels[SEL_COUNT] = {};
static char g_panel_title[64] = {};
static char s_label_storage_action[AA_COUNT][64] = {};
static char s_label_storage_selector[SEL_COUNT][64] = {};

struct UiTextEntry { const char* key; const char* text; };

// 内置默认表（与 lang/zh-CN.ini 镜像；改文案两处同步改）。
static const UiTextEntry kUiTextDefaults[] = {
    // ---- panel：设置面板 ----
    { "panel.title", "打铁设置" },
    { "panel.battle_dd", "本场存档 ▾" },
    { "panel.battle_dd_none", "无存档 ▾" },
    { "panel.status_battle_none", "{红}本场还没有存档（点√确定即存）" },
    { "panel.status_battle_loaded", "{绿}已载入所选存档（点√确定才生效）" },
    { "panel.stop_label", "停止:" },
    { "panel.stop_mana_label", "敌方魔力≤" },
    { "panel.stop_mana_tail", "时停" },
    { "panel.tab_army", "部队" },
    { "panel.tab_summon", "召唤" },
    { "panel.tab_status", "保持状态" },
    { "panel.status_note", "只列英雄已学会的范围状态魔法。多个都保持，哪个剩余回合先到阈值就补哪个" },
    { "panel.status_add", "+" },
    { "panel.status_slow", "敌方减速（需专家群体）" },
    { "panel.status_empty", "（无已学范围魔法）" },
    { "panel.status_th_label", "剩余回合≤" },
    { "panel.status_th_tail", "时补" },
    // ---- 召唤页（PAGE_SUMMON）----
    { "panel.summon_enable", "启用自动召唤" },
    { "panel.summon_spell_label", "魔法:" },
    { "panel.summon_cond_label", "条件:" },
    { "panel.summon_spell_opt0", "自动" },
    { "panel.summon_spell_opt1", "气元素" },
    { "panel.summon_spell_opt2", "水元素" },
    { "panel.summon_spell_opt3", "火元素" },
    { "panel.summon_spell_opt4", "土元素" },
    { "panel.summon_count_label", "队数 <" },
    { "panel.summon_hp_label", "血量 ≤" },
    { "panel.summon_cond_and", "和" },
    { "panel.summon_cond_or", "或" },
    { "panel.summon_action_label", "行动:" },
    { "panel.summon_note", "启用后：先判保活，无人可救且满足下方条件时才召唤" },
    { "panel.tab_profile", "方案" },
    { "panel.profile_note", "方案级设置：自动停止（作用于当前选中的方案）；保活与召唤见各部队卡片与召唤页" },
    { "panel.profile_btn_fmt", "方案 %d" },
    { "panel.status_save_ok", "{绿}存档成功" },
    { "panel.status_save_fail", "{红}存档失败" },
    // ---- help：帮助模态 ----
    { "help.title", "使用说明" },
    { "help.line_open", "打开设置：右键“自动战斗”按钮，或按 %s 键" },
    { "help.line1", "方案 1-5：独立草稿，切换编号各自保留" },
    { "help.line2", "施法/近战/移动：点 ＋ 后按提示设置" },
    { "help.line3", "停止：预计回合内全灭或敌方魔力≤阈值时交回" },
    { "help.line4", "本场存档：点√自动存档；右上角下拉选历史档载入草稿，再点√生效" },
    { "help.line5", "删除：槽位上右键" },
    { "help.line_summon", "召唤：召唤页勾「启用自动召唤」后，保活无人可救且兵力不足时召唤元素补位" },
    { "help.line_hotkey", "热键：%s 启停打铁 · %s 单次接管 · %s 打开设置" },
    { "help.line6", "设置有效期：同一场战斗，包括取消重打；重打/重开后开面板即见上次方案" },
    { "help.link", "{灰}开源地址（点击复制）：{白}https://github.com/Apq/H3Auto" },
    { "help.link_flash", "{绿}开源地址（点击复制）：https://github.com/Apq/H3Auto" },
    { "help.link_url", "https://github.com/Apq/H3Auto" },
    { "help.link_copied", "{绿}开源地址已复制到剪贴板" },
    { "help.link_fail", "{红}复制失败：剪贴板被占用，请重试" },
    { "help.close", "关闭" },
    { "help.log_level_label", "日志级别:" },
    { "help.log_level_opt0", "全部" },
    { "help.log_level_opt1", "调试" },
    { "help.log_level_opt2", "信息" },
    { "help.log_level_opt3", "警告" },
    { "help.log_level_opt4", "错误" },
    { "help.pack_btn", "打包日志" },
    { "help.pack_title", "日志已打包" },
    { "help.pack_ok", "日志已打包成 .7z 并复制为文件，直接到 QQ 聊天框粘贴发送即可。|QQ群：1042362808 / 740338251，或加 QQ：712999712 私发。|（QQ号已同时写在压缩包内的 00_说明.txt 里，解压即可复制）" },
    { "help.pack_note", "请把同目录的日志文件发送到：|QQ群：1042362808 / 740338251|或加 QQ：712999712 私发|" },
    { "help.pack_fail", "{红}打包日志失败：%s" },
    { "help.pack_no_logs", "没有找到日志文件" },
    { "help.pack_clipboard_fail", "zip 已生成但复制路径失败（剪贴板被占用），请手动复制" },
    { "help.log_level_set", "{绿}日志级别已设为 %s（已写入 user.ini）" },
    // ---- tips：状态栏悬停提示 ----
    { "tips.color_wrap", "{金}%s" },
    { "tips.titlebar", "热键：%s 启停打铁 · %s 单次接管 · %s 打开设置 · 右键“自动战斗”按钮也可打开" },
    { "tips.btn_battle_dd", "本场存档：列出这场战斗每次点√确定的方案快照（最多30条）；点开选一条载入草稿，点√确定才生效；首次打开面板时最新一条已自动载入" },
    { "tips.btn_profile", "方案 1-5：各编号独立草稿；切换编号各自保留，确定按当前编号生效" },
    { "tips.summon_enable", "启用自动召唤：与保活同一条施法通道，保活优先。每次行动（还有施法次数时）先按各部队卡片的方式判保活；无人可救且已勾选时，再按「和/或」组合队数/血量两条件判定兵力不足而召唤元素补位。默认关闭=只保活、不召唤" },
        { "tips.stop_mana", "敌方魔力停止：默认勾选；敌方英雄（有魔法书）魔力 ≤ 右侧阈值时切回手动，与停止回合数任一满足即停" },
        { "tips.stop_mana_th", "敌方魔力阈值：默认 6，范围 0..32767；随方案存档，配合左侧勾选生效" },
        { "tips.stop_row", "停止：敌方预计剩余回合 ≤ 此值时切回手动；0=关闭，最大 999" },
        { "tips.status_th", "补状态阈值：所有已配置的状态魔法都保持；任意法术在任意己方部队（减速为敌方全体覆盖）上剩余回合 ≤ 此值就补施它，多个到期补剩余最少的。默认 1，范围 1..9；随方案存档" },
    { "tips.btn_ok", "勾号：草稿生效并关闭面板，同时自动存一条本场存档（内容没变则不重复存）；有效期同一场战斗（含取消重打）" },
    { "tips.btn_cancel", "取消：丢弃全部修改并关闭面板" },
    { "tips.action_opt0", "行动·手动：轮到本部队时交给玩家操作" },
    { "tips.action_opt1", "行动·防御：自动防御" },
    { "tips.action_opt3", "行动·循环移动：按槽位顺序走向战场格，走完从头再来" },
    { "tips.action_opt4", "行动·循环近战：按「站立格→攻击格」成对循环攻击" },
    { "tips.action_opt5", "行动·远程攻击：自动远程攻击，目标按目标选择排序" },
    { "tips.action_opt6", "行动·急救治疗：自动治疗己方伤员，目标按目标选择排序" },
    { "tips.action_opt7", "行动·散开：尽量与己方所有其它部队保持至少 2 格；做不到时降为 1 格，再做不到原地防御（召唤物行动专用）" },
    { "tips.action_opt8", "行动·随机移动：随机挑一格移动，无路可走时可勾选降级为防御（召唤物行动专用）" },
    { "tips.selector_opt0", "目标选择·随机：候选中随机挑一个" },
    { "tips.selector_opt1", "目标选择·远程飞兵高速优先：先打远程和飞行，再按速度；同类同速优先打血量更高的那队" },
    { "tips.selector_opt2", "目标选择·数量最多：优先数量最多的部队" },
    { "tips.selector_opt3", "目标选择·失血比例：优先失血比例最高的（急救用）" },
    { "tips.selector_opt4", "目标选择·失血数值：优先失血数值最大的（急救用）" },
    { "tips.cell_fallback", "允许降级为防御：目标不可达或动作失败时自动防御，否则交回玩家" },
    { "tips.cell_protect_count", "保活数量阈值：该队剩余数量 ≤ 此值才保活。默认 2，范围 0..2147483647；0 表示只救已全灭的部队。保活方式需为「剩余数量≤」" },
    { "tips.protect_opt_none", "保活·不保活：该队不参与保活" },
    { "tips.protect_opt_count", "保活·剩余数量≤：剩余数量不超过「剩≤」才救" },
    { "tips.protect_opt_loss", "保活·损失量>恢复量：损失超过一次恢复量才救" },
    { "tips.tab_army", "部队页：每支部队的行动、循环施法/移动/近战与保活方式" },
    { "tips.tab_profile", "方案页：方案级设置——自动停止回合数与敌方魔力停止" },
    { "tips.tab_summon", "召唤页：勾「启用自动召唤」后，保活无人可救且按「和/或」组合的队数/血量条件满足时召唤元素补位；召唤物共用行动" },
    { "tips.summon_spell_opt0", "法术·自动：已学的四系召唤里选单次召唤总量最大的元素；首次成功施放后本场锁定该元素" },
    { "tips.summon_spell_opt1", "法术·气元素：固定召唤气元素；未学该法术则不施放" },
    { "tips.summon_spell_opt2", "法术·水元素：固定召唤水元素；未学该法术则不施放" },
    { "tips.summon_spell_opt3", "法术·火元素：固定召唤火元素；未学该法术则不施放" },
    { "tips.summon_spell_opt4", "法术·土元素：固定召唤土元素；未学该法术则不施放" },
    { "tips.summon_count", "队数阈值：存活的本体部队加存活的召唤物（不含战争机器、克隆）小于此值即满足队数条件；默认 2，范围 2..21" },
    { "tips.summon_hp", "血量阈值：己方各队剩余血量加总（存活数×单兵满血−顶层已损，含召唤物、不含战争机器）≤ 此值即满足血量条件" },
    { "tips.summon_cond", "条件组合：队数与血量两条件怎么搭配——和=两个都满足才召唤（默认）；或=任一满足即召唤" },
    { "tips.summon_action", "召唤物行动：手动/防御/散开/随机移动，全部召唤物共用一个设置；默认防御" },
    { "tips.summon_fallback", "允许降级为防御：随机移动找不到可走格时改为防御；仅行动=随机移动时显示并生效" },
    { "tips.cell_drop", "选择一项；点外部或再点一次收起" },
    { "tips.cell_spell", "行动前循环快捷施法：本部队行动前按槽位顺序投快捷施法键（1-9,0）；英雄每回合只施一次，已施法则跳过；右键槽位删除" },
    { "tips.cell_move", "循环移动：按槽位顺序走向战场格，走完一轮从头再来；右键槽位删除" },
    { "tips.cell_melee", "循环近战：每槽一对「站立格→攻击格」，按序循环执行；右键槽位删除" },
    // ---- cell：格子控件 ----
    { "cell.allow_fallback", "允许降级为防御" },
    { "cell.pre_cast_label", "循环施法:" },
    { "cell.protect_mode_lbl", "保活:" },
    { "cell.protect_mode_none", "不保活" },
    { "cell.protect_mode_count", "剩余数量≤" },
    { "cell.protect_mode_loss", "损失量>恢复量" },
    { "cell.protect_count_lbl", "剩≤" },
    { "cell.target_melee_pair", "循环站立位+攻击位" },
    { "cell.target_wounded", "己方伤员" },
    { "cell.target_none", "无目标" },
    { "cell.spell_modal_title", "设置快捷施法键" },
    { "cell.spell_modal_slot", "正在设置循环施法第 %d 槽" },
    { "cell.spell_modal_hint1", "请直接按数字键 1-9 或 0（支持小键盘）" },
    { "cell.spell_modal_hint2", "输入后自动保存；按 ESC 或点击取消放弃。" },
    { "cell.spell_modal_cancel", "取消" },
    // ---- hud：战场角标 ----
    { "hud.prefix", "打铁助手: %s" },
    { "hud.mode_auto", "自动" },
    { "hud.mode_manual", "全手动" },
    { "hud.mode_oneshot", "单次接管" },
    { "hud.mode_wait", "单次待命" },
};
static const int kUiTextCount = (int)(sizeof(kUiTextDefaults) / sizeof(kUiTextDefaults[0]));

// 覆盖缓冲（lang 文件命中才非空）。512：单条文案含多行说明与路径占位
// （help.pack_ok 原文 233 字节），192 会在「压缩包内的」处截断。
static char s_ui_overrides[kUiTextCount][512];

// 取界面文案。键不存在返回 "!!键!!"（开发期立即可见）。
static const char* T(const char* key)
{
    for (int i = 0; i < kUiTextCount; ++i)
        if (strcmp(kUiTextDefaults[i].key, key) == 0)
            return s_ui_overrides[i][0] ? s_ui_overrides[i] : kUiTextDefaults[i].text;
    return "!!键不存在!!";
}

// lang 文件路径（UTF-8，与 g_ini_path 同目录）。
static char* g_lang_path = new char[kPathCap_]();

// 语言名（如 zh-CN），来自 H3Auto.default.ini [General] Language（user 层可覆盖）。
static char g_ui_language[32] = "zh-CN";

// 启动时调用一次：读语言名 → 逐键覆盖 → 填充 action/selector 标签数组。
static void LoadUiTexts()
{
    char lang[32] = {};
    IniReadUtf8Layered("General", "Language", "zh-CN", lang, sizeof(lang));
    bool lang_ok = lang[0] != 0;
    for (const char* p = lang; p && *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) { lang_ok = false; break; }
    }
    if (lang_ok) strncpy(g_ui_language, lang, sizeof(g_ui_language) - 1);
    else strncpy(g_ui_language, "zh-CN", sizeof(g_ui_language) - 1);
    g_ui_language[sizeof(g_ui_language) - 1] = 0;

    // lang\<语言>.ini：DLL 目录 = g_ini_path 去掉文件名。
    strncpy(g_lang_path, g_ini_path, kPathCap_ - 1);
    g_lang_path[kPathCap_ - 1] = 0;
    char* slash = strrchr(g_lang_path, (char)92);
    if (!slash) slash = strrchr(g_lang_path, '/');
    if (slash) {
        _snprintf(slash + 1, kPathCap_ - (int)(slash + 1 - g_lang_path) - 1,
            "lang\\%s.ini", g_ui_language);
        g_lang_path[kPathCap_ - 1] = 0;
    } else {
        g_lang_path[0] = 0;
    }

    int hits = 0;
    if (g_lang_path[0]) {
        for (int i = 0; i < kUiTextCount; ++i) {
            const char* dot = strchr(kUiTextDefaults[i].key, '.');
            if (!dot) continue;
            char sec[24] = {};
            const int n = (int)(dot - kUiTextDefaults[i].key);
            if (n <= 0 || n >= (int)sizeof(sec)) continue;
            memcpy(sec, kUiTextDefaults[i].key, n);
            sec[n] = 0;
            if (IniReadUtf8(g_lang_path, sec, dot + 1, "",
                s_ui_overrides[i], sizeof(s_ui_overrides[i]))
                && s_ui_overrides[i][0])
                ++hits;
        }
    }

    // 行动/目标标签数组（沿用原 [Actions]/[Selectors] 序号键语义，
    // 现从 lang 文件的 [actions]/[selectors] 节读）。
    for (int i = 0; i < AA_COUNT; ++i) {
        char key[8] = {};
        _snprintf(key, sizeof(key) - 1, "%d", i);
        IniReadUtf8(g_lang_path[0] ? g_lang_path : g_ini_path, "actions", key,
            DEFAULT_ACTION_LABELS[i], s_label_storage_action[i],
            sizeof(s_label_storage_action[i]));
        g_action_labels[i] = s_label_storage_action[i];
    }
    for (int i = 0; i < SEL_COUNT; ++i) {
        char key[8] = {};
        _snprintf(key, sizeof(key) - 1, "%d", i);
        IniReadUtf8(g_lang_path[0] ? g_lang_path : g_ini_path, "selectors", key,
            DEFAULT_SELECTOR_LABELS[i], s_label_storage_selector[i],
            sizeof(s_label_storage_selector[i]));
        g_selector_labels[i] = s_label_storage_selector[i];
    }
    IniReadUtf8(g_lang_path[0] ? g_lang_path : g_ini_path, "panel", "title",
        "打铁设置", g_panel_title, sizeof(g_panel_title));

    LogInfo("[UiText] 文案加载：lang=%s 覆盖 %d 键（%s）",
        g_ui_language, hits, g_lang_path[0] ? g_lang_path : "无 lang 文件，全内置默认");
}

// 面板标题（SettingsDlg/PanelDraw 使用）。
static const char* PanelTitle_()
{
    return g_panel_title[0] ? g_panel_title : T("panel.title");
}
