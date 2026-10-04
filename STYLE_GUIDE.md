# BlockBox 样式代码规范（Style Guide）

> 目标：**消灭内联 QSS 屎山**。所有颜色来自主题令牌（Token），所有重复样式走集中原型。
> 2026-08 重构后全代码库已无常量灰色文字/边框（`#333`/`#888`/`#d8d8d8`…），请新代码保持。

## 一、三层样式体系（按优先级选层）

| 层 | 用途 | 位置 |
|---|---|---|
| 1. 全局 QSS | 所有控件的默认外观、hover/focus/disabled 态 | `styles/style.qss`（objectName/类型选择器 + `@TOKEN@`） |
| 2. StyleKit 原型 | 跨页面反复出现的组件样式（按钮/标题/输入框/卡片/胶囊） | `utils/StyleKit.h/.cpp` |
| 3. 一次性内联 | 仅本控件使用的特殊样式 | `setStyleSheet(StyleKit::resolve(QStringLiteral(...)))`，颜色一律 `@TOKEN@` |

**判断顺序**：全局 QSS 能管到的（如普通 QLineEdit、QCheckBox）→ 不要写任何内联样式；
样式在 ≥2 个文件出现 → 提升为 StyleKit 原型；其余 → 第 3 层。

## 二、禁止事项（Code Review 红线）

1. **禁止硬编码颜色**：`#333`、`#888`、`rgba(0,0,0,0.06)`、`white` 等一律不得出现在
   内联样式中。深色/自定义主题下它们是肉眼可见的 bug，也是历次"深色模式翻车"的根源。
2. **禁止在页面里定义 `QString btnStyle = QString(...).arg(themeColor)` 这类局部样式字符串** —
   这是本次重构清除的主要屎山形态。用 `StyleKit::primaryButton()` 等原型替代。
3. **禁止绕过 ThemeManager 取色后手工拼深浅分支**（`isDark ? "#2e2e32" : "#fafafa"`）—
   token 表已经做了主题适配，写 `@BG_CONTENT@` 即可。
4. **禁止新增 `@TOKEN@` 之外的占位符格式**；新颜色请先在
   `ThemeManager::buildTokenTable()` 注册并更新 style.qss 头部令牌速查表。

## 三、StyleKit 用法

```cpp
#include "utils/StyleKit.h"

// 原型直接用：调用时解析当前主题颜色
btn->setStyleSheet(StyleKit::primaryButton());   // 实心主按钮（含 hover/pressed/disabled）
btn->setStyleSheet(StyleKit::outlineButton());   // 描边次按钮
btn->setStyleSheet(StyleKit::miniButton());      // 全选/工具性小按钮
lbl->setStyleSheet(StyleKit::pageTitle());       // 18px 粗体标题
lbl->setStyleSheet(StyleKit::mutedLabel());      // 12px 弱化说明
edit->setStyleSheet(StyleKit::lineEdit());       // 输入框（含聚焦主题色描边）
card->setStyleSheet(StyleKit::card());           // 卡片容器

// 一次性样式：布局属性照写，颜色用 token，包一层 resolve
w->setStyleSheet(StyleKit::resolve(QStringLiteral(
    "QFrame#myPanel { background: @BG_MUTED@;"
    " border: 1px solid @BORDER@; border-radius: 12px; }")));

// 运行时颜色（用户取色器等）与 token 混排：arg 完再 resolve
btn->setStyleSheet(StyleKit::resolve(QString(
    "QPushButton { background-color: %1; border: 2px solid @BORDER_STRONG@; }")
    .arg(userColor)));

// 语义色速查（详见 style.qss 头部令牌表）
// 文字：@TEXT_PRIMARY@/@TEXT_SECONDARY@/@TEXT_TERTIARY@/@TEXT_DISABLED@
// 表面：@BG_CARD@/@BG_MUTED@/@BG_HOVER@/@BG_CONTENT@/@BG_TRACK@
// 边框：@BORDER@/@BORDER_STRONG@/@BORDER_HOVER@/@BORDER_LIGHT@
// 语义：@PRIMARY@/@INFO@/@SUCCESS@/@WARNING@/@DANGER@（及 *_HOVER/_PRESSED/_BG 变体）
// 半透明：@PRIMARY_RGBA_20@/@INFO_RGBA_38@/@BLACK_RGBA_0.06@（深色主题自动反白）
```

**当前原型清单**（`utils/StyleKit.h`）：`pageTitle / sectionTitle / cardTitle /
bodyLabel / secondaryLabel / mutedLabel / captionLabel / successLabel / dangerLabel /
primaryButton / smallPrimaryButton / miniButton / outlineButton / dangerButton /
ghostButton / lineEdit / textEdit / checkBox / card / flatCard / transparent /
chip(bg, fg, border) / colorSwatch(color) / resolve(raw) / setClass(w, value)`。

新增原型：在 `StyleKit.h` 声明 + `.cpp` 实现，实现体用 `resolve(QStringLiteral(...))`
书写；命名用语义（`primaryButton`）而非外观（`greenButton`）。

## 四、跟随主题色变更

StyleKit 返回值在**调用时**解析颜色。需要实时跟随 `themeColorChanged` 的控件，
沿用现有惯例 —— 在槽里重新调用并 setStyleSheet：

```cpp
connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this, [this] {
    m_ctaBtn->setStyleSheet(StyleKit::primaryButton());
});
```

静态语义 token（`@TEXT_*`/`@BG_*`/`@DANGER@` 等）已随主题自适应，**无需**重刷；
只有引用 `@PRIMARY@`/`@INFO@` 族（用户可改的主题色/强调色）的控件需要重刷。

## 五、运行时切换样式类

基于全局 QSS 属性选择器（如 `QPushButton[class~="btn-primary"]`）的控件，运行时切换类
必须触发重新抛光，直接用工具函数：

```cpp
StyleKit::setClass(btn, "btn-primary");   // 内部完成 setProperty + unpolish/polish
```

仅构造期设置时，`setProperty("class", "...")` 即可，无需 repolish。

## 六、例外与历史遗留

- `PluginSafetyGuard` / `PluginPage` / 部分 AI 对话气泡使用**页内语义调色板**
  （`ThemeColors` 结构体），是收拢过的自洽体系，允许保留，但新增颜色仍须走 token。
- 皮肤编辑器画布的 `#2a2a2a` 深底是**功能色**（图像编辑环境），不属于主题适配范围。
- 富文本 `setText("<div style='color:#666'>…")` 中的颜色不走 QSS 管线，改动时请改用
  `ThemeManager::instance()->currentTextColor()` 等运行时取色，并逐步迁移。
- `styles/style.qss.bak` 为旧版备份，勿在其中查找现行规则。
