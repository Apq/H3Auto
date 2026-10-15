// ========== UiPortHost.inc.cpp ==========
// 界面契约宿主：定义全局分发指针 g_uiPort 与兜底空实现。
// 包含位置：H3Auto.cpp 中紧随 IniUtf8、先于 ConfigLog——ConfigLog（清方案时
// 重置面板草稿）是契约最早的使用方。
//
// 生命周期：
// 1) DLL 静态初始化期：g_uiPort = 兜底空实现（IsPanelActive 恒 false、
//    FilterGameMsg 恒 false、其余无操作），保证任何时序下的空调用安全；
// 2) SettingsDlg 尾部的 PanelUiPort 适配器在静态初始化期（定义顺序晚于
//    本文件）把 g_uiPort 接管为真实面板实现。
// 默认构建与旧单实现行为完全一致；「无界面」编译变体（H3AUTO_UI_NULL，
// 批次3，见 docs/13）将只保留兜底实现。
#include "AutoUiPort.hpp"

namespace {

// 兜底/无界面实现：诊断与安全默认。不触碰任何游戏对象。
struct FallbackUiPort_ : public IH3AutoUi {
    bool IsPanelActive() const override { return false; }
    bool OpenPanel() override { return false; }
    void ClosePanelSilently() override {}
    void ResetPanelDrafts() override {}
    void OnFramePanelTick() override {}
    bool FilterGameMsg(int* /*msg*/) override { return false; }
};

static FallbackUiPort_ s_fallback_ui_port;

} // namespace

// 契约唯一入口。功能层一律 extern 本指针，不 include 界面实现头。
IH3AutoUi* g_uiPort = &s_fallback_ui_port;
