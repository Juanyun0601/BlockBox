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
 *   渐变 banner + 徽标（拿不到封面 → 封面区居中放大；封面就位 → 右下半压）
 *   + 名称/描述/chips + 操作按钮
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
    /* 首字母渐变徽标；size = 徽标边长（默认 52 右下角款，居中放大款传更大值） */
    static QPixmap makeLogoPixmap(const QColor &c1, const QColor &c2, const QString &letter,
                                  int size = 52);
    /* 本地图源封面：KeepAspectRatioByExpanding 居中裁剪 + 圆角 16 */
    static QPixmap makeCoverPixmap(const QPixmap &src, int w, int h);
    static void paletteFor(const ModInfo &info, QColor &c1, QColor &c2);
    /* 从图标位图提取"最抢眼"的主色（绝对色彩度 × 亮度加权，排除灰/白/黑与过小色块），
     * 生成同色系渐变（c1 亮 / c2 暗），让居中徽标下方的 banner 底色与图标同色系。
     * 图标为空、或没有可用彩色色块时返回 false，调用方回退 paletteFor */
    static bool paletteForIcon(const QPixmap &icon, QColor &c1, QColor &c2);
    /* 按当前主题整形一对渐变色：浅色模式整体偏亮色系、深色模式整体偏暗色系
     * （调色对内部明暗关系保持不变，仅整体提亮/压暗） */
    static void shapePairForTheme(QColor &c1, QColor &c2);
    /* 圆角 + 白描边图标（logo 用，支持本地/网络图源） */
    static QPixmap makeRoundIcon(const QPixmap &src, int size = 52);

    /* 徽标边长：无封面 → 封面区居中放大；有封面 → 右下半压小尺寸 */
    static int logoSizeFor(bool centered, int bannerH);
    /* 徽标定位：centered=true 放封面区正中，false 收回右下角（半压封面下缘）。
     * 开启 scaledContents，封面迟到/徽标尺寸切换时按标签框自动缩放，避免裁剪 */
    static void placeLogo(QLabel *logo, bool centered, int bannerW, int bannerH);

    /* 通用卡片投影（Qt QSS 无 box-shadow，用图形效果替代）
     * 供列表式卡片（modCardListItem）等复用，与瀑布流卡片视觉一致 */
    static void applyShadow(QWidget *w, int blurRadius = 18, int offsetY = 2,
                            int alpha = 40);

    /* 稳定 hash → banner 高度（瀑布流错落） */
    static int bannerHeightFor(const ModInfo &info);
};

#endif // MASONRYCONTENTCARD_H
