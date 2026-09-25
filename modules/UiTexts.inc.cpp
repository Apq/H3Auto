// ========== 界面文案（i18n 外置） ==========
// 全部界面文本的唯一定义点：内置默认表 + lang\<语言>.ini 覆盖。
// 加载链：H3Auto.ini [General] Language（缺省 zh-CN）→ DLL 同目录
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
    { "panel.load", "读档" },
    { "panel.save", "存档" },
    { "panel.protect_label", "保活策略:" },
    { "panel.protect_opt0", "无" },
    { "panel.protect_opt1", "部队全灭后" },
    { "panel.protect_opt2", "回合内首动" },
    { "panel.protect_opt3", "损失量大于恢复量" },
    { "panel.stop_label", "停止:" },
    { "panel.profile_btn_fmt", "方案 %d" },
    { "panel.status_save_ok", "{绿}存档成功" },
    { "panel.status_save_fail", "{红}存档失败" },
    { "panel.status_load_ok", "{绿}读档成功" },
    { "panel.status_load_fail", "{红}读档失败" },
    // ---- help：帮助模态 ----
    { "help.title", "使用说明" },
    { "help.line_open", "打开设置：右键“自动战斗”按钮，或按 %s 键" },
    { "help.line1", "方案 1-5：独立草稿与存档文件，读档/存档针对选中编号" },
    { "help.line2", "施法/近战/移动：点 ＋ 后按提示设置" },
    { "help.line3", "停止：敌方预计剩余回合内全灭时交回" },
    { "help.line4", "读档/存档：仅更新界面显示，点勾号才生效" },
    { "help.line5", "删除：槽位上右键" },
    { "help.line_hotkey", "热键：%s 启停打铁 · %s 单次接管 · %s 打开设置" },
    { "help.line6", "设置有效期：同一场战斗，包括取消重打" },
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
    { "help.log_level_set", "{绿}日志级别已设为 %s（已写入配置）" },
    // ---- tips：状态栏悬停提示 ----
    { "tips.color_wrap", "{金}%s" },
    { "tips.titlebar", "热键：%s 启停打铁 · %s 单次接管 · %s 打开设置 · 右键“自动战斗”按钮也可打开" },
    { "tips.btn_load", "读档：从选中编号的存档槽读入草稿（四轮部队关联）；仅更新界面，点勾号才生效" },
    { "tips.btn_save", "存档：把草稿写入选中编号的存档槽（其他槽不变）；不改变已生效方案" },
    { "tips.btn_profile", "方案 1-5：各编号独立草稿与存档文件；切换编号各自保留，读档/存档针对选中编号" },
    { "tips.protect_row", "保活策略：无 / 部队全灭后 / 回合内首动 / 损失量大于恢复量" },
    { "tips.stop_row", "停止：敌方预计剩余回合 ≤ 此值时切回手动；0=关闭，最大 999" },
    { "tips.btn_ok", "勾号：草稿生效并关闭面板（不写盘）；有效期同一场战斗（含取消重打）" },
    { "tips.btn_cancel", "取消：丢弃全部修改并关闭面板" },
    { "tips.cell_action", "行动：本部队轮到时自动执行的动作；手动=交给玩家" },
    { "tips.cell_selector", "目标选择：多个候选时按此排序挑选（远程/急救各有自己的选项）" },
    { "tips.cell_fallback", "允许降级为防御：目标不可达或动作失败时自动防御，否则交回玩家" },
    { "tips.cell_protect", "加入保活队列：勾选后参与方案级保活策略的复活判定" },
    { "tips.cell_drop", "选择一项；点外部或再点一次收起" },
    { "tips.cell_spell", "行动前循环快捷施法：本部队行动前按槽位顺序投快捷施法键（1-9,0）；英雄每回合只施一次，已施法则跳过；右键槽位删除" },
    { "tips.cell_move", "循环移动：按槽位顺序走向战场格，走完一轮从头再来；右键槽位删除" },
    { "tips.cell_melee", "循环近战：每槽一对「站立格→攻击格」，按序循环执行；右键槽位删除" },
    // ---- cell：格子控件 ----
    { "cell.allow_fallback", "允许降级为防御" },
    { "cell.pre_cast_label", "行动前循环快捷施法:" },
    { "cell.protect_join", "加入保活队列" },
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

// 语言名（如 zh-CN），来自 H3Auto.ini [General] Language。
static char g_ui_language[32] = "zh-CN";

// 启动时调用一次：读语言名 → 逐键覆盖 → 填充 action/selector 标签数组。
static void LoadUiTexts()
{
    char lang[32] = {};
    IniReadUtf8(g_ini_path, "General", "Language", "zh-CN", lang, sizeof(lang));
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
