/**
 * @file   LocalModDetailPage.cpp
 * @brief  本地模组详情页实现 - 展示从本地 JAR 文件提取的模组信息
 * @author BlockBox Team
 * @date   2026-06-20
 */
#include "LocalModDetailPage.h"
#include "utils/mod/ModLinkResolver.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include "utils/ThemeManager.h"

namespace {
bool localModDark()
{
    return ThemeManager::instance()->currentTheme() != ThemeManager::LightTheme;
}

QString localModGray(int lightGray, int darkGray)
{
    const int v = localModDark() ? darkGray : lightGray;
    return QColor(v, v, v).name();
}
}

LocalModDetailPage::LocalModDetailPage(QWidget *parent)
    : QWidget(parent)
    , m_scrollArea(nullptr)
    , m_contentWidget(nullptr)
    , m_contentLayout(nullptr)
    , m_iconLabel(nullptr)
    , m_titleLabel(nullptr)
    , m_subtitleLabel(nullptr)
    , m_versionLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_infoContainer(nullptr)
    , m_infoLayout(nullptr)
    , m_linksContainer(nullptr)
    , m_linksLayout(nullptr)
    , m_introContainer(nullptr)
    , m_introLayout(nullptr)
    , m_linkResolver(nullptr)
{
    initUI();
}

LocalModDetailPage::~LocalModDetailPage()
{
}

void LocalModDetailPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // 滚动区域
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("QScrollArea { background: transparent; }");

    m_contentWidget = new QWidget();
    m_contentWidget->setObjectName("localModDetailContent");
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(20, 20, 20, 20);
    m_contentLayout->setSpacing(16);

    // Header 区域
    QWidget *headerWidget = new QWidget();
    headerWidget->setObjectName("localModDetailHeader");
    QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(20, 20, 20, 20);
    headerLayout->setSpacing(20);

    // 图标
    m_iconLabel = new QLabel();
    m_iconLabel->setObjectName("localModIcon");
    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_iconLabel);

    // 名称 + 版本
    QVBoxLayout *nameLayout = new QVBoxLayout();
    nameLayout->setSpacing(6);

    m_titleLabel = new QLabel();
    m_titleLabel->setObjectName("localModTitle");
    nameLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel();
    m_subtitleLabel->setObjectName("localModSubtitle");
    nameLayout->addWidget(m_subtitleLabel);

    m_versionLabel = new QLabel();
    m_versionLabel->setObjectName("localModVersion");
    nameLayout->addWidget(m_versionLabel);

    headerLayout->addLayout(nameLayout, 1);

    m_statusLabel = new QLabel();
    m_statusLabel->setObjectName("localModStatus");
    m_statusLabel->setFixedHeight(28);
    headerLayout->addWidget(m_statusLabel);

    m_contentLayout->addWidget(headerWidget);

    // 信息卡片
    m_infoContainer = new QWidget();
    m_infoContainer->setObjectName("localModInfoCard");
    m_infoLayout = new QVBoxLayout(m_infoContainer);
    m_infoLayout->setContentsMargins(20, 20, 20, 20);
    m_infoLayout->setSpacing(12);
    m_contentLayout->addWidget(m_infoContainer);

    // 相关链接卡片
    m_linksContainer = new QWidget();
    m_linksContainer->setObjectName("localModInfoCard");
    m_linksLayout = new QVBoxLayout(m_linksContainer);
    m_linksLayout->setContentsMargins(20, 20, 20, 20);
    m_linksLayout->setSpacing(10);
    m_contentLayout->addWidget(m_linksContainer);

    // 链接解析中的提示标签
    m_resolvingLabel = new QLabel(tr("正在解析 CurseForge / Modrinth / MC百科 链接..."));
    m_resolvingLabel->setStyleSheet(
        QString("font-size: 12px; color: %1; background: transparent; padding: 8px 0;")
            .arg(localModGray(153, 155)));
    m_resolvingLabel->hide();

    // 简介卡片
    m_introContainer = new QWidget();
    m_introContainer->setObjectName("localModInfoCard");
    m_introLayout = new QVBoxLayout(m_introContainer);
    m_introLayout->setContentsMargins(20, 20, 20, 20);
    m_introLayout->setSpacing(12);
    m_contentLayout->addWidget(m_introContainer);

    m_contentLayout->addStretch();
    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea, 1);

    // 链接解析器
    m_linkResolver = new ModLinkResolver(this);
    connect(m_linkResolver, &ModLinkResolver::linksResolved, this, [this](const ModInfo &resolvedInfo) {
        if (resolvedInfo.id == m_currentModInfo.id ||
            resolvedInfo.fileName == m_currentModInfo.fileName)
        {
            // 合并已解析的链接和标识到当前信息
            if (!resolvedInfo.curseforgeUrl.isEmpty()) {
                m_currentModInfo.curseforgeUrl = resolvedInfo.curseforgeUrl;
                m_currentModInfo.id = resolvedInfo.id;
                m_currentModInfo.source = QStringLiteral("curseforge");
            }
            if (!resolvedInfo.modrinthUrl.isEmpty()) {
                m_currentModInfo.modrinthUrl = resolvedInfo.modrinthUrl;
                m_currentModInfo.id = resolvedInfo.id;
                m_currentModInfo.source = QStringLiteral("modrinth");
            }
            if (!resolvedInfo.mcmodUrl.isEmpty())
                m_currentModInfo.mcmodUrl = resolvedInfo.mcmodUrl;
            rebuildLinksSection(m_currentModInfo);
        }
    });
    connect(m_linkResolver, &ModLinkResolver::allDone, this, [this]() {
        m_resolvingLabel->hide();
        m_resolveTimeout->stop();
    });

    // 安全超时：15 秒后自动隐藏解析提示
    m_resolveTimeout = new QTimer(this);
    m_resolveTimeout->setSingleShot(true);
    connect(m_resolveTimeout, &QTimer::timeout, this, [this]() {
        m_resolvingLabel->hide();
    });
}

