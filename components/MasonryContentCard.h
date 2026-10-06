#ifndef MASONRYCONTENTCARD_H
#define MASONRYCONTENTCARD_H

#include <QColor>
#include <QList>
#include <QString>
#include <QWidget>
#include <functional>

struct ModInfo;
class QLabel;

/* ============================================================
 * MasonryContentCard — 瀑布流内容卡片构建器（无状态，纯静态）
 * 对齐 BlockBoxPrototype 的 .content-card 设计：
 *   渐变 banner + 半压封面右下 logo + 名称/描述/chips + 操作按钮
 * 多个内容列表页共用（ModDownload / ContentDownload / Favorites 等）
 * ============================================================ */
class MasonryContentCard
{
public:
    struct ActionSpec
    {
        QString iconPath;
        QString tooltip;
        QColor iconColor;    // 空 = 主题色
        std::function<void()> onClick;
    };

    /* 构建瀑布流卡片；actions 为空则不渲染按钮区 */
    static QWidget *build(const ModInfo &info, QWidget *parent,
                          const QList<ActionSpec> &actions = {});

    /* 异步加载封面并按 banner 尺寸圆角裁剪（封面 URL 后到时回填用） */
    static void loadCoverInto(QLabel *banner, const QString &url, int width, int height);

    /* 绘制辅助（供页面自绘复用） */
    static QPixmap makeBannerPixmap(const QColor &c1, const QColor &c2, int width, int height);
    static QPixmap makeLogoPixmap(const QColor &c1, const QColor &c2, const QString &letter);
    static void paletteFor(const ModInfo &info, QColor &c1, QColor &c2);
    /* 圆角 + 白描边图标（logo 用，支持本地/网络图源） */
    static QPixmap makeRoundIcon(const QPixmap &src, int size = 52);

    /* 通用卡片投影（Qt QSS 无 box-shadow，用图形效果替代）
     * 供列表式卡片（modCardListItem）等复用，与瀑布流卡片视觉一致 */
    static void applyShadow(QWidget *w, int blurRadius = 18, int offsetY = 2,
                            int alpha = 40);

    /* 稳定 hash → banner 高度（瀑布流错落） */
    static int bannerHeightFor(const ModInfo &info);
};

#endif // MASONRYCONTENTCARD_H
