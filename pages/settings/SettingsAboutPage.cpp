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
#include "utils/RuntimeDependencyManager.h"
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

    // ── 特别鸣谢卡片 ──
    QVBoxLayout *thanksCard = createSettingsCard(layout, tr("特别鸣谢"));

    appendSettingRow(thanksCard, QString(),
        tr("以下朋友在测试阶段帮助发现了大量问题，为 BlockBox 的完善提供了宝贵帮助，特此鸣谢。"));

    QHBoxLayout *thanksRow = appendSettingRow(thanksCard,
        QStringLiteral("A花好月圆"),
        tr("测试阶段反馈了许多 Bug，帮助启动器变得更稳定、更好用。"));
    QPushButton *thanksBtn = new QPushButton(tr("抖音主页"), m_aboutSettings);
    thanksBtn->setObjectName("primaryButton");
    thanksBtn->setCursor(Qt::PointingHandCursor);
    thanksBtn->setFixedWidth(96);
    connect(thanksBtn, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://v.douyin.com/ugbz7UwTmtY/")));
    });
    thanksRow->addWidget(thanksBtn);

    // ── 运行依赖卡片（安卓 / 鸿蒙） ──
    if (RuntimeDependencyManager::isSupportedPlatform()) {
        RuntimeDependencyManager *deps = RuntimeDependencyManager::instance();
        QVBoxLayout *runtimeCard = createSettingsCard(layout, tr("运行依赖"));

        QHBoxLayout *checkDepsRow = appendSettingRow(runtimeCard,
            tr("检查依赖更新"),
            tr("获取运行 Minecraft 所需依赖的最新版本，检查启动器更新时也会自动检查。"));
        QPushButton *checkDepsBtn = new QPushButton(tr("检查更新"), m_aboutSettings);
        checkDepsBtn->setObjectName("checkUpdateBtn");
        checkDepsBtn->setCursor(Qt::PointingHandCursor);
        checkDepsBtn->setFixedWidth(96);
        checkDepsRow->addWidget(checkDepsBtn);

        QHBoxLayout *installRow = appendSettingRow(runtimeCard,
            tr("一键安装"),
            tr("自动下载并安装缺少的运行依赖，安装进度可在任务栏查看。"));
        QPushButton *installAllBtn = new QPushButton(tr("一键安装"), m_aboutSettings);
        installAllBtn->setObjectName("primaryButton");
        installAllBtn->setCursor(Qt::PointingHandCursor);
        installAllBtn->setFixedWidth(96);
        installAllBtn->setEnabled(false);
        installRow->addWidget(installAllBtn);

        QHBoxLayout *statusRow = appendSettingRow(runtimeCard, QString(), QString(), QString(), true);
        QLabel *statusLabel = new QLabel(tr("尚未检查，点击上方按钮获取最新版本。"), m_aboutSettings);
        statusLabel->setObjectName("settingValueLabel");
        statusLabel->setWordWrap(true);
        statusRow->addWidget(statusLabel);

        auto refreshRuntimeStatus = [statusLabel, installAllBtn]() {
            RuntimeDependencyManager *manager = RuntimeDependencyManager::instance();
            const QList<RuntimeDependency> items = manager->dependencies();
            if (items.isEmpty()) {
                statusLabel->setText(tr("尚未检查，点击上方按钮获取最新版本。"));
                installAllBtn->setEnabled(false);
                return;
            }
            QStringList lines;
            int pendingCount = 0;
            for (const RuntimeDependency &dependency : items) {
                QString state;
                if (!dependency.installed)
                    state = tr("未安装");
                else if (dependency.needsUpdate())
                    state = tr("已安装 %1，可更新").arg(dependency.installedVersion);
                else if (dependency.installedVersion.isEmpty())
                    state = tr("已安装");
                else
                    state = tr("已安装 %1").arg(dependency.installedVersion);
                if (dependency.isPending())
                    ++pendingCount;
                lines << QStringLiteral("%1 · %2 · %3")
                             .arg(dependency.name, dependency.version, state);
            }
            statusLabel->setText(lines.join(QLatin1Char('\n')));
            installAllBtn->setEnabled(pendingCount > 0 && !manager->isInstalling());
        };

        connect(checkDepsBtn, &QPushButton::clicked, this, [checkDepsBtn]() {
            checkDepsBtn->setEnabled(false);
            checkDepsBtn->setText(tr("检查中…"));
            RuntimeDependencyManager::instance()->checkForUpdates(checkDepsBtn->window(), false);
        });
        connect(deps, &RuntimeDependencyManager::checkFinished, this,
                [checkDepsBtn](bool, const QString &) {
                    checkDepsBtn->setEnabled(true);
                    checkDepsBtn->setText(tr("检查更新"));
                });
        connect(deps, &RuntimeDependencyManager::dependenciesChanged, this, refreshRuntimeStatus);
        connect(installAllBtn, &QPushButton::clicked, this, []() {
            RuntimeDependencyManager::instance()->installAll();
        });
        connect(deps, &RuntimeDependencyManager::installProgress, this,
                [statusLabel, installAllBtn](const QString &id, int percent, const QString &stage) {
                    installAllBtn->setEnabled(false);
                    const RuntimeDependency dependency =
                        RuntimeDependencyManager::instance()->dependencyById(id);
                    const QString name = dependency.name.isEmpty() ? id : dependency.name;
                    statusLabel->setText(tr("正在安装 %1：%2%（%3）")
                                             .arg(name, QString::number(percent), stage));
                });
        connect(deps, &RuntimeDependencyManager::installFinished, this,
                [refreshRuntimeStatus](const QString &, bool, const QString &) {
                    refreshRuntimeStatus();
                });

        refreshRuntimeStatus();
    }

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

    // 意见反馈：跳到软件内反馈页（登录论坛账号后提交，后台统一处理）
    QHBoxLayout *feedbackRow = appendSettingRow(linkCard,
        tr("意见反馈"),
        tr("提交软件问题与建议，处理进度会在反馈页里显示。"));
    QPushButton *feedbackBtn = new QPushButton(tr("去反馈"), m_aboutSettings);
    feedbackBtn->setObjectName("primaryButton");
    feedbackBtn->setCursor(Qt::PointingHandCursor);
    feedbackBtn->setFixedWidth(80);
    connect(feedbackBtn, &QPushButton::clicked, this, &SettingsPage::openFeedbackRequested);
    feedbackRow->addWidget(feedbackBtn);

    // ── 开源许可卡片 ──
    QVBoxLayout *licenseCard = createSettingsCard(layout, tr("开源许可"));

    appendSettingRow(licenseCard,
        tr("开源协议"), tr("BlockBox 基于开源协议发布，并使用了以下第三方开源库，特此致谢。"));

    // 本项目本体：GPL-3.0
    QHBoxLayout *selfRow = appendSettingRow(licenseCard,
        "BlockBox",
        tr("启动器本体以 GNU 通用公共许可证第 3 版（GPL-3.0）发布，第三方库与素材仍遵循各自协议。"));
    QLabel *selfValue = new QLabel("GPL-3.0", m_aboutSettings);
    selfValue->setObjectName("settingValueLabel");
    selfRow->addWidget(selfValue);
    QPushButton *licenseBtn = new QPushButton(tr("查看"), m_aboutSettings);
    licenseBtn->setObjectName("primaryButton");
    licenseBtn->setCursor(Qt::PointingHandCursor);
    licenseBtn->setFixedWidth(80);
    connect(licenseBtn, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/Juanyun0601/BlockBox/blob/main/LICENSE")));
    });
    selfRow->addWidget(licenseBtn);

    // GPL-3.0 要求的法律声明：版权、无担保、许可获取方式
    appendSettingRow(licenseCard, QString(),
        tr("Copyright (C) 2026 Juanyun0601。本程序不提供任何担保，可自由复制、修改与再发布，"
           "但须同样以 GPL-3.0 公开源代码。"));

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
    addOpenSourceRow("Verse 启动器（VersePc2）", tr("新一代启动器，旋转全景背景的 3D 天空盒渲染与内置全景场景素材取自其开源壁纸引擎。"),
        "GPL-3.0", "https://github.com/doujie081231/VersePc2");
    addOpenSourceRow("Original Panorama", tr("MC 原版主菜单全景高清重制包，「主菜单·经典/现代」场景素材来源；全景图版权归 Mojang Studios 所有。"),
        tr("未声明协议"), "https://github.com/Nixinova/Original-Panorama");
    addOpenSourceRow("Poly Haven", tr("CC0 实景全景素材库，「高山草甸」等实景场景取自于此。"),
        "CC0", "https://polyhaven.com/");
    addOpenSourceRow("MultiMC", tr("实例管理启动器，其下载层（多任务并发、多源回退、哈希校验、ETag 缓存、QSaveFile 原子落盘）与进度条系统（任务进度聚合、进度对话框）已整体移植进 BlockBox（Apache-2.0）。"),
        "Apache-2.0", "https://github.com/MultiMC/Launcher");
    addOpenSourceRow("PrismLauncher", tr("MultiMC 社区分支，参考 Java 扫描、内存分配、整合包格式。"),
        "GPL-3.0", "https://github.com/PrismLauncher/PrismLauncher");
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
    addOpenSourceRow("SchematicWebViewer", tr("WorldEdit 官方投影预览器，封面 3D 渲染的遮挡剔除、资源包/客户端材质加载思路参考。"),
        "MIT", "https://github.com/EngineHub/SchematicWebViewer");
    addOpenSourceRow("litematic-viewer", tr("浏览器端 Litematica 预览器（基于 Deepslate），等距封面与投影解析思路参考。"),
        "源码可见", "https://github.com/EndingCredits/litematic-viewer");
    addOpenSourceRow("Deepslate", tr("Minecraft 结构/Web 渲染库，封面体素预览思路来源之一。"),
        "MIT", "https://github.com/misode/deepslate");
    addOpenSourceRow("原版结构模板（Java 版 1.21.4 客户端）", tr("世界地图结构详情三维预览的核心数据：冰屋、沉船、废弃传送门、水下遗迹、掠夺者哨塔、古迹废墟、末地船、下界化石、平原村庄、远古城市、末地城、试炼密室、堡垒遗迹、林地府邸共 14 类结构，直接按游戏自带的结构模板逐块还原，方块种类、数量与朝向和游戏内一致（准确度 95% 以上）。其中村庄、远古城市、末地城、堡垒遗迹和试炼密室还会按游戏自带的拼接规则（模板池）拼出成片建筑群或整座地下迷宫，林地府邸用游戏自带的 73 个房间模板铺成楼层；村庄按所在群系自动选用平原、沙漠、热带草原、针叶林、雪原五种风格，并复刻原版拼图算法（随机种子、段的选择、朝向与位置）逐段还原，与游戏内实际生成的村庄逐方块对照一致（苔藓、路面风化这类游戏随机装饰除外）。结构模板版权归 Mojang Studios 所有。"),
        tr("Mojang 最终用户许可协议"), "https://www.minecraft.net/eula");
    addOpenSourceRow("Cubiomes", tr("世界地图的结构定位、种子计算与群系判定算法参考。"),
        "MIT", "https://github.com/Cubitect/cubiomes");
    addOpenSourceRow("Minecraft Wiki · 结构蓝本", tr("没有结构模板的结构（海底神殿等）仍按 Wiki 结构页面做示意重建，预览页会写明「程序化近似」；要塞、下界要塞和废弃矿井则按游戏自身的生成与拼装规则逐段还原（对照 1.21.4 客户端），与游戏内实际生成的结构逐块一致。"),
        "CC BY-NC-SA 3.0", "https://zh.minecraft.wiki/w/结构");

    // 投影方块编辑器
    appendSettingRow(opensourceCard, tr("投影方块编辑器"), QString(), QString(), true);

    addOpenSourceRow("Luanti（原 Minetest）", tr("开源体素沙盒。投影方块编辑器的第一人称自由视角参考了它的相机与移动实现：鼠标原地转动视角、俯仰角限制 ±90°、W/S 沿视线飞行、A/D 水平横移、空格 / Shift 升降、双击 W 或按住 Ctrl 疾跑，操作逻辑与《我的世界》原版一致。"),
        "LGPL-2.1", "https://github.com/minetest/minetest");

    // 地图与存档
    appendSettingRow(opensourceCard, tr("地图与存档"), QString(), QString(), true);

    addOpenSourceRow("MCA Selector", tr("Anvil 区域文件解析、地表俯视着色与多线程渐进渲染思路参考（存档地图）。"),
        "GPL-3.0", "https://github.com/Querz/mcaselector");
    addOpenSourceRow("Misode · mcmeta", tr("原版世界生成数据。世界地图的未加载区块种子预测据此还原蘑菇岛巨型蘑菇：每个区块长蘑菇的位置、红伞还是棕伞、伞柄多高，都按游戏标记区块装饰阶段的同一套随机算法推算，游戏内逐区块核对，位置、颜色与高度一致（岛屿上的巨型蘑菇全部命中，整体准确度约 93%，1.18 至 1.21.4 各版本均已核对生效）。"),
        "MIT", "https://github.com/misode/mcmeta");
    addOpenSourceRow("FeatureUtils（KaptainWutax）", tr("世界地图「战利品」页通过种子推测箱子内容的算法参考：先按世界种子定位结构，再按游戏自带的战利品表（池、轮数、权重、附魔与药水等物品函数、嵌套表）逐项推算每个箱子里各物品的出现概率。出现概率不低于 95% 的物品标为「必出」，其余按概率从高到低排列；宝箱数据取自官方 1.21.4 客户端（56 张宝箱表 + 3 张考古表），与游戏内战利品规则一致。"),
        "MIT", "https://github.com/KaptainWutax/FeatureUtils");

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

    addOpenSourceRow("opencode", tr("开源 AI 编程助手，AI 助手工作模式的过程展示（思考行、工具调用行的状态图标与展开样式）参考其设计。"),
        "MIT", "https://github.com/sst/opencode");
    addOpenSourceRow("Ollama", tr("本地大模型推理后端。"),
        "MIT", "https://github.com/ollama/ollama");
    addOpenSourceRow("Chunker", tr("Java 版与基岩版存档转换引擎。"),
        "MIT", "https://github.com/HiveGamesOSS/Chunker");

    layout->addStretch();
}
