/**
 * @file   SettingsJavaPage.cpp
 * @brief  Java 设置页实现 — 参考 HMCL/PCL-CE 的工具栏 + 列表布局
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include "components/AppMessageBox.h"
#include "components/OutlinedLabel.h"
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include "components/CustomCheckBox.h"
#include "utils/GameLauncher.h"
#include "utils/JavaScanWorker.h"
#include "utils/SettingsManager.h"

// ────────────────────────────────────────────────────────────
// 工具：从版本号提取主版本号（如 "1.8.0_301" → 8, "17.0.3" → 17）
// ────────────────────────────────────────────────────────────
static int javaMajorVersion(const QString& version)
{
    static const QRegularExpression re("(?:1\\.)?([0-9]+)");
    QRegularExpressionMatch m = re.match(version);
    if (m.hasMatch()) {
        return m.captured(1).toInt();
    }
    return 0;
}

// ────────────────────────────────────────────────────────────
// Java 管理页初始化
// ────────────────────────────────────────────────────────────

void SettingsPage::initJavaManagerSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_javaManagerSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题 + 副标题 ──
    OutlinedLabel *titleLabel = new OutlinedLabel(tr("Java管理"), m_javaManagerSettings);
    titleLabel->setObjectName("sectionTitle");

    QLabel *subtitleLabel = new QLabel(
        tr("管理本机已安装的 Java 运行时，扫描结果会自动缓存供启动游戏时使用。"),
        m_javaManagerSettings);
    subtitleLabel->setObjectName("sectionSubtitle");
    subtitleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    layout->addWidget(subtitleLabel);

    // ── 工具栏 ──
    QWidget *toolbar = new QWidget(m_javaManagerSettings);
    toolbar->setObjectName("javaToolbar");
    QHBoxLayout *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(8);

    QPushButton *scanBtn = new QPushButton(tr("全盘扫描"), toolbar);
    scanBtn->setObjectName("javaScanBtn");
    scanBtn->setCursor(Qt::PointingHandCursor);
    scanBtn->setToolTip(tr("扫描所有磁盘以查找已安装的 Java"));
    connect(scanBtn, &QPushButton::clicked, this, &SettingsPage::onScanJavaClicked);

    QPushButton *addBtn = new QPushButton(tr("手动添加"), toolbar);
    addBtn->setObjectName("javaAddBtn");
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setToolTip(tr("通过浏览选择 java.exe 手动添加"));
    connect(addBtn, &QPushButton::clicked, this, &SettingsPage::onAddJavaClicked);

    QPushButton *detectBtn = new QPushButton(tr("快速检测"), toolbar);
    detectBtn->setObjectName("javaDetectBtn");
    detectBtn->setCursor(Qt::PointingHandCursor);
    detectBtn->setToolTip(tr("从 PATH 与常见目录快速检测 Java"));
    connect(detectBtn, &QPushButton::clicked, this, [this]() {
        GameLauncher::JavaInfo javaInfo = GameLauncher::instance()->detectJava();
        if (javaInfo.valid) {
            // 把检测到的 Java 加入缓存列表（去重）
            QList<QPair<QString, QString>> installations =
                SettingsManager::instance()->getJavaInstallations();
            bool exists = false;
            for (const auto& inst : installations) {
                if (inst.first == javaInfo.path) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                installations.append(qMakePair(javaInfo.path, javaInfo.version));
                SettingsManager::instance()->setJavaInstallations(installations);
            }
            refreshJavaList();
            refreshGlobalJavaCombo();
            AppMessageBox::information(this, tr("检测成功"),
                tr("已检测到 Java %1\n路径: %2").arg(javaInfo.version).arg(javaInfo.path));
        } else {
            AppMessageBox::warning(this, tr("警告"), tr("未检测到 Java，请尝试全盘扫描或手动添加。"));
        }
    });

    QPushButton *downloadBtn = new QPushButton(tr("下载Java"), toolbar);
    downloadBtn->setObjectName("downloadJavaBtn");
    downloadBtn->setCursor(Qt::PointingHandCursor);
    connect(downloadBtn, &QPushButton::clicked, this, &SettingsPage::downloadJavaRequested);

    toolbarLayout->addWidget(scanBtn);
    toolbarLayout->addWidget(addBtn);
    toolbarLayout->addWidget(detectBtn);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(downloadBtn);

    // ── 已安装 Java 列表 ──
    QLabel *listTitleLabel = new QLabel(tr("已安装的 Java"), m_javaManagerSettings);
    listTitleLabel->setObjectName("javaListTitle");

    m_javaListWidget = new QListWidget(m_javaManagerSettings);
    m_javaListWidget->setObjectName("javaListWidget");
    m_javaListWidget->setFrameShape(QFrame::NoFrame);
    m_javaListWidget->setSelectionMode(QAbstractItemView::NoSelection);
    m_javaListWidget->setFocusPolicy(Qt::NoFocus);
    m_javaListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_javaListWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_javaListWidget->setMinimumHeight(180);

    // 空状态提示
    m_javaEmptyLabel = new QLabel(
        tr("尚未发现任何 Java 安装。\n点击上方「全盘扫描」或「手动添加」以检测 Java。"),
        m_javaListWidget);
    m_javaEmptyLabel->setObjectName("javaEmptyLabel");
    m_javaEmptyLabel->setAlignment(Qt::AlignCenter);
    m_javaEmptyLabel->setWordWrap(true);

    // ── 全局默认 Java 选择 ──
    QVBoxLayout *runtimeCard = createSettingsCard(layout, tr("全局默认 Java"));

    m_globalJavaCombo = new QComboBox();
    m_globalJavaCombo->setObjectName("globalJavaCombo");
    disableWheelEffect(m_globalJavaCombo);
    m_globalJavaCombo->setToolTip(tr("选择所有实例默认使用的 Java。可在实例设置中单独覆盖。"));
    connect(m_globalJavaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onGlobalJavaChanged);

    QHBoxLayout *globalRow = appendSettingRow(runtimeCard,
        tr("全局默认Java版本"), tr("所有实例默认使用的 Java，可在实例设置中单独覆盖。"),
        tr("设置所有实例默认使用的 Java 版本。如果某个实例有特殊需求，"
           "可以在该实例的单独设置中覆盖此选项。"));
    globalRow->addWidget(m_globalJavaCombo, 1);

    m_autoSelectJavaCheck = new CustomCheckBox();
    m_autoSelectJavaCheck->setChecked(true);
    m_autoSelectJavaCheck->setToolTip(
        tr("启用后，启动游戏时会根据游戏版本自动匹配合适的 Java（如 MC 1.17+ 用 Java 17）。"));
    connect(m_autoSelectJavaCheck, &CustomCheckBox::toggled, this, [this](bool checked) {
        m_globalJavaCombo->setEnabled(!checked);
        SettingsManager::instance()->setAutoSelectJava(checked);
    });

    QHBoxLayout *autoSelectRow = appendSettingRow(runtimeCard,
        tr("按游戏版本自动选择 Java"), tr("根据游戏版本自动匹配合适的 Java 运行时。"), QString(), true);
    autoSelectRow->addWidget(m_autoSelectJavaCheck);

    // ── 恢复默认值 ──
    QPushButton *restoreDefaultsBtn = new QPushButton(tr("恢复默认值"), m_javaManagerSettings);
    restoreDefaultsBtn->setObjectName("restoreDefaultsBtn");
    connect(restoreDefaultsBtn, &QPushButton::clicked, this, &SettingsPage::onRestoreDefaults);

    // ── 组装 ──
    layout->addWidget(toolbar);
    layout->addWidget(listTitleLabel);
    layout->addWidget(m_javaListWidget, 1);
    layout->addStretch();
    layout->addWidget(restoreDefaultsBtn, 0, Qt::AlignRight);

    // 初始填充
    refreshJavaList();
    refreshGlobalJavaCombo();

    // 同步自动选择状态
    bool autoSelect = SettingsManager::instance()->getAutoSelectJava();
    m_autoSelectJavaCheck->setChecked(autoSelect);
    m_globalJavaCombo->setEnabled(!autoSelect);
}

// ────────────────────────────────────────────────────────────
// 创建单个 Java 列表项 Widget（参考 HMCL JavaItemCell + PCL MyListItem）
// ────────────────────────────────────────────────────────────

QWidget* SettingsPage::createJavaItemWidget(const QString& path, const QString& version)
{
    QFrame *itemFrame = new QFrame();
    itemFrame->setObjectName("javaItemFrame");
    itemFrame->setFrameShape(QFrame::NoFrame);

    QHBoxLayout *itemLayout = new QHBoxLayout(itemFrame);
    itemLayout->setContentsMargins(14, 10, 14, 10);
    itemLayout->setSpacing(12);

    // 左侧：标题 + 路径 + 版本标签
    QWidget *infoWidget = new QWidget(itemFrame);
    QVBoxLayout *infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    // 标题：JDK/JRE + 主版本号
    int major = javaMajorVersion(version);
    QString typeStr = QFileInfo(QFileInfo(path).path() + "/javac.exe").exists()
                      ? QStringLiteral("JDK") : QStringLiteral("JRE");
    QString titleStr = (major > 0)
                       ? QStringLiteral("%1 %2").arg(typeStr).arg(major)
                       : QStringLiteral("%1 %2").arg(typeStr).arg(version);
    QLabel *titleLabel = new QLabel(titleStr, infoWidget);
    titleLabel->setObjectName("javaItemTitle");

    // 路径
    QLabel *pathLabel = new QLabel(path, infoWidget);
    pathLabel->setObjectName("javaItemPath");
    pathLabel->setWordWrap(false);
    pathLabel->setToolTip(path);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // 版本完整号 + 位数标签
    QLabel *versionLabel = new QLabel(
        QStringLiteral("%1 · %2").arg(version).arg(tr("本机")), infoWidget);
    versionLabel->setObjectName("javaItemVersion");

    infoLayout->addWidget(titleLabel);
    infoLayout->addWidget(pathLabel);
    infoLayout->addWidget(versionLabel);

    // 右侧：操作按钮
    QWidget *actionsWidget = new QWidget(itemFrame);
    QVBoxLayout *actionsLayout = new QVBoxLayout(actionsWidget);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(6);

    QPushButton *openFolderBtn = new QPushButton(tr("打开文件夹"), actionsWidget);
    openFolderBtn->setObjectName("javaOpenFolderBtn");
    openFolderBtn->setCursor(Qt::PointingHandCursor);
    openFolderBtn->setToolTip(tr("在文件资源管理器中打开 Java 所在目录"));
    connect(openFolderBtn, &QPushButton::clicked, this, [this, path]() {
        onOpenJavaFolderClicked(path);
    });

    QPushButton *removeBtn = new QPushButton(tr("移除"), actionsWidget);
    removeBtn->setObjectName("javaRemoveBtn");
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setToolTip(tr("从列表中移除该 Java（不会删除文件）"));
    connect(removeBtn, &QPushButton::clicked, this, [this, path]() {
        onRemoveJavaClicked(path);
    });

    actionsLayout->addWidget(openFolderBtn);
    actionsLayout->addWidget(removeBtn);
    actionsLayout->addStretch();

    itemLayout->addWidget(infoWidget, 1);
    itemLayout->addWidget(actionsWidget, 0, Qt::AlignTop);

    return itemFrame;
}

// ────────────────────────────────────────────────────────────
// 刷新 Java 列表（从缓存读取）
// ────────────────────────────────────────────────────────────

void SettingsPage::refreshJavaList()
{
    if (!m_javaListWidget) return;

    m_javaListWidget->clear();

    QList<QPair<QString, QString>> installations =
        SettingsManager::instance()->getJavaInstallations();

    if (installations.isEmpty()) {
        // 显示空状态提示
        QListWidgetItem *emptyItem = new QListWidgetItem(m_javaListWidget);
        emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        m_javaListWidget->addItem(emptyItem);
        m_javaListWidget->setItemWidget(emptyItem, m_javaEmptyLabel);
        emptyItem->setSizeHint(m_javaEmptyLabel->sizeHint().expandedTo(QSize(0, 80)));
        return;
    }

    for (const auto& inst : installations) {
        QListWidgetItem *item = new QListWidgetItem(m_javaListWidget);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        QWidget *w = createJavaItemWidget(inst.first, inst.second);
        m_javaListWidget->addItem(item);
        m_javaListWidget->setItemWidget(item, w);
        item->setSizeHint(w->sizeHint().expandedTo(QSize(0, 88)));
    }
}

// ────────────────────────────────────────────────────────────
// 刷新全局 Java 下拉框
// ────────────────────────────────────────────────────────────

void SettingsPage::refreshGlobalJavaCombo()
{
    if (!m_globalJavaCombo) return;

    // 阻塞信号，避免刷新时触发 onGlobalJavaChanged
    m_globalJavaCombo->blockSignals(true);
    m_globalJavaCombo->clear();

    m_globalJavaCombo->addItem(tr("自动选择（按游戏版本）"), QStringLiteral("auto"));

    QList<QPair<QString, QString>> installations =
        SettingsManager::instance()->getJavaInstallations();
    for (const auto& inst : installations) {
        int major = javaMajorVersion(inst.second);
        QString label = (major > 0)
                        ? QStringLiteral("Java %1 (%2)").arg(major).arg(inst.second)
                        : QStringLiteral("Java %1").arg(inst.second);
        m_globalJavaCombo->addItem(label, inst.first);
    }

    // 还原当前选择
    QString currentPath = SettingsManager::instance()->getJavaPath();
    int idx = m_globalJavaCombo->findData(currentPath);
    if (idx >= 0) {
        m_globalJavaCombo->setCurrentIndex(idx);
    } else {
        m_globalJavaCombo->setCurrentIndex(0);
    }

    m_globalJavaCombo->blockSignals(false);

    // 自动选择模式下禁用下拉
    bool autoSelect = SettingsManager::instance()->getAutoSelectJava();
    m_globalJavaCombo->setEnabled(!autoSelect);
}

// ────────────────────────────────────────────────────────────
// 全盘扫描 Java（静默扫描，无弹窗，由新手引导/后台线程承担）
// ────────────────────────────────────────────────────────────

void SettingsPage::onScanJavaClicked()
{
    JavaScanWorker *worker = new JavaScanWorker();
    QThread *scanThread = new QThread();
    worker->moveToThread(scanThread);

    connect(scanThread, &QThread::started, worker, &JavaScanWorker::doScan);
    connect(worker, &JavaScanWorker::scanFinished, scanThread, &QThread::quit);
    connect(scanThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(scanThread, &QThread::finished, scanThread, &QObject::deleteLater);

    QList<QPair<QString, QString>> installations;

    QEventLoop loop;
    connect(worker, &JavaScanWorker::scanFinished, &loop,
            [&](const QList<QPair<QString, QString>>& results) {
                installations = results;
                loop.quit();
            });

    scanThread->start();
    loop.exec();

    // 合并去重后保存到缓存
    QList<QPair<QString, QString>> existing =
        SettingsManager::instance()->getJavaInstallations();
    QSet<QString> seenPaths;
    for (const auto& e : existing) {
        seenPaths.insert(e.first);
    }

    int newCount = 0;
    for (const auto& inst : installations) {
        if (!seenPaths.contains(inst.first)) {
            existing.append(inst);
            seenPaths.insert(inst.first);
            newCount++;
        }
    }

    SettingsManager::instance()->setJavaInstallations(existing);
    refreshJavaList();
    refreshGlobalJavaCombo();

    if (newCount > 0) {
        AppMessageBox::information(this, tr("扫描完成"),
            tr("共发现 %1 个 Java，新增 %2 个已加入列表。").arg(installations.size()).arg(newCount));
    } else if (!installations.isEmpty()) {
        AppMessageBox::information(this, tr("扫描完成"),
            tr("共发现 %1 个 Java（均已在列表中）。").arg(installations.size()));
    } else {
        AppMessageBox::information(this, tr("扫描完成"),
            tr("未发现任何 Java 安装，请点击「下载Java」获取。"));
    }
}

// ────────────────────────────────────────────────────────────
// 手动添加 Java（文件对话框）
// ────────────────────────────────────────────────────────────

void SettingsPage::onAddJavaClicked()
{
    QString filePath = AppFileDialog::getOpenFileName(
        this, tr("选择 Java 可执行文件"),
        QStringLiteral("C:/Program Files/Java"),
        tr("Java 可执行文件 (java.exe javaw.exe);;所有文件 (*.*)"));
    if (filePath.isEmpty()) return;

    filePath = QDir::toNativeSeparators(filePath);

    // 验证
    GameLauncher::JavaInfo info;
    if (!GameLauncher::instance()->validateJavaPath(filePath, info)) {
        AppMessageBox::warning(this, tr("添加失败"),
            tr("所选文件不是有效的 Java 可执行文件，或无法获取版本信息。"));
        return;
    }

    QList<QPair<QString, QString>> installations =
        SettingsManager::instance()->getJavaInstallations();
    for (const auto& inst : installations) {
        if (inst.first == filePath) {
            AppMessageBox::information(this, tr("已存在"),
                tr("该 Java 已在列表中:\n%1").arg(filePath));
            return;
        }
    }

    installations.append(qMakePair(filePath, info.version));
    SettingsManager::instance()->setJavaInstallations(installations);
    refreshJavaList();
    refreshGlobalJavaCombo();
    AppMessageBox::information(this, tr("添加成功"),
        tr("已添加 Java %1:\n%2").arg(info.version).arg(filePath));
}

// ────────────────────────────────────────────────────────────
// 移除 Java（仅从列表移除，不删除文件）
// ────────────────────────────────────────────────────────────

void SettingsPage::onRemoveJavaClicked(const QString& javaPath)
{
    auto reply = AppMessageBox::question(this, tr("确认移除"),
        tr("确定要从列表中移除该 Java 吗？\n（不会删除实际文件）\n\n%1").arg(javaPath),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes) return;

    QList<QPair<QString, QString>> installations =
        SettingsManager::instance()->getJavaInstallations();
    for (int i = 0; i < installations.size(); ++i) {
        if (installations[i].first == javaPath) {
            installations.removeAt(i);
            break;
        }
    }
    SettingsManager::instance()->setJavaInstallations(installations);

    // 若移除的正是当前全局 Java，回退到 auto
    if (SettingsManager::instance()->getJavaPath() == javaPath) {
        SettingsManager::instance()->setJavaPath(QStringLiteral("auto"));
    }

    refreshJavaList();
    refreshGlobalJavaCombo();
}

// ────────────────────────────────────────────────────────────
// 打开 Java 所在文件夹
// ────────────────────────────────────────────────────────────

void SettingsPage::onOpenJavaFolderClicked(const QString& javaPath)
{
    QFileInfo fi(javaPath);
    QString dir = fi.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

// ────────────────────────────────────────────────────────────
// 全局 Java 切换
// ────────────────────────────────────────────────────────────

void SettingsPage::onGlobalJavaChanged(int index)
{
    if (!m_globalJavaCombo || index < 0) return;
    QString path = m_globalJavaCombo->itemData(index).toString();
    SettingsManager::instance()->setJavaPath(path);
}
