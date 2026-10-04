/**
 * @file   InstanceJavaPage.cpp
 * @brief  实例助手 - Java 管理页面实现
 * @author BlockBox Team
 * @date   2026-07-21
 */
#include "InstanceJavaPage.h"

#include <QComboBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include "components/AppMessageBox.h"
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include "components/CustomCheckBox.h"
#include "utils/GameLauncher.h"
#include "utils/JavaScanWorker.h"
#include "utils/SettingsManager.h"

namespace {
/**
 * @brief 从 Java 版本字符串提取主版本号
 * @param version 完整版本字符串，如 "1.8.0_301" / "17.0.3"
 * @return 主版本号（如 8 / 17），无法识别返回 0
 */
int javaMajorVersion(const QString &version)
{
    static const QRegularExpression re(QStringLiteral("(?:1\\.)?([0-9]+)"));
    QRegularExpressionMatch m = re.match(version);
    if (m.hasMatch())
    {
        return m.captured(1).toInt();
    }
    return 0;
}
} // namespace

InstanceJavaPage::InstanceJavaPage(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(nullptr)
    , m_subtitleLabel(nullptr)
    , m_scanBtn(nullptr)
    , m_addBtn(nullptr)
    , m_detectBtn(nullptr)
    , m_downloadBtn(nullptr)
    , m_listTitleLabel(nullptr)
    , m_listWidget(nullptr)
    , m_emptyLabel(nullptr)
    , m_globalJavaCombo(nullptr)
    , m_autoSelectCheck(nullptr)
{
    initUI();
    refreshJavaList();
    refreshGlobalJavaCombo();

    // 同步自动选择状态
    bool autoSelect = SettingsManager::instance()->getAutoSelectJava();
    m_autoSelectCheck->setChecked(autoSelect);
    m_globalJavaCombo->setEnabled(!autoSelect);
}

void InstanceJavaPage::initUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    // ── 标题 + 副标题 ──
    m_titleLabel = new OutlinedLabel(tr("Java管理"), this);
    m_titleLabel->setObjectName("sectionTitle");

    m_subtitleLabel = new QLabel(
        tr("管理本机已安装的 Java 运行时，扫描结果会自动缓存供启动游戏时使用。"),
        this);
    m_subtitleLabel->setObjectName("sectionSubtitle");
    m_subtitleLabel->setWordWrap(true);

    // ── 工具栏 ──
    auto *toolbar = new QWidget(this);
    toolbar->setObjectName("javaToolbar");
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    m_scanBtn = new QPushButton(tr("全盘扫描"), toolbar);
    m_scanBtn->setObjectName("javaScanBtn");
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    m_scanBtn->setToolTip(tr("扫描所有磁盘以查找已安装的 Java"));
    connect(m_scanBtn, &QPushButton::clicked, this, &InstanceJavaPage::onScanClicked);

    m_addBtn = new QPushButton(tr("手动添加"), toolbar);
    m_addBtn->setObjectName("javaAddBtn");
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setToolTip(tr("通过浏览选择 java.exe 手动添加"));
    connect(m_addBtn, &QPushButton::clicked, this, &InstanceJavaPage::onAddClicked);

    m_detectBtn = new QPushButton(tr("快速检测"), toolbar);
    m_detectBtn->setObjectName("javaDetectBtn");
    m_detectBtn->setCursor(Qt::PointingHandCursor);
    m_detectBtn->setToolTip(tr("从 PATH 与常见目录快速检测 Java"));
    connect(m_detectBtn, &QPushButton::clicked, this, &InstanceJavaPage::onDetectClicked);

    m_downloadBtn = new QPushButton(tr("下载Java"), toolbar);
    m_downloadBtn->setObjectName("downloadJavaBtn");
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    connect(m_downloadBtn, &QPushButton::clicked, this, &InstanceJavaPage::onDownloadClicked);

    toolbarLayout->addWidget(m_scanBtn);
    toolbarLayout->addWidget(m_addBtn);
    toolbarLayout->addWidget(m_detectBtn);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_downloadBtn);

    // ── 列表标题 ──
    m_listTitleLabel = new QLabel(tr("已安装的 Java"), this);
    m_listTitleLabel->setObjectName("javaListTitle");

    // ── 已安装 Java 列表 ──
    m_listWidget = new QListWidget(this);
    m_listWidget->setObjectName("javaListWidget");
    m_listWidget->setFrameShape(QFrame::NoFrame);
    m_listWidget->setSelectionMode(QAbstractItemView::NoSelection);
    m_listWidget->setFocusPolicy(Qt::NoFocus);
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_listWidget->setMinimumHeight(160);

    // 空状态提示
    m_emptyLabel = new QLabel(
        tr("尚未发现任何 Java 安装。\n点击上方「全盘扫描」或「下载Java」以获取 Java。"),
        m_listWidget);
    m_emptyLabel->setObjectName("javaEmptyLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);

    // ── 全局 Java 选择 ──
    auto *runtimeGroup = new QFrame(this);
    runtimeGroup->setObjectName("javaRuntimeFrame");
    auto *runtimeLayout = new QVBoxLayout(runtimeGroup);
    runtimeLayout->setContentsMargins(12, 10, 12, 10);
    runtimeLayout->setSpacing(8);

    auto *globalLabel = new QLabel(tr("全局默认 Java："), runtimeGroup);
    globalLabel->setObjectName("javaGlobalLabel");

    m_globalJavaCombo = new QComboBox(runtimeGroup);
    m_globalJavaCombo->setObjectName("globalJavaCombo");
    m_globalJavaCombo->setToolTip(tr("选择所有实例默认使用的 Java。可在实例设置中单独覆盖。"));
    connect(m_globalJavaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceJavaPage::onGlobalJavaChanged);

    m_autoSelectCheck = new CustomCheckBox(tr("按游戏版本自动选择 Java"), runtimeGroup);
    m_autoSelectCheck->setChecked(true);
    m_autoSelectCheck->setToolTip(
        tr("启用后，启动游戏时会根据游戏版本自动匹配合适的 Java（如 MC 1.17+ 用 Java 17）。"));
    connect(m_autoSelectCheck, &CustomCheckBox::toggled, this, &InstanceJavaPage::onAutoSelectToggled);

    runtimeLayout->addWidget(globalLabel);
    runtimeLayout->addWidget(m_globalJavaCombo);
    runtimeLayout->addWidget(m_autoSelectCheck);

    // ── 组装 ──
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_subtitleLabel);
    layout->addWidget(toolbar);
    layout->addWidget(m_listTitleLabel);
    layout->addWidget(m_listWidget, 1);
    layout->addWidget(runtimeGroup);
}

