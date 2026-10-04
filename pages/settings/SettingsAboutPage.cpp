/**
 * @file   SettingsAboutPage.cpp
 * @brief  关于页 — 展示应用信息、版本、链接等
 * @author BlockBox Team
 */
#include "pages/SettingsPage.h"
#include "utils/UiMetrics.h"

#include <QDesktopServices>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "components/OutlinedLabel.h"
#include "utils/ThemeManager.h"

void SettingsPage::initAboutSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_aboutSettings);
    layout->setContentsMargins(UiMetrics::kPagePadH, 8, UiMetrics::kPagePadH, 24);
    layout->setSpacing(16);

    OutlinedLabel *titleLabel = new OutlinedLabel(tr("关于"), m_aboutSettings);
    titleLabel->setObjectName("sectionTitle");
    layout->addWidget(titleLabel);

    // ── 应用信息卡片 ──
    QVBoxLayout *infoCard = createSettingsCard(layout, tr("应用信息"));

    // Logo 居中显示
    QHBoxLayout *logoRow = new QHBoxLayout();
    logoRow->setContentsMargins(24, 16, 24, 8);
    logoRow->setAlignment(Qt::AlignCenter);

    QLabel *logoLabel = new QLabel(m_aboutSettings);
    logoLabel->setFixedSize(128, 128);
    const bool isDark = (ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme);
    const QString logoPath = isDark
        ? QStringLiteral(":/Images/blockbox_icon_dark.png")
        : QStringLiteral(":/Images/logo.png");
    QPixmap logo(logoPath);
    if (!logo.isNull()) {
        logoLabel->setPixmap(logo.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    logoRow->addWidget(logoLabel);

    infoCard->addLayout(logoRow);

    // 描述
    QHBoxLayout *descRow = appendSettingRow(infoCard, QString(),
        tr("BlockBox 是一款现代化的 Minecraft 启动器，支持 Java 版与基岩版，"
           "提供实例管理、模组下载、AI 助手等丰富功能。"));
    Q_UNUSED(descRow);

    // ── 链接卡片 ──
    QVBoxLayout *linkCard = createSettingsCard(layout, tr("相关链接"));

    auto addLinkRow = [&](const QString &title, const QString &desc, const QString &url) {
        QHBoxLayout *row = appendSettingRow(linkCard, title, desc);
        QPushButton *btn = new QPushButton(tr("打开"), m_aboutSettings);
        btn->setObjectName("primaryButton");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedWidth(80);
        connect(btn, &QPushButton::clicked, [url]() {
            QDesktopServices::openUrl(QUrl(url));
        });
        row->addWidget(btn);
    };

    addLinkRow(tr("官方网站"), tr("访问 BlockBox 官方网站。"), "https://blockbox.app");
    addLinkRow(tr("GitHub"), tr("查看源代码、提交问题与建议。"), "https://github.com/blockbox");
    addLinkRow(tr("用户手册"), tr("查阅使用指南与常见问题。"), "https://blockbox.app/docs");

    // ── 开源许可卡片 ──
    QVBoxLayout *licenseCard = createSettingsCard(layout, tr("开源许可"));

    QHBoxLayout *licenseRow = appendSettingRow(licenseCard,
        tr("开源协议"), tr("BlockBox 基于开源协议发布，感谢以下项目的贡献。"));
    Q_UNUSED(licenseRow);

    QHBoxLayout *qtRow = appendSettingRow(licenseCard,
        "Qt", tr("跨平台 C++ 图形用户界面应用程序框架。"), QString(), true);
    QLabel *qtValue = new QLabel("LGPL / GPL", m_aboutSettings);
    qtValue->setObjectName("settingValueLabel");
    qtRow->addWidget(qtValue);

    // ── 参考开源项目卡片 ──
    QVBoxLayout *opensourceCard = createSettingsCard(layout, tr("参考开源项目"));

    QHBoxLayout *opensourceDescRow = appendSettingRow(opensourceCard, QString(),
        tr("BlockBox 的设计与实现参考了以下优秀的开源项目，特此致谢。"));
    Q_UNUSED(opensourceDescRow);

    // 启动器
    QHBoxLayout *launcherTitleRow = appendSettingRow(opensourceCard,
        tr("启动器"), QString(), QString(), true);
    Q_UNUSED(launcherTitleRow);

    auto addOpenSourceRow = [&](const QString &name, const QString &desc, const QString &license, const QString &url) {
        QHBoxLayout *row = appendSettingRow(opensourceCard, name, desc);
        QLabel *licenseLabel = new QLabel(license, m_aboutSettings);
        licenseLabel->setObjectName("settingValueLabel");
        row->addWidget(licenseLabel);
        if (!url.isEmpty()) {
            QPushButton *btn = new QPushButton(tr("查看"), m_aboutSettings);
            btn->setObjectName("primaryButton");
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedWidth(80);
            connect(btn, &QPushButton::clicked, [url]() {
                QDesktopServices::openUrl(QUrl(url));
            });
            row->addWidget(btn);
        }
    };

    addOpenSourceRow("HMCL", tr("Java 版启动器，参考账户管理、实例识别、Java 设置等。"),
        "GPL-3.0", "https://github.com/huanghongxun/HMCL");
    addOpenSourceRow("PCL2 / PCL-CE", tr("Java 版启动器，参考存档设置、Mod 更新判断等。"),
        "GPL-3.0", "https://github.com/Hex-Dragon/PCL2");
    addOpenSourceRow("MultiMC / PrismLauncher", tr("实例管理启动器，参考 Java 扫描、内存分配、整合包格式。"),
        "MIT / GPL-3.0", "https://github.com/PrismLauncher/PrismLauncher");

    // 联机服务
    QHBoxLayout *multiTitleRow = appendSettingRow(opensourceCard,
        tr("联机服务"), QString(), QString(), true);
    Q_UNUSED(multiTitleRow);

    addOpenSourceRow("Terracotta", tr("Java 版联机核心。"),
        "MIT", "https://github.com/burningtnt/Terracotta");
    addOpenSourceRow("GravityCone", tr("基岩版联机核心。"),
        "MIT", "https://github.com/Tianpao/GravityCone");
    addOpenSourceRow("Hongshi", tr("红石联机外壳参考。"),
        "MIT", "https://github.com/hongshionline/hongshi-shell");

    // 皮肤/3D
    QHBoxLayout *skinTitleRow = appendSettingRow(opensourceCard,
        tr("皮肤与 3D 渲染"), QString(), QString(), true);
    Q_UNUSED(skinTitleRow);

    addOpenSourceRow("Blockbench", tr("3D 建模工具，参考皮肤 UV 布局与模型数据。"),
        "GPL-3.0", "https://github.com/JannisX11/blockbench");
    addOpenSourceRow("skinview3d", tr("皮肤 3D 渲染库，参考动画与视角。"),
        "MIT", QString());

    // Mod 平台
    QHBoxLayout *modTitleRow = appendSettingRow(opensourceCard,
        tr("Mod 平台"), QString(), QString(), true);
    Q_UNUSED(modTitleRow);

    addOpenSourceRow("CurseForge / Modrinth", tr("Mod 搜索与下载 API。"),
        "API 服务", "https://modrinth.com/");
    addOpenSourceRow("Litematica", tr("投影文件格式参考。"),
        "MIT", QString());

    // AI / 工具
    QHBoxLayout *aiTitleRow = appendSettingRow(opensourceCard,
        tr("AI 与工具"), QString(), QString(), true);
    Q_UNUSED(aiTitleRow);

    addOpenSourceRow("Ollama", tr("本地大模型推理后端。"),
        "MIT", "https://github.com/ollama/ollama");
    addOpenSourceRow("Chunker", tr("Java 版与基岩版存档转换引擎。"),
        "MIT", "https://github.com/HiveGamesOSS/Chunker");

    layout->addStretch();
}
