/**
 * @file   ForumKit.h
 * @brief  论坛页面共用的小部件与工具函数
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 帖子详情页、板块 / 搜索列表页、发帖页、用户主页共用：
 * 时间格式化、卡片 / 弱化文本工厂、头像标签（自动加载站内图片）、
 * 单行省略文本、分页条、可点卡片与标签胶囊。
 * 颜色一律通过 @TOKEN@ 占位符 + StyleKit::resolve 解析，跟随主题。
 */
#pragma once

#include <QColor>
#include <QFont>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVector>
#include <QWidget>

#include <functional>

class QLayout;
class QPaintEvent;
class QPlainTextEdit;
class QVariantAnimation;

namespace ForumKit
{
/** 相对时间：刚刚 / N 分钟前 / N 小时前 / N 天前 / 超过一周显示日期 */
QString relativeTime(qint64 seconds);

/** 文件大小给人看：B / KB / MB（插件包等附件卡片用） */
QString humanFileSize(qint64 bytes);

/** 递归清空布局里的控件 */
void clearLayout(QLayout *layout);

/** 圆角卡片 */
QFrame *makeCard(QWidget *parent, const QString &objectName = QStringLiteral("forumCard"));

/** 弱化说明文字 */
QLabel *makeMuted(QWidget *parent, const QString &text = QString());

/** 区块标题 */
QLabel *makeSectionTitle(QWidget *parent, const QString &text);

/**
 * @brief 圆形头像标签：有站内头像时异步加载图片，否则显示首字母占位
 * @param size 直径（像素）
 */
QLabel *makeAvatar(QWidget *parent, const QString &avatarPath, const QString &name, int size);

/**
 * @brief 群头像：icon 是内置表情（如 👥）或站内图片路径，空则按群名首字占位
 * @param size 直径（像素）
 */
QLabel *makeGroupAvatar(QWidget *parent, const QString &icon, const QString &name, int size);

/**
 * @brief 官方头像标签：绿色圆角底 + 方块盒子立方体图标
 *
 * 官方客服与官方全用户群统一用它当头像，与普通用户 / 群的首字母占位区分开。
 * @param size 直径（像素）
 */
QLabel *makeOfficialAvatar(QWidget *parent, int size);

/**
 * @brief 个人主页封面横幅：有封面时按比例铺满并居中裁切，没有时画主题色渐变
 *
 * 顶部两角按卡片圆角裁掉，直接贴在卡片顶部不会露出方角。
 */
class CoverBanner : public QWidget
{
    Q_OBJECT

public:
    explicit CoverBanner(QWidget *parent = nullptr);

    /** 圆角半径（只有顶部两角），默认与卡片一致 */
    void setRadius(int radius);

    /** 设置封面（站内路径）；空字符串表示没有封面，画渐变占位 */
    void setCover(const QString &coverPath);

    /** 当前封面路径 */
    QString coverPath() const { return m_path; }

