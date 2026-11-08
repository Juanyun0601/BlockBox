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

#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <QWidget>

#include <functional>

class QLayout;
class QPaintEvent;
class QPlainTextEdit;

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

} // namespace ForumKit
