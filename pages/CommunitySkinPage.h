/**
 * @file   CommunitySkinPage.h
 * @brief  资源页「皮肤」子页：方块皮肤站皮肤库
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 数据来自方块皮肤站（skin.mcblockbox.top）的皮肤库 /api/library，
 * 卡片外观与「模组」「投影」等社区资源列表页一致。
 * 点卡片进入整页的作品详情（CommunitySkinDetailPage），
 * 在详情页里 3D 预览、穿上到皮肤站角色（进游戏生效）、
 * 应用为离线账户皮肤（Images/Skins/{用户名}.png），或导出 PNG。
 */
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QVector>
#include <QWidget>

#include <functional>

#include "SkinLibraryItem.h"

class QComboBox;
class QEvent;
class QGridLayout;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QScrollArea;

class CommunitySkinPage : public QWidget
{
    Q_OBJECT

public:
    explicit CommunitySkinPage(QWidget *parent = nullptr);

    /** 打开页面并从第 1 页重新拉取 */
    void openPage();

    /** 子导航搜索框分发：同步到页内搜索框（走服务端关键字搜索） */
    Q_INVOKABLE void applyPageFilter(const QString &query);

signals:
    /** 未登录 / 登录失效时提示跳账户管理页 */
    void loginRequested();

    /** 点皮肤卡：请求打开这件作品的整页详情 */
    void skinDetailRequested(const SkinLibraryItem &item);

private:
    void buildUi();
    void loadPage(int page);
    void renderLoaded();
    void showStatus(const QString &text, bool error = false);
    void updatePager();

    // ── 网络小工具（与皮肤站整页 / 详情页同款逻辑） ──
    QString skinSiteBaseUrl() const;
    using JsonCb = std::function<void(bool ok, int httpStatus, const QJsonObject &root)>;
    void sendRequest(const QString &path, const QString &method, const QByteArray &contentType,
                     const QByteArray &body, JsonCb done);
    void getJson(const QString &path, JsonCb done);
    void fetchPng(const QString &url, const std::function<void(bool ok, const QByteArray &)> &done);

    QComboBox *m_sortCombo = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_gridHost = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    QWidget *m_paginationBar = nullptr;
    QPushButton *m_firstButton = nullptr;
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_lastButton = nullptr;
    QLabel *m_pageLabel = nullptr;

    QNetworkAccessManager *m_net = nullptr;
    QHash<QString, QByteArray> m_pngCache;   ///< 皮肤 PNG 原始字节缓存（按 URL）
    QVector<SkinLibraryItem> m_skins;
    int m_page = 1;
    int m_totalPages = 1;
    int m_total = 0;
    QString m_sort = QStringLiteral("hot");
    QString m_model;                         ///< 空 = 全部模型
    QString m_query;                         ///< 服务端关键字搜索

protected:
    /** 点皮肤卡：打开这件作品的整页详情 */
    bool eventFilter(QObject *watched, QEvent *event) override;
};
