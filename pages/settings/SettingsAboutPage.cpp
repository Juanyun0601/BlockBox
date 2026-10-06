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
#include "utils/AppVersion.h"
#include "utils/ThemeManager.h"
#include "utils/UpdateChecker.h"

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

    // 版本与检查更新
    QHBoxLayout *updateRow = appendSettingRow(infoCard,
        tr("检查更新"),
        tr("当前版本 %1，点击右侧按钮立即检查是否有新版本。").arg(AppVersion::current()),
        QString(), true);
    QPushButton *checkUpdateBtn = new QPushButton(tr("检查更新"), m_aboutSettings);
    checkUpdateBtn->setObjectName("checkUpdateBtn");
    checkUpdateBtn->setCursor(Qt::PointingHandCursor);
    checkUpdateBtn->setFixedWidth(96);
    connect(checkUpdateBtn, &QPushButton::clicked, this, [this, checkUpdateBtn]() {
        UpdateChecker *checker = UpdateChecker::instance();
        if (checker->isChecking())
            return;
        checkUpdateBtn->setEnabled(false);
        checkUpdateBtn->setText(tr("检查中…"));
        checker->checkForUpdates(this, false);
    });
    connect(UpdateChecker::instance(), &UpdateChecker::checkFinished, this,
            [checkUpdateBtn](bool, const QString &) {
        checkUpdateBtn->setEnabled(true);
        checkUpdateBtn->setText(tr("检查更新"));
    });
    updateRow->addWidget(checkUpdateBtn);

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

    addLinkRow(tr("官方网站"), tr("访问 BlockBox 官方网站。"), "https://blockbox.cc.cd");
    addLinkRow(tr("GitHub"), tr("查看源代码、提交问题与建议。"), "https://github.com/Juanyun0601/BlockBox");

    // ── 开源许可卡片 ──
    QVBoxLayout *licenseCard = createSettingsCard(layout, tr("开源许可"));

    appendSettingRow(licenseCard,
        tr("开源协议"), tr("BlockBox 基于开源协议发布，并使用了以下第三方开源库，特此致谢。"));

    QHBoxLayout *qtRow = appendSettingRow(licenseCard,
        "Qt", tr("跨平台 C++ 图形用户界面应用程序框架。"));
    QLabel *qtValue = new QLabel("LGPL / GPL", m_aboutSettings);
    qtValue->setObjectName("settingValueLabel");
    qtRow->addWidget(qtValue);

    QHBoxLayout *zlibRow = appendSettingRow(licenseCard,
        "zlib", tr("数据压缩库，用于整合包与 ZIP 压缩包的解压。"));
    QLabel *zlibValue = new QLabel("zlib", m_aboutSettings);
    zlibValue->setObjectName("settingValueLabel");
    zlibRow->addWidget(zlibValue);

    QHBoxLayout *webpRow = appendSettingRow(licenseCard,
        "libwebp", tr("WebP 图像解码库，用于 WebP 图片加载（运行时动态加载）。"), QString(), true);
    QLabel *webpValue = new QLabel("BSD-3-Clause", m_aboutSettings);
    webpValue->setObjectName("settingValueLabel");
    webpRow->addWidget(webpValue);

    // ── 参考开源项目卡片 ──
    QVBoxLayout *opensourceCard = createSettingsCard(layout, tr("参考开源项目"));

    appendSettingRow(opensourceCard, QString(),
        tr("BlockBox 的设计与实现参考了以下优秀的开源项目，特此致谢。"));

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

    // 启动器
    appendSettingRow(opensourceCard, tr("启动器"), QString(), QString(), true);

    addOpenSourceRow("HMCL", tr("Java 版启动器，参考账户管理、实例识别、Java 设置等。"),
        "GPL-3.0", "https://github.com/HMCL-dev/HMCL");
    addOpenSourceRow("PCL-CE", tr("PCL 社区开源版，参考存档设置、Mod 更新判断等。"),
        "Apache-2.0", "https://github.com/PCL-Community/PCL-CE");
    addOpenSourceRow("MultiMC / PrismLauncher", tr("实例管理启动器，参考 Java 扫描、内存分配、整合包格式。"),
        "Apache-2.0 / GPL-3.0", "https://github.com/PrismLauncher/PrismLauncher");
    addOpenSourceRow("ProjBobcat", tr(".NET 启动器核心库，参考账户登录与资源下载流程设计。"),
        "MIT", "https://github.com/Corona-Studio/ProjBobcat");

    // 联机服务
    appendSettingRow(opensourceCard, tr("联机服务"), QString(), QString(), true);

    addOpenSourceRow("Terracotta（陶瓦联机）", tr("Java 版联机核心，集成其建房 / 加房 / 邀请码流程。"),
        "AGPL-3.0", "https://github.com/burningtnt/Terracotta");
    addOpenSourceRow("EasyTier", tr("去中心化组网隧道，联机隧道引擎基于其免驱动 --no-tun 模式。"),
        "LGPL-3.0", "https://github.com/EasyTier/EasyTier");
    addOpenSourceRow("GravityCone", tr("基岩版联机核心。"),
        tr("未声明协议"), "https://github.com/Tianpao/GravityCone");
    addOpenSourceRow("Hongshi（红石联机）", tr("红石联机外壳参考。"),
        tr("未声明协议"), "https://github.com/hongshionline/hongshi-webui");

    // 皮肤/3D
    appendSettingRow(opensourceCard, tr("皮肤与 3D 渲染"), QString(), QString(), true);

    addOpenSourceRow("Blockbench", tr("3D 建模工具，参考皮肤 UV 布局与模型数据。"),
        "GPL-3.0", "https://github.com/JannisX11/blockbench");
    addOpenSourceRow("skinview3d", tr("皮肤 3D 渲染库，参考动画与视角。"),
        "MIT", "https://github.com/bs-community/skinview3d");

    // Mod 平台
    appendSettingRow(opensourceCard, tr("Mod 平台"), QString(), QString(), true);

    addOpenSourceRow("CurseForge / Modrinth", tr("Mod 搜索与下载 API。"),
        tr("API 服务"), "https://modrinth.com/");
    addOpenSourceRow("BMCLAPI", tr("国内镜像加速源，用于 Forge / Fabric / OptiFine 等资源下载。"),
        "Beerware", "https://github.com/bangbang93/BMCL");
    addOpenSourceRow("Cleanroom", tr("1.7.10 版 Forge 延续分支，支持其安装与版本获取。"),
        tr("源码可见许可"), "https://github.com/CleanroomMC/Cleanroom");
    addOpenSourceRow("Litematica", tr("投影文件格式参考（.litematic 读取）。"),
        "LGPL-3.0", "https://github.com/maruohon/litematica");

    // AI / 工具
    appendSettingRow(opensourceCard, tr("AI 与工具"), QString(), QString(), true);

    addOpenSourceRow("Ollama", tr("本地大模型推理后端。"),
        "MIT", "https://github.com/ollama/ollama");
    addOpenSourceRow("Chunker", tr("Java 版与基岩版存档转换引擎。"),
        "MIT", "https://github.com/HiveGamesOSS/Chunker");

    layout->addStretch();
}
