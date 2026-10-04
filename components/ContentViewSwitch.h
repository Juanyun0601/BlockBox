#ifndef CONTENTVIEWSWITCH_H
#define CONTENTVIEWSWITCH_H

#include <QWidget>
#include <QSettings>

class QPushButton;

/* ============================================================
 * ContentViewSwitch — 列表式 / 瀑布流 视图切换分段控件
 * 对齐 BlockBoxPrototype 的 .view-switch 设计
 * - 两个图标按钮（列表三横线 / 瀑布流四宫格）
 * - active 态由 QSS 属性 [active="true"] 驱动
 * - 视图偏好经 QSettings 持久化（按 pageKey 隔离）
 * ============================================================ */
class ContentViewSwitch : public QWidget
{
    Q_OBJECT
public:
    enum ViewMode { List = 0, Masonry = 1 };
    Q_ENUM(ViewMode)

    explicit ContentViewSwitch(QWidget *parent = nullptr);
    ~ContentViewSwitch() override = default;

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_mode; }

    /* 持久化辅助 */
    static QString persistedKey(const QString &pageKey);
    static ViewMode loadPersisted(const QString &pageKey, ViewMode fallback = Masonry);
    static void savePersisted(const QString &pageKey, ViewMode mode);

signals:
    void viewModeChanged(ContentViewSwitch::ViewMode mode);

private:
    void updateButtons();

    ViewMode m_mode = Masonry;
    QPushButton *m_listBtn = nullptr;
    QPushButton *m_masonryBtn = nullptr;
};

#endif // CONTENTVIEWSWITCH_H
