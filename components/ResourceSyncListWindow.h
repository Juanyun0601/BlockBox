#ifndef RESOURCESYNCLISTWINDOW_H
#define RESOURCESYNCLISTWINDOW_H

#include <QList>
#include <QPoint>
#include <QWidget>

#include "utils/ResourceVersionSyncer.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/**
 * @brief 未适配资源清单悬浮窗
 *
 * 修改实例版本后，「没有适配新版本的资源」清单可以在这里常驻查看并逐项处理：
 * 每项可删除（二次确认）或保留，底部可整体交给 AI 找替代。清单清空后自动关闭。
 */
class ResourceSyncListWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ResourceSyncListWindow(QWidget *parent = nullptr);
    ~ResourceSyncListWindow() override;

    /** 填充清单内容（覆盖式重建列表） */
    void setItems(const QList<ResourceSyncItem> &items, const QString &newVersion);

signals:
    /** 点击「让 AI 找替代」：携带当前仍在清单里的资源 */
    void findReplacementRequested(const QList<ResourceSyncItem> &items);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildList();
    void removeItem(const QString &filePath);
    void keepAll();
    void requestAiReplacement();
    void updateCountLabel();
    void applyThemeStyles();

    QList<ResourceSyncItem> m_items; ///< 仍在清单里的待处理资源
    QString m_newVersion;
    bool m_centeredOnce = false;

    QWidget *m_titleBar;
    QLabel *m_titleLabel;
    QLabel *m_countLabel;
    QLabel *m_hintLabel;
    QPushButton *m_closeBtn;
    QScrollArea *m_scrollArea;
    QWidget *m_listContainer;
    QVBoxLayout *m_listLayout;
    QLabel *m_emptyLabel;
    QPushButton *m_keepAllBtn;
    QPushButton *m_aiBtn;

    bool m_dragging = false;
    QPoint m_dragOrigin;

    static constexpr int kShadowMargin = 12; ///< 外圈阴影边距（留出投影绘制空间）
    static constexpr int kCornerRadius = 12;
};

#endif // RESOURCESYNCLISTWINDOW_H