void InstanceJavaPage::onScanClicked()
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
            [&](const QList<QPair<QString, QString>> &results) {
                installations = results;
                loop.quit();
            });

    scanThread->start();
    loop.exec();

    // 合并去重
    QList<QPair<QString, QString>> existing =
        SettingsManager::instance()->getJavaInstallations();
    QSet<QString> seenPaths;
    for (const auto &e : existing)
    {
        seenPaths.insert(e.first);
    }

    int newCount = 0;
    for (const auto &inst : installations)
    {
        if (!seenPaths.contains(inst.first))
        {
            existing.append(inst);
            seenPaths.insert(inst.first);
            newCount++;
        }
    }

    SettingsManager::instance()->setJavaInstallations(existing);
    refreshJavaList();
    refreshGlobalJavaCombo();

    if (newCount > 0)
    {
        AppMessageBox::information(this, tr("扫描完成"),
            tr("共发现 %1 个 Java，新增 %2 个已加入列表。").arg(installations.size()).arg(newCount));
    }
    else if (!installations.isEmpty())
    {
        AppMessageBox::information(this, tr("扫描完成"),
            tr("共发现 %1 个 Java（均已在列表中）。").arg(installations.size()));
    }
    else
    {
        AppMessageBox::information(this, tr("扫描完成"),
            tr("未发现任何 Java 安装，请点击「下载Java」获取。"));
    }
}