void LocalModDetailPage::clearContent()
{
    m_resolveTimeout->stop();

    // 清除原有信息行
    while (m_infoLayout->count() > 0)
    {
        QLayoutItem *item = m_infoLayout->takeAt(0);
        if (item->widget())
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    // 清除链接卡片（保留 m_resolvingLabel）
    for (int i = m_linksLayout->count() - 1; i >= 0; --i)
    {
        QLayoutItem *item = m_linksLayout->itemAt(i);
        if (item->widget() && item->widget() != m_resolvingLabel)
        {
            item->widget()->deleteLater();
            m_linksLayout->removeItem(item);
            delete item;
        }
    }

    // 清除简介卡片
    while (m_introLayout->count() > 0)
    {
        QLayoutItem *item = m_introLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

void LocalModDetailPage::rebuildLinksSection(const ModInfo &info)
{
    // 清除链接卡片（保留 m_resolvingLabel 不被删除）
    for (int i = m_linksLayout->count() - 1; i >= 0; --i)
    {
        QLayoutItem *item = m_linksLayout->itemAt(i);
        if (item->widget() && item->widget() != m_resolvingLabel)
        {
            m_linksLayout->takeAt(i);
            item->widget()->deleteLater();
            delete item;
        }
    }

    addSectionTitle(m_linksContainer, tr("相关链接"));

    // 解析中的提示标签（可见性由 setModInfo/allDone 管理）
    m_resolvingLabel->setParent(m_linksContainer);
    m_linksLayout->addWidget(m_resolvingLabel);

    QWidget *linksRow = new QWidget();
    QHBoxLayout *linksRowLayout = new QHBoxLayout(linksRow);
    linksRowLayout->setContentsMargins(0, 0, 0, 0);
    linksRowLayout->setSpacing(8);

    auto addLink = [&](const QString &text, const QString &url) {
        QPushButton *btn = new QPushButton(text);
        btn->setObjectName("modDetailLinkBtn");
        btn->setFixedWidth(100);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [url]() {
            QDesktopServices::openUrl(QUrl(url));
        });
        linksRowLayout->addWidget(btn);
    };

    // 在线详情（内置详情页入口 — 当解析出 CF/MR 来源时显示）
    if (!info.curseforgeUrl.isEmpty() || !info.modrinthUrl.isEmpty())
    {
        QPushButton *onlineBtn = new QPushButton(tr("在线详情"));
        onlineBtn->setObjectName("modDetailLinkBtn");
        onlineBtn->setFixedWidth(100);
        onlineBtn->setCursor(Qt::PointingHandCursor);
        QString themeColor = ThemeManager::instance()->currentThemeColor();
        onlineBtn->setStyleSheet(
            QString("QPushButton { font-size: 12px; padding: 6px 12px; border-radius: 6px; "
                    "background-color: %1; color: white; border: none; }"
                    "QPushButton:hover { opacity: 0.8; }").arg(themeColor));
        connect(onlineBtn, &QPushButton::clicked, this, [this, info]() {
            // 组装带正确 source 的 ModInfo
            ModInfo remoteInfo = info;
            if (!info.source.isEmpty()) {
                remoteInfo.source = info.source;
            } else if (!info.curseforgeUrl.isEmpty()) {
                remoteInfo.source = "curseforge";
            } else if (!info.modrinthUrl.isEmpty()) {
                remoteInfo.source = "modrinth";
            }
            emit remoteDetailRequested(remoteInfo);
        });
        linksRowLayout->addWidget(onlineBtn);
    }

    // 官网链接（来自 JAR 元数据）
    if (!info.homepageUrl.isEmpty())
        addLink(tr("官网链接"), info.homepageUrl);

    // 报告问题（来自 JAR 元数据）
    if (!info.issuesUrl.isEmpty())
        addLink(tr("报告问题"), info.issuesUrl);

    // CurseForge（通过 ModLinkResolver 解析）
    if (!info.curseforgeUrl.isEmpty())
        addLink("CurseForge", info.curseforgeUrl);

    // Modrinth（通过 ModLinkResolver 解析）
    if (!info.modrinthUrl.isEmpty())
        addLink("Modrinth", info.modrinthUrl);

    // MC百科（通过 ModLinkResolver 解析）
    if (!info.mcmodUrl.isEmpty())
        addLink(tr("MC百科"), info.mcmodUrl);

    // 必应搜索
    {
        QString searchName = info.chineseName.isEmpty() ? (info.englishName.isEmpty() ? info.name : info.englishName) : info.chineseName;
        addLink(tr("必应搜索"), QString("https://www.bing.com/search?q=%1+minecraft+mod").arg(QString(QUrl::toPercentEncoding(searchName))));
    }

    // 百度搜索
    {
        QString searchName = info.chineseName.isEmpty() ? (info.englishName.isEmpty() ? info.name : info.englishName) : info.chineseName;
        addLink(tr("百度搜索"), QString("https://www.baidu.com/s?wd=%1+minecraft+mod").arg(QString(QUrl::toPercentEncoding(searchName))));
    }

    // 问问AI
    {
        QString searchName = info.chineseName.isEmpty() ? (info.englishName.isEmpty() ? info.name : info.englishName) : info.chineseName;
        addLink(tr("问问AI"), QString("https://www.bing.com/search?q=%1+minecraft+mod+介绍").arg(QString(QUrl::toPercentEncoding(searchName))));
    }

    linksRowLayout->addStretch();
    m_linksLayout->addWidget(linksRow);

    // 显示/隐藏解析中的提示：有平台链接时隐藏，否则显示
    bool hasPlatformLinks = !info.curseforgeUrl.isEmpty()
        || !info.modrinthUrl.isEmpty()
        || !info.mcmodUrl.isEmpty();
    m_resolvingLabel->setVisible(!hasPlatformLinks && !info.homepageUrl.isEmpty());
    // 如果已经有预解析的链接或没有任何元数据，则不显示等待提示
    if (hasPlatformLinks || (info.homepageUrl.isEmpty() && info.issuesUrl.isEmpty()))
        m_resolvingLabel->hide();
}

QPushButton* LocalModDetailPage::addInfoRow(const QString &label, const QString &value, bool clickable)
{
    if (value.isEmpty())
        return nullptr;

    QWidget *row = new QWidget();
    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 4, 0, 4);

    QLabel *keyLabel = new QLabel(label);
    keyLabel->setStyleSheet(
        QString("font-size: 13px; font-weight: 600; color: %1; background: transparent; min-width: 80px;")
            .arg(localModGray(136, 176)));
    rowLayout->addWidget(keyLabel);

    QPushButton *btn = nullptr;

    if (clickable)
    {
        QString themeColor = ThemeManager::instance()->currentThemeColor();
        btn = new QPushButton(value);
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString("QPushButton { font-size: 13px; color: %1; background: transparent; border: none; text-align: left; }"
                    "QPushButton:hover { color: %2; text-decoration: underline; }")
                .arg(themeColor, themeColor));
        rowLayout->addWidget(btn, 1);
    }
    else
    {
        QLabel *valLabel = new QLabel(value);
        valLabel->setWordWrap(true);
        valLabel->setStyleSheet(
            QString("font-size: 13px; color: %1; background: transparent;")
                .arg(localModGray(51, 210)));
        rowLayout->addWidget(valLabel, 1);
    }

    m_infoLayout->addWidget(row);
    return btn;
}

