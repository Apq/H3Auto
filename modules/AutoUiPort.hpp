// ========== AutoUiPort.hpp ==========
// 界面契约（docs/13-界面与功能分离.md，参考 H3BattleStore BattleUiPort 模式）。
// 目标：以后可以整套替换设置面板界面，功能层（AutoExecute/BattleState/
// ConfigLog/Entry 钩子外壳）不感知任何界面实现符号，只经本接口。
//
// 约束（与 H3BattleStore 契约同口径）：
// - 仅游戏主线程调用（各钩子回调与绘制帧）；不新增线程入口。
// - 不得在 __except 过滤器内调用；实现方法不得要求栈展开携带 C++ 对象。
// - 禁止把绘制原语、控件命中、布局常量放进契约——那是界面实现私有。
// - 每新增一个方法必须在 docs/13 登记理由与调用方。
//
// 包含位置：无游戏类型依赖，可被任意模块包含。g_uiPort 的定义在
// UiPortHost.inc.cpp（先于 ConfigLog/AutoExecute 包含），加载期指向
// 兜底空实现，SettingsDlg 尾部的真实适配器在静态初始化期接管。
#pragma once

class IH3AutoUi {
public:
    virtual ~IH3AutoUi() {}

    // ---- 查询/请求（功能层 → 界面）----
    // 设置面板当前是否打开（含隐藏拾取态）。调用方：AutoExecute 战斗热键
    // 边沿门禁、ResetAutoState、BattleState 结算边动作。
    virtual bool IsPanelActive() const = 0;
    // 打开设置面板；返回打开后面板是否处于激活态（对应旧 OpenSettingsPanel_()
    // + s_p.active 复核的调用约定）。调用方：SettingsDlg 生命周期事件源
    // （开面板热键/右键说明框路径），批次2 随生命周期层一并改走契约。
    virtual bool OpenPanel() = 0;
    // 静默关闭设置面板（不清草稿语义由实现保持原 CloseSettingsPanel 不变）。
    // 调用方：ResetAutoState、BattleState 结算出现边。
    virtual void ClosePanelSilently() = 0;
    // 清空面板草稿。调用方：ConfigLog ClearConfirmedProfiles（生效方案清空
    // 时草稿必须同步，否则下一场「确定」复活旧规则）。
    virtual void ResetPanelDrafts() = 0;

    // ---- 帧与消息（钩子外壳 → 界面）----
    // 绘制帧的面板部分（模态深度、悬挂判定、输入处理、绘制、拾取标记）。
    // 仅当 IsPanelActive() 为真时由外壳调用；原 Hook_BltComplete_Inner_ 的
    // UI 分支原样迁入实现。
    virtual void OnFramePanelTick() = 0;
    // 战斗消息钩子的面板吞并判定：面板打开时拦 0-9 快捷施法键（返回 true =
    // 吞并），并把鼠标移动坐标改写离屏以清 hover 高亮（改写后返回 false
    // 放行）。调用前提 IsPanelActive() 为真——外壳在面板活跃分支内调用且
    // 无论返回值都直接返回，不落入主动作提交。msg 为原版消息数组
    // （[0]=命令 [1]=键码/子类型 [4]/[5]=坐标），与原 Hook_BattleMsgProc_
    // Inner_ 同一读法。
    virtual bool FilterGameMsg(int* msg) = 0;
};
