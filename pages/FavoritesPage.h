/**
 * @file   FavoritesPage.h
 * @brief  收藏夹内容展示页面
 * @author BlockBox Team
 * @date   2026-07-18
 */
#ifndef FAVORITESPAGE_H
#define FAVORITESPAGE_H

#include <QLabel>
#include <QList>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include "../components/ContentViewSwitch.h"
#include "../components/MasonryContentCard.h"

#include "utils/FavoritesManager.h"

class QGridLayout;
class FlowLayout;
class ContentViewSwitch;
class BlurLoadingOverlay;
class QNetworkAccessManager;
class QNetworkDiskCache;

class FavoritesPage : public QWidget
{
    Q_OBJECT

public:
    explicit FavoritesPage(QWidget *parent = nullptr);
    ~FavoritesPage();

    void setFolder(const QString &folderId);
    QString folderId() const { return m_folderId; }

signals:
    void favoriteClicked(const FavoriteItem &item);
    void backRequested();
    void manageFoldersRequested();

private slots:
    void onRefreshClicked();
    void onCardContextMenu(const QPoint &pos);

private:
    void initUI();
    void rebuildHeader();
    void clearCards();
    void addCard(const FavoriteItem &item);
    void addListCard(const FavoriteItem &item);
    void addMasonryCard(const FavoriteItem &item);
    QList<MasonryContentCard::ActionSpec> buildMasonryActions(const FavoriteItem &item);
    void placeCards();
    void rebuildCards();
    void onViewModeChanged(ContentViewSwitch::ViewMode mode);
    int columnCount() const;
    void showEmptyHint(bool show);
    void loadCardIcon(QLabel *iconLabel, const QString &iconUrl);
    bool eventFilter(QObject *watched, QEvent *event) override;

    QString m_folderId;
    QString m_folderName;
    QList<FavoriteItem> m_items;

    // UI
    QWidget *m_headerWidget;
    QLabel *m_titleLabel;
    QLabel *m_countLabel;
    QPushButton *m_refreshBtn;
    QPushButton *m_manageBtn;

    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    FlowLayout *m_cardFlowLayout;
    ContentViewSwitch *m_viewSwitch;
    int m_viewMode;
    QLabel *m_emptyLabel;

    QList<QWidget *> m_cardWidgets;
    QList<FavoriteItem> m_cardItems;

    QNetworkAccessManager *m_iconNAM;
    QNetworkDiskCache *m_iconCache;
};

#endif // FAVORITESPAGE_H