    /**
     * @brief 显示宽高比（宽 / 高），默认 3:1
     *
     * 和网页版、上传时的裁剪比例保持一致：横幅高度跟着卡片宽度走，
     * 3:1 的封面原样铺满，不再因为固定高度被上下裁掉一截。
     */
    void setAspectRatio(qreal ratio);
    qreal aspectRatio() const { return m_aspectRatio; }

    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_path;
    QImage m_image;
    int m_radius = 16;
    qreal m_aspectRatio = 3.0;
};

/** 单行省略文本标签 */
QLabel *makeElided(QWidget *parent, const QString &text, int maxWidth = 0);

/** 标签胶囊按钮（点击跳到该标签的主题列表） */
QPushButton *makeTagChip(QWidget *parent, const QString &tag);

/** 置顶 / 锁定等小徽章 */
QLabel *makeBadge(QWidget *parent, const QString &text, bool warn = false);

/** Markdown 预设：菜单里显示的名称 + 插进正文的整段模板 */
struct MarkdownPreset
{
    QString label;
    QString text;
};

/** 发帖正文预设（求助 / 教程 / 作品分享 / 插件介绍 / 意见建议） */
QVector<MarkdownPreset> postPresets();

/** 教程帖专用预设（图文教程 / 视频教程 / 攻略合集），发教程模式的预设菜单用 */
QVector<MarkdownPreset> tutorialPresets();

/** 联机大厅「详细介绍」预设（生存 / 建筑 / 小游戏 / 联机须知） */
QVector<MarkdownPreset> lobbyIntroPresets();

/**
 * @brief 把预设模板插进 Markdown 输入框
 *
 * 正文空着就整段铺满，已经有内容就在光标处另起一段插入；
 * 插完自动选中标题下面那行提示文字，直接打字就能替换。
 */
void applyMarkdownPreset(QPlainTextEdit *edit, const QString &text);

/**
 * @brief 在选区两侧包上 Markdown 包裹符（**、*、~~、`）
 *
 * 选区两侧已经是同一对包裹符时反向拆掉；选区为空就插入占位词。
 * 完成后选中包住的内容，直接打字就能替换。
 */
void mdWrapSelection(QPlainTextEdit *edit, const QString &marker, const QString &placeholder);

/**
 * @brief 对选区覆盖的每一行做「加 / 去前缀」的开关式转换
 * @param kind heading（## 标题）/ quote（> 引用）/ ul（- 列表）/ ol（1. 编号）
 *
 * 选中的行都带前缀时统一去掉，否则统一加上；编号按行序号生成。
 */
void mdToggleLines(QPlainTextEdit *edit, const QString &kind);

/** 把选区（或光标所在行）整段包进 ``` 围栏代码块，原内容留在块里方便直接改 */
void mdWrapCodeBlock(QPlainTextEdit *edit);

/** 插入 [标题](链接)：选中的是链接就反过来包标题，选的是文字就选中链接位让用户填 */
void mdInsertLink(QPlainTextEdit *edit);

/** 在光标处插入一段 Markdown 文本（替换选区），插入后光标停在末尾 */
void mdInsertText(QPlainTextEdit *edit, const QString &text);

/**
 * @brief 整张卡片可点击（点击空白处也算），列表行用
 */
class ClickableCard : public QFrame
{
    Q_OBJECT

public:
    explicit ClickableCard(QWidget *parent = nullptr);

    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
};

/**
 * @brief 单行省略文本（宽度变化时重新省略）
 */
class ElidedLabel : public QLabel
{
    Q_OBJECT

public:
    explicit ElidedLabel(QWidget *parent = nullptr);

    void setFullText(const QString &text);
    QString fullText() const { return m_text; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyElide();

    QString m_text;
};

/**
 * @brief 分页条：上一页 / 第 x / y 页 / 下一页
 */
class ForumPager : public QWidget
{
    Q_OBJECT

public:
    explicit ForumPager(QWidget *parent = nullptr);

    void setPage(int current, int total);
    int currentPage() const { return m_current; }
    int totalPages() const { return m_total; }

signals:
    void pageRequested(int page);

private:
    void emitPage(int page);

    QPushButton *m_prevButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QLabel *m_label = nullptr;
    int m_current = 1;
    int m_total = 1;
};

/**
 * @brief 点赞按钮：心形 SVG 图标 + 计数
 *
 * 未点赞是灰色空心，点过之后变红（实心），点下去的一瞬间图标会「心跳」
 * 放大回弹，心形后面还会散开一圈红光。整个控件自己绘制，不跟着
 * 全局 QPushButton 样式走，高度固定 28，和旁边「回复 / 引用」等胶囊按钮齐平。
 */
class LikeButton : public QPushButton
{
    Q_OBJECT

public:
    explicit LikeButton(QWidget *parent = nullptr);

    /** 点赞数（按钮右边的数字，重建时直接给最终值，不播动画） */
    void setLikes(int likes);
    int likes() const { return m_likes; }

    /**
     * @brief 切换点赞状态
     * @param animate 状态真的变了且为 true 时播放一次点赞动画
     */
    void setLiked(bool liked, bool animate = false);
    bool isLiked() const { return m_liked; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void applySize();
    void playBurst(bool liked);

    bool m_liked = false;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_focusVisible = false;   ///< 只有用键盘切过来的焦点才描边
    int m_likes = 0;
    qreal m_t = 0.0;        ///< 动画进度 0~1（0 表示没在播）
    bool m_burstLiked = true;   ///< 本次动画是点赞还是取消
    QColor m_likeColor;     ///< 点赞红（取主题里的危险色，深色主题也够亮）
    QColor m_iconTint;      ///< 空心心形当前染成的灰色，换主题时重画
    QPixmap m_outline;      ///< 空心心形（未点赞）
    QPixmap m_filled;       ///< 实心心形（已点赞）
    QFont m_font;
    QVariantAnimation *m_anim = nullptr;
};

} // namespace ForumKit