void InstanceJavaPage::onAddClicked()
{
    QString filePath = AppFileDialog::getOpenFileName(
        this, tr("选择 Java 可执行文件"),
        QStringLiteral("C:/Program Files/Java"),
        tr("Java 可执行文件 (java.exe javaw.exe);;所有文件 (*.*)"));
    if (filePath.isEmpty())
        return;

    filePath = QDir::toNativeSeparators(filePath);

    GameLauncher::JavaInfo info;
    if (!GameLauncher::instance()->validateJavaPath(filePath, info))
    {
        AppMessageBox::warning(this, tr("添加失败"),
            tr("所选文件不是有效的 Java 可执行文件，或无法获取版本信息。"));
        return;
    }

    QList<QPair<QString, QString>> installations =
        SettingsManager::instance()->getJavaInstallations();
    for (const auto &inst : installations)
    {
        if (inst.first == filePath)
        {
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

void InstanceJavaPage::onDetectClicked()
{
    GameLauncher::JavaInfo javaInfo = GameLauncher::instance()->detectJava();
    if (javaInfo.valid)
    {
        QList<QPair<QString, QString>> installations =
            SettingsManager::instance()->getJavaInstallations();
        bool exists = false;
        for (const auto &inst : installations)
        {
            if (inst.first == javaInfo.path)
            {
                exists = true;
                break;
            }
        }
        if (!exists)
        {
            installations.append(qMakePair(javaInfo.path, javaInfo.version));
            SettingsManager::instance()->setJavaInstallations(installations);
        }
        refreshJavaList();
        refreshGlobalJavaCombo();
        AppMessageBox::information(this, tr("检测成功"),
            tr("已检测到 Java %1\n路径: %2").arg(javaInfo.version).arg(javaInfo.path));
    }
    else
    {
        AppMessageBox::warning(this, tr("警告"), tr("未检测到 Java，请尝试全盘扫描或下载安装。"));
    }
}

void InstanceJavaPage::onDownloadClicked()
{
    emit downloadJavaRequested();
}

void InstanceJavaPage::onRemoveJava(const QString &javaPath)
{
    auto reply = AppMessageBox::question(this, tr("确认移除"),
        tr("确定要从列表中移除该 Java 吗？\n（不会删除实际文件）\n\n%1").arg(javaPath),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    QList<QPair<QString, QString>> installations =
        SettingsManager::instance()->getJavaInstallations();
    for (int i = 0; i < installations.size(); ++i)
    {
        if (installations[i].first == javaPath)
        {
            installations.removeAt(i);
            break;
        }
    }
    SettingsManager::instance()->setJavaInstallations(installations);

    // 若移除的正是当前全局 Java，回退到 auto
    if (SettingsManager::instance()->getJavaPath() == javaPath)
    {
        SettingsManager::instance()->setJavaPath(QStringLiteral("auto"));
    }

    refreshJavaList();
    refreshGlobalJavaCombo();
    emit javaSettingsChanged();
}

void InstanceJavaPage::onOpenJavaFolder(const QString &javaPath)
{
    QFileInfo fi(javaPath);
    QString dir = fi.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void InstanceJavaPage::onGlobalJavaChanged(int index)
{
    if (!m_globalJavaCombo || index < 0)
        return;
    QString path = m_globalJavaCombo->itemData(index).toString();
    SettingsManager::instance()->setJavaPath(path);
    emit javaSettingsChanged();
}

void InstanceJavaPage::onAutoSelectToggled(bool checked)
{
    m_globalJavaCombo->setEnabled(!checked);
    SettingsManager::instance()->setAutoSelectJava(checked);
    emit javaSettingsChanged();
}

QWidget *InstanceJavaPage::createJavaItemWidget(const QString &path, const QString &version)
{
    auto *itemFrame = new QFrame();
    itemFrame->setObjectName("javaItemFrame");
    itemFrame->setFrameShape(QFrame::NoFrame);

    auto *itemLayout = new QHBoxLayout(itemFrame);
    itemLayout->setContentsMargins(12, 8, 12, 8);
    itemLayout->setSpacing(10);

    // 左侧：标题 + 路径 + 版本
    auto *infoWidget = new QWidget(itemFrame);
    auto *infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(3);

    int major = javaMajorVersion(version);
    QString typeStr = QFileInfo(QFileInfo(path).path() + QStringLiteral("/javac.exe")).exists()
                          ? QStringLiteral("JDK")
                          : QStringLiteral("JRE");
    QString titleStr = (major > 0)
                           ? QStringLiteral("%1 %2").arg(typeStr).arg(major)
                           : QStringLiteral("%1 %2").arg(typeStr).arg(version);
    auto *titleLabel = new QLabel(titleStr, infoWidget);
    titleLabel->setObjectName("javaItemTitle");

    auto *pathLabel = new QLabel(path, infoWidget);
    pathLabel->setObjectName("javaItemPath");
    pathLabel->setWordWrap(false);
    pathLabel->setToolTip(path);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *versionLabel = new QLabel(
        QStringLiteral("%1 · %2").arg(version).arg(tr("本机")), infoWidget);
    versionLabel->setObjectName("javaItemVersion");

    infoLayout->addWidget(titleLabel);
    infoLayout->addWidget(pathLabel);
    infoLayout->addWidget(versionLabel);

    // 右侧：操作按钮
    auto *actionsWidget = new QWidget(itemFrame);
    auto *actionsLayout = new QVBoxLayout(actionsWidget);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(4);

    auto *openFolderBtn = new QPushButton(tr("打开"), actionsWidget);
    openFolderBtn->setObjectName("javaOpenFolderBtn");
    openFolderBtn->setCursor(Qt::PointingHandCursor);
    openFolderBtn->setToolTip(tr("在文件资源管理器中打开 Java 所在目录"));
    connect(openFolderBtn, &QPushButton::clicked, this, [this, path]() {
        onOpenJavaFolder(path);
    });

    auto *removeBtn = new QPushButton(tr("移除"), actionsWidget);
    removeBtn->setObjectName("javaRemoveBtn");
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setToolTip(tr("从列表中移除该 Java（不会删除文件）"));
    connect(removeBtn, &QPushButton::clicked, this, [this, path]() {
        onRemoveJava(path);
    });

    actionsLayout->addWidget(openFolderBtn);
    actionsLayout->addWidget(removeBtn);
    actionsLayout->addStretch();

    itemLayout->addWidget(infoWidget, 1);
    itemLayout->addWidget(actionsWidget, 0, Qt::AlignTop);

    return itemFrame;
}

void InstanceJavaPage::refreshJavaList()
{
    if (!m_listWidget)
        return;

    m_listWidget->clear();

    QList<QPair<QString, QString>> installations =
        SettingsManager::instance()->getJavaInstallations();

    if (installations.isEmpty())
    {
        auto *emptyItem = new QListWidgetItem(m_listWidget);
        emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        m_listWidget->addItem(emptyItem);
        m_listWidget->setItemWidget(emptyItem, m_emptyLabel);
        emptyItem->setSizeHint(m_emptyLabel->sizeHint().expandedTo(QSize(0, 80)));
        return;
    }

    for (const auto &inst : installations)
    {
        auto *item = new QListWidgetItem(m_listWidget);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        QWidget *w = createJavaItemWidget(inst.first, inst.second);
        m_listWidget->addItem(item);
        m_listWidget->setItemWidget(item, w);
        item->setSizeHint(w->sizeHint().expandedTo(QSize(0, 84)));
    }
}

void InstanceJavaPage::refreshGlobalJavaCombo()
{
    if (!m_globalJavaCombo)
        return;

    m_globalJavaCombo->blockSignals(true);
    m_globalJavaCombo->clear();

    m_globalJavaCombo->addItem(tr("自动选择（按游戏版本）"), QStringLiteral("auto"));

    QList<QPair<QString, QString>> installations =
        SettingsManager::instance()->getJavaInstallations();
    for (const auto &inst : installations)
    {
        int major = javaMajorVersion(inst.second);
        QString label = (major > 0)
                            ? QStringLiteral("Java %1 (%2)").arg(major).arg(inst.second)
                            : QStringLiteral("Java %1").arg(inst.second);
        m_globalJavaCombo->addItem(label, inst.first);
    }

    // 还原当前选择
    QString currentPath = SettingsManager::instance()->getJavaPath();
    int idx = m_globalJavaCombo->findData(currentPath);
    if (idx >= 0)
    {
        m_globalJavaCombo->setCurrentIndex(idx);
    }
    else
    {
        m_globalJavaCombo->setCurrentIndex(0);
    }

    m_globalJavaCombo->blockSignals(false);

    bool autoSelect = SettingsManager::instance()->getAutoSelectJava();
    m_globalJavaCombo->setEnabled(!autoSelect);
}
