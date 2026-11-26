/**
 * @file   CommunitySkinDetailPage.h
 * @brief  皮肤库作品详情页：方块皮肤站皮肤库的一件作品（整页展示）
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 从「资源 > 皮肤」皮肤库点卡片进入，不再弹小窗：
 *   - 左侧大号 3D 预览（可切正面 / 左侧 / 背面 / 右侧、自动旋转），
 *   - 右侧作品信息（名称 / 作者 / 模型 / 点赞 / 下载 / 收藏 / 评论 / 上传日期），
 *   - 底部动作：穿上到皮肤站角色（他人作品自动先复制进自己的衣柜）、
 *     应用为离线账户皮肤（Images/Skins/{用户名}.png）、导出 PNG。
 *
 * 网络请求沿用皮肤库列表页那一套（论坛登录令牌直连皮肤站），
 * 详情接口 /api/textures/{id} 会把点赞、收藏、评论数、上传日期补全。
 */
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QWidget>

#include <functional>

#include "SkinLibraryItem.h"

class QBoxLayout;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class Skin3DWidget;

class CommunitySkinDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit CommunitySkinDetailPage(QWidget *parent = nullptr);

    /** 打开一件作品的详情：先用列表里已有的字段铺开，再拉一次详情补全 */
    void openItem(const SkinLibraryItem &item);

    /** 当前展示的作品（还没打开任何作品时 id 为 -1） */
    SkinLibraryItem item() const { return m_item; }

    /** 页面切换动画结束后重绘 3D 预览（动画的透明效果会破坏 GL 渲染） */
    void refreshPreviewAfterTransition();

public slots:
    /** 顶栏「刷新」：重新拉一次当前作品的详情 */
    void refresh();

signals:
    /** 没登录 / 登录失效：请求跳账户管理页登录 */
    void loginRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void applyStyle();
    /** 宽屏左右两栏，窄屏（手机 / 小窗口）上下堆叠 */
    void applyLayoutMode();
    void renderItem();
    /** 拉一次 /api/textures/{id}：补全点赞 / 收藏 / 评论 / 上传日期，纹理换了也跟着换 */
    void loadDetail();
    /** 按当前作品的地址下载纹理并贴到 3D 预览上 */
    void loadPreviewTexture();
    void setStatus(const QString &text, bool error = false);
    void setBusy(bool busy);

    // ── 动作：穿上到皮肤站 / 上传副本再穿 / 应用为离线皮肤 / 导出 PNG ──
    void applyToSkinStation();
    /** 他人作品不能直接穿时：下载一份上传到自己的衣柜再穿上 */
    void uploadCopyAndApply();
    void applyToOfflineAccount();
    void downloadPng();

    // ── 网络小工具（与皮肤库列表页同款逻辑） ──
    QString skinSiteBaseUrl() const;
    using JsonCb = std::function<void(bool ok, int httpStatus, const QJsonObject &root)>;
    void sendRequest(const QString &path, const QString &method, const QByteArray &contentType,
                     const QByteArray &body, JsonCb done);
    void getJson(const QString &path, JsonCb done);
    void postJson(const QString &path, const QJsonObject &body, JsonCb done);
    void postPng(const QString &path, const QByteArray &png, JsonCb done);
    void fetchPng(const QString &url, const std::function<void(bool ok, const QByteArray &)> &done);
    static int extractTextureId(const QJsonObject &root);

    SkinLibraryItem m_item;

    QScrollArea *m_scroll = nullptr;
    QBoxLayout *m_bodyLayout = nullptr;    ///< 宽屏左右 / 窄屏上下的两栏
    QWidget *m_previewCard = nullptr;
    QWidget *m_infoCard = nullptr;
    Skin3DWidget *m_preview3D = nullptr;
    QPushButton *m_rotateButton = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_authorLabel = nullptr;
    QLabel *m_modelChip = nullptr;
    QLabel *m_stateChip = nullptr;     ///< 我的作品 / 私密
    QLabel *m_statsLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_applyStationBtn = nullptr;
    QPushButton *m_applyOfflineBtn = nullptr;
    QPushButton *m_downloadBtn = nullptr;

    QNetworkAccessManager *m_net = nullptr;
    QHash<QString, QByteArray> m_pngCache;   ///< 皮肤 PNG 原始字节缓存（按 URL）
    int m_layoutMode = -1;                  ///< 当前版面（宽屏 / 窄屏），避免 resize 反复重排
    bool m_busy = false;                    ///< 动作进行中（按钮统一禁用）
};