void LocalModDetailPage::addSectionTitle(QWidget *container, const QString &title)
{
    QLabel *titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(
        QString("font-size: 15px; font-weight: bold; color: %1; background: transparent; padding-bottom: 4px;")
            .arg(localModGray(51, 225)));
    container->layout()->addWidget(titleLabel);
}

QString LocalModDetailPage::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

void LocalModDetailPage::setModInfo(const ModInfo &info)
{
    clearContent();

    // 保存当前模组信息，供链接解析回调使用
    m_currentModInfo = info;

    // 图标：首字母头像
    {
        QPixmap iconPix(72, 72);
        iconPix.fill(QColor("#4CAF50"));
        QPainter painter(&iconPix);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::white);
        QFont font;
        font.setBold(true);
        font.setPixelSize(32);
        painter.setFont(font);
        QString letter = info.name.isEmpty() ? "?" : info.name.left(1).toUpper();
        painter.drawText(iconPix.rect(), Qt::AlignCenter, letter);
        painter.end();
        m_iconLabel->setPixmap(iconPix);
    }

    // 标题：优先中文名 > 英文名 > 原始名
    QString title;
    QString subtitle;
    QString version = info.latestVersion;

    if (!info.chineseName.isEmpty())
    {
        title = info.chineseName;
        if (!info.englishName.isEmpty() && info.englishName != info.chineseName)
            subtitle = info.englishName;
    }
    else if (!info.englishName.isEmpty())
    {
        title = info.englishName;
    }
    else
    {
        title = info.name.isEmpty() ? info.fileName : info.name;
    }

    m_titleLabel->setText(title);
    m_subtitleLabel->setText(subtitle);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
    m_versionLabel->setText(version.isEmpty() ? QString() : tr("v%1").arg(version));
    m_versionLabel->setVisible(!version.isEmpty());

    // 状态标签
    if (info.enabled)
    {
        m_statusLabel->setText(tr("已启用"));
        m_statusLabel->setProperty("status", QStringLiteral("enabled"));
    }
    else
    {
        m_statusLabel->setText(tr("已禁用"));
        m_statusLabel->setProperty("status", QStringLiteral("disabled"));
    }
    m_statusLabel->style()->polish(m_statusLabel);

    // 信息行
    addInfoRow(tr("模组 ID"), info.id);
    addInfoRow(tr("原始名"), info.name);
    addInfoRow(tr("加载器"), info.loaderType);
    addInfoRow(tr("描述"), info.description);
    addInfoRow(tr("文件大小"), formatFileSize(info.fileSize));
    addInfoRow(tr("文件名"), info.fileName);

    // 文件路径（可点击）
    if (!info.filePath.isEmpty())
    {
        QFileInfo fi(info.filePath);
        QPushButton *pathBtn = addInfoRow(tr("文件路径"), fi.absoluteFilePath(), true);
        if (pathBtn)
        {
            QString absPath = fi.absolutePath();
            connect(pathBtn, &QPushButton::clicked, this, [absPath]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(absPath));
            });
        }
    }

    // 修改时间
    if (info.dateModified.isValid())
    {
        addInfoRow(tr("修改时间"), info.dateModified.toString("yyyy-MM-dd hh:mm:ss"));
    }

    // 作者名称
    addInfoRow(tr("作者"), info.author);

    // ========== 相关链接卡片 ==========
    rebuildLinksSection(info);

    // ========== 简介卡片 ==========
    QString introText = info.detailedDescription.isEmpty() ? info.description : info.detailedDescription;
    if (!introText.isEmpty())
    {
        m_introContainer->show();
        addSectionTitle(m_introContainer, tr("简介"));

        QLabel *introLabel = new QLabel(introText);
        introLabel->setWordWrap(true);
        introLabel->setStyleSheet(
            QString("font-size: 13px; color: %1; background: transparent;")
                .arg(localModGray(85, 200)));
        m_introLayout->addWidget(introLabel);
    }
    else
    {
        m_introContainer->hide();
    }

    // 异步解析 CurseForge / Modrinth / MC百科 链接
    // 如果已经预解析了链接（如 ModNameFetcher 提前解析的 mcmodUrl），跳过重复解析
    bool hasAllLinks = !info.curseforgeUrl.isEmpty()
        && !info.modrinthUrl.isEmpty()
        && !info.mcmodUrl.isEmpty();
    if (!hasAllLinks) {
        m_resolvingLabel->show();
        m_resolveTimeout->start(15000);
        m_linkResolver->resolveLinks(info);
    }
}