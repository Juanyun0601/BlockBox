/**
 * @file   LocalModDetailPage.h
 * @brief  本地模组详情页 - 展示从本地 JAR 文件提取的模组信息
 * @author BlockBox Team
 * @date   2026-06-20
 */
#ifndef LOCALMODDETAILPAGE_H
#define LOCALMODDETAILPAGE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QPointer>
#include <QTimer>
#include "../utils/mod/ModData.h"

class ModLinkResolver;

class LocalModDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit LocalModDetailPage(QWidget *parent = nullptr);
    ~LocalModDetailPage();

    void setModInfo(const ModInfo &info);

signals:
    void backRequested();
    /** 请求跳转到远程在线详情页（ModDetailPage） */
    void remoteDetailRequested(const ModInfo &info);

private:
    void initUI();
    void clearContent();
    void rebuildLinksSection(const ModInfo &info);
    QPushButton* addInfoRow(const QString &label, const QString &value, bool clickable = false);
    void addSectionTitle(QWidget *container, const QString &title);
    QString formatFileSize(qint64 bytes) const;

    ModLinkResolver *m_linkResolver;
    ModInfo m_currentModInfo;

    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QVBoxLayout *m_contentLayout;

    // Header
    QLabel *m_iconLabel;
    QLabel *m_titleLabel;
    QLabel *m_subtitleLabel;
    QLabel *m_versionLabel;
    QLabel *m_statusLabel;

    // Info rows (动态添加)
    QWidget *m_infoContainer;
    QVBoxLayout *m_infoLayout;

    // 相关链接卡片
    QWidget *m_linksContainer;
    QVBoxLayout *m_linksLayout;
    QLabel *m_resolvingLabel;
    QTimer *m_resolveTimeout;

    // 简介卡片
    QWidget *m_introContainer;
    QVBoxLayout *m_introLayout;


};

#endif // LOCALMODDETAILPAGE_H
