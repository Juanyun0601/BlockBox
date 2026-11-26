/**
 * @file   ServerDownloadDialog.cpp
 * @brief  「下载服务端」弹窗实现
 * @author BlockBox Team
 * @date   2026-11-26
 */
#include "ServerDownloadDialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QUrl>
#include <QVector>
#include <QVBoxLayout>
#include <QApplication>
#include <QClipboard>

#include "NotificationManager.h"
#include "platform.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace {

constexpr int kCardWidth = 520;

/** 服务端目录的根：桌面端跟在启动器目录下，移动端只能写数据目录 */
QString serverRootDir()
{
    if (Platform::isMobile() || Platform::isHarmony())
        return Platform::getMinecraftDirectory() + QStringLiteral("/servers");
    return QCoreApplication::applicationDirPath() + QStringLiteral("/servers");
}

} // namespace

ServerDownloadDialog::ServerDownloadDialog(const QString &versionId, QWidget *parent)
    : AppDialogBase(parent)
    , m_version(versionId)
    , m_manager(new LocalServerManager(this))
{
    buildUi();
    applyStyle();
    rebuildCoreCombo();

    LocalServerManager::Config config = buildConfig();
    m_autoDir = suggestedDir(config.core);
    m_dirEdit->setText(m_autoDir);

    connect(m_manager, &LocalServerManager::progressChanged, this,
            [this](const QString &text, int percent) {
        if (!text.isEmpty())
            setStatus(text);
        if (percent >= 0) {
            m_progress->setRange(0, 100);
            m_progress->setValue(qBound(0, percent, 100));
        } else {
            m_progress->setRange(0, 0);   // 不确定进度
        }
    });
    connect(m_manager, &LocalServerManager::logLine, this, [this](const QString &line) {
        // 只挑关键几行显示，别把日志刷满
        if (line.contains(QStringLiteral("将安装")) || line.contains(QStringLiteral("正在运行"))
            || line.contains(QStringLiteral("下载")) || line.contains(QStringLiteral("已下载")))
            setStatus(line);
    });
    connect(m_manager, &LocalServerManager::downloadFinished, this,
            [this](const QString &path, bool needsInstall) {
        m_lastPath = path;
        m_needsInstall = needsInstall;
        m_finished = true;
        setBusy(false);
        m_progress->setRange(0, 100);
        m_progress->setValue(100);
        m_startBtn->setText(tr("重新下载"));
        m_openBtn->setEnabled(true);
        m_copyBtn->setEnabled(true);
        if (needsInstall) {
            setStatus(tr("安装器已下载：%1").arg(QFileInfo(path).fileName()));
            m_coreHintLabel->setText(
                tr("这是 %1 的官方安装器：在这个目录里执行 "
                   "java -jar %2 --installServer 就会装好；"
                   "也可以直接在实例助手的「开服」页一键安装并开服。")
                    .arg(LocalServerManager::coreDisplayName(currentCore()),
                         QFileInfo(path).fileName()));
        } else {
            setStatus(tr("服务端已下载：%1").arg(QFileInfo(path).fileName()));
        }
        NotificationManager::showSuccess(this, tr("服务端已下载到 %1").arg(QFileInfo(path).absolutePath()));
    });
    connect(m_manager, &LocalServerManager::failed, this, [this](const QString &reason) {
        setBusy(false);
        m_progress->hide();
        setStatus(reason, true);
    });
}

void ServerDownloadDialog::openFor(QWidget *parent, const QString &versionId)
{
    if (versionId.trimmed().isEmpty()) {
        NotificationManager::showInfo(parent, QObject::tr("没读到版本号，刷新一下版本列表再试。"));
        return;
    }
    ServerDownloadDialog dialog(versionId, parent);
    dialog.exec();
}

void ServerDownloadDialog::showEvent(QShowEvent *event)
{
    AppDialogBase::showEvent(event);
    // 宿主窗口比卡片窄时（小窗 / 手机），把卡片收窄，别让右边的按钮跑到屏幕外
    if (m_card) {
        int available = kCardWidth;
        if (QWidget *host = hostWindow())
            available = host->width() - 40;
        const int width = qBound(300, qMin(kCardWidth, available), kCardWidth);
        m_card->setMinimumWidth(qMin(300, width));
        m_card->setMaximumWidth(width);
    }
}

void ServerDownloadDialog::buildUi()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("serverDownloadCard"));
    card->setFixedWidth(kCardWidth);
    m_card = card;

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(card, 0, Qt::AlignCenter);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("下载服务端"), card);
    title->setObjectName(QStringLiteral("serverDownloadTitle"));
    auto *subtitle = new QLabel(
        tr("下好就能自己开服给朋友玩；本机开服也可以直接用实例助手的「开服」页。"), card);
    subtitle->setObjectName(QStringLiteral("serverDownloadSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(subtitle);

    // 版本（只读）
    auto *versionRow = new QHBoxLayout();
    versionRow->setSpacing(8);
    auto *versionLabel = new QLabel(tr("版本"), card);
    versionLabel->setObjectName(QStringLiteral("serverDownloadFieldLabel"));
    versionLabel->setFixedWidth(56);
    m_versionLabel = new QLabel(m_version, card);
    m_versionLabel->setObjectName(QStringLiteral("serverDownloadVersion"));
    m_versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    versionRow->addWidget(versionLabel);
    versionRow->addWidget(m_versionLabel, 1);
    layout->addLayout(versionRow);

    // 服务端核心
    auto *coreRow = new QHBoxLayout();
    coreRow->setSpacing(8);
    auto *coreLabel = new QLabel(tr("核心"), card);
    coreLabel->setObjectName(QStringLiteral("serverDownloadFieldLabel"));
    coreLabel->setFixedWidth(56);
    m_coreCombo = new QComboBox(card);
    m_coreCombo->setObjectName(QStringLiteral("serverDownloadCombo"));
    connect(m_coreCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
        refreshDirForCore();
        m_coreHintLabel->setText(
            LocalServerManager::coreUsesInstaller(currentCore())
                ? tr("这个核心给的是官方安装器，装好之后才能开服。")
                : tr("直接就是服务端 jar，有 Java 就能 java -jar 开服。"));
    });
    coreRow->addWidget(coreLabel);
    coreRow->addWidget(m_coreCombo, 1);
    layout->addLayout(coreRow);

    m_coreHintLabel = new QLabel(card);
    m_coreHintLabel->setObjectName(QStringLiteral("serverDownloadHint"));
    m_coreHintLabel->setWordWrap(true);
    layout->addWidget(m_coreHintLabel);

    // 保存位置
    auto *dirRow = new QHBoxLayout();
    dirRow->setSpacing(8);
    auto *dirLabel = new QLabel(tr("保存到"), card);
    dirLabel->setObjectName(QStringLiteral("serverDownloadFieldLabel"));
    dirLabel->setFixedWidth(56);
    m_dirEdit = new QLineEdit(card);
    m_dirEdit->setObjectName(QStringLiteral("serverDownloadDirEdit"));
    m_dirEdit->setClearButtonEnabled(true);
    m_browseBtn = new QPushButton(tr("浏览…"), card);
    m_browseBtn->setObjectName(QStringLiteral("serverDownloadGhostBtn"));
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_browseBtn, &QPushButton::clicked, this, &ServerDownloadDialog::pickDir);
    dirRow->addWidget(dirLabel);
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(m_browseBtn);
    layout->addLayout(dirRow);

    // 进度条 + 状态
    m_progress = new QProgressBar(card);
    m_progress->setObjectName(QStringLiteral("serverDownloadProgress"));
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->hide();
    layout->addWidget(m_progress);

    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("serverDownloadStatus"));
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // 下载完成后才出现的两个小按钮：单独一行，窄窗口下也不挤
    auto *resultRow = new QHBoxLayout();
    resultRow->setSpacing(8);
    m_openBtn = new QPushButton(tr("打开目录"), card);
    m_openBtn->setObjectName(QStringLiteral("serverDownloadGhostBtn"));
    m_openBtn->setCursor(Qt::PointingHandCursor);
    // 常驻显示但先禁用：AppDialogBase 会把卡片内容套进滚动区，
    // 内容一会长高容易把最后一行裁掉，布局保持稳定最省事
    m_openBtn->setEnabled(false);
    connect(m_openBtn, &QPushButton::clicked, this, &ServerDownloadDialog::openDownloadDir);

    m_copyBtn = new QPushButton(tr("复制路径"), card);
    m_copyBtn->setObjectName(QStringLiteral("serverDownloadGhostBtn"));
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setEnabled(false);
    connect(m_copyBtn, &QPushButton::clicked, this, &ServerDownloadDialog::copyDownloadPath);

    resultRow->addWidget(m_openBtn);
    resultRow->addWidget(m_copyBtn);
    resultRow->addStretch(1);
    layout->addLayout(resultRow);

    // 底部按钮：关闭 + 开始下载
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    m_closeBtn = new QPushButton(tr("关闭"), card);
    m_closeBtn->setObjectName(QStringLiteral("serverDownloadGhostBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);

    m_startBtn = new QPushButton(tr("开始下载"), card);
    m_startBtn->setObjectName(QStringLiteral("serverDownloadPrimaryBtn"));
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setMinimumHeight(34);
    connect(m_startBtn, &QPushButton::clicked, this, &ServerDownloadDialog::startDownload);

    btnRow->addStretch(1);
    btnRow->addWidget(m_closeBtn);
    btnRow->addWidget(m_startBtn);
    layout->addLayout(btnRow);
}

void ServerDownloadDialog::applyStyle()
{
    // 注意：AppDialogBase 在构造里给 QDialog 设了透明底色（模糊背景由基类绘制），
    // 这里整窗设样式表会把那条覆盖掉，所以要把它一起写进来
    setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QDialog { background-color: transparent; border: none; }"
        "QFrame#serverDownloadCard {"
        "  background-color: @BG_CARD@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 14px;"
        "}"
        "QLabel#serverDownloadTitle {"
        "  color: @TEXT_PRIMARY@; font-size: 16px; font-weight: 700;"
        "}"
        "QLabel#serverDownloadSubtitle {"
        "  color: @TEXT_SECONDARY@; font-size: 12px;"
        "}"
        "QLabel#serverDownloadFieldLabel {"
        "  color: @TEXT_SECONDARY@; font-size: 12px; font-weight: 600;"
        "}"
        "QLabel#serverDownloadVersion {"
        "  color: @TEXT_PRIMARY@; font-size: 13px; font-weight: 700;"
        "}"
        "QLabel#serverDownloadHint {"
        "  color: @TEXT_TERTIARY@; font-size: 11.5px;"
        "}"
        "QLabel#serverDownloadStatus {"
        "  color: @TEXT_SECONDARY@; font-size: 11.5px;"
        "}"
        "QComboBox#serverDownloadCombo, QLineEdit#serverDownloadDirEdit {"
        "  background-color: @BG_MUTED@;"
        "  border: 1px solid @BORDER_STRONG@;"
        "  border-radius: 8px;"
        "  padding: 6px 10px;"
        "  min-height: 18px;"
        "  color: @TEXT_PRIMARY@;"
        "  font-size: 12.5px;"
        "}"
        "QComboBox#serverDownloadCombo:hover, QLineEdit#serverDownloadDirEdit:hover {"
        "  background-color: @BG_HOVER@;"
        "  border-color: @BORDER_HOVER@;"
        "}"
        "QComboBox#serverDownloadCombo:disabled, QLineEdit#serverDownloadDirEdit:disabled {"
        "  background-color: @BG_DISABLED@;"
        "  border-color: @BORDER_DISABLED@;"
        "  color: @TEXT_DISABLED@;"
        "}"
        "QComboBox#serverDownloadCombo::drop-down {"
        "  subcontrol-origin: padding; subcontrol-position: center right;"
        "  border: none; width: 22px;"
        "}"
        "QComboBox#serverDownloadCombo::down-arrow {"
        "  image: url(:/Images/Icons/arrow_down_triangle.svg);"
        "  width: 10px; height: 10px; margin-right: 8px; border: none;"
        "}"
        "QPushButton#serverDownloadPrimaryBtn {"
        "  background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 @PRIMARY@, stop:1 @PRIMARY_DARK@);"
        "  border: 1px solid @PRIMARY_DARK@;"
        "  border-radius: 9px;"
        "  padding: 8px 20px;"
        "  color: @TEXT_ON_PRIMARY@;"
        "  font-size: 13px; font-weight: 700;"
        "}"
        "QPushButton#serverDownloadPrimaryBtn:hover {"
        "  background-color: @PRIMARY_HOVER@; border-color: @PRIMARY_HOVER@;"
        "}"
        "QPushButton#serverDownloadPrimaryBtn:disabled {"
        "  background-color: @BG_DISABLED@;"
        "  border-color: @BORDER_DISABLED@;"
        "  color: @TEXT_DISABLED@;"
        "}"
        "QPushButton#serverDownloadGhostBtn {"
        "  background-color: @BG_MUTED@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 9px;"
        "  padding: 7px 14px;"
        "  color: @TEXT_SECONDARY@;"
        "  font-size: 12px;"
        "}"
        "QPushButton#serverDownloadGhostBtn:hover {"
        "  background-color: @BG_HOVER@;"
        "  border-color: @BORDER_HOVER@;"
        "  color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#serverDownloadGhostBtn:disabled {"
        "  color: @TEXT_DISABLED@; border-color: @BORDER_DISABLED@;"
        "}"
        "QProgressBar#serverDownloadProgress {"
        "  background-color: @BG_MUTED@;"
        "  border: none; border-radius: 3px; max-height: 6px;"
        "}"
        "QProgressBar#serverDownloadProgress::chunk {"
        "  background-color: @PRIMARY@; border-radius: 3px;"
        "}")));
}

void ServerDownloadDialog::rebuildCoreCombo()
{
    const auto previous = static_cast<LocalServerManager::Core>(
        m_coreCombo->count() > 0 ? m_coreCombo->currentData().toInt()
                                 : static_cast<int>(LocalServerManager::Core::Vanilla));

    m_coreCombo->blockSignals(true);
    m_coreCombo->clear();
    const QVector<LocalServerManager::Core> cores = {
        LocalServerManager::Core::Vanilla, LocalServerManager::Core::Paper,
        LocalServerManager::Core::Fabric, LocalServerManager::Core::Forge,
        LocalServerManager::Core::NeoForge};
    for (const auto core : cores) {
        if (!LocalServerManager::coreAvailableForVersion(core, m_version))
            continue;
        m_coreCombo->addItem(LocalServerManager::coreDisplayName(core), static_cast<int>(core));
    }
    int index = m_coreCombo->findData(static_cast<int>(previous));
    if (index < 0)
        index = 0;
    m_coreCombo->setCurrentIndex(index);
    m_coreCombo->blockSignals(false);

    m_coreHintLabel->setText(
        LocalServerManager::coreUsesInstaller(currentCore())
            ? tr("这个核心给的是官方安装器，装好之后才能开服。")
            : tr("直接就是服务端 jar，有 Java 就能 java -jar 开服。"));
}

QString ServerDownloadDialog::suggestedDir(LocalServerManager::Core core) const
{
    const QString folder = QStringLiteral("%1-%2").arg(
        m_version, LocalServerManager::coreKey(core));
    return QDir::cleanPath(serverRootDir() + QLatin1Char('/') + folder);
}

void ServerDownloadDialog::refreshDirForCore()
{
    const QString current = QDir::cleanPath(m_dirEdit->text().trimmed());
    // 用户自己改过目录就别动它
    if (!current.isEmpty() && current != QDir::cleanPath(m_autoDir))
        return;
    m_autoDir = suggestedDir(currentCore());
    m_dirEdit->setText(m_autoDir);
}

void ServerDownloadDialog::pickDir()
{
    const QString start = m_dirEdit->text().trimmed().isEmpty()
                              ? serverRootDir()
                              : m_dirEdit->text().trimmed();
    const QString chosen = QFileDialog::getExistingDirectory(
        this, tr("选一个位置保存服务端"), start);
    if (chosen.isEmpty())
        return;
    // 选的是父目录时，自动在里面再建一层「版本-核心」，免得几个服务端混在一起
    const QString leaf = QFileInfo(chosen).fileName();
    const QString wantPrefix = m_version + QLatin1Char('-');
    if (leaf.startsWith(wantPrefix))
        m_dirEdit->setText(QDir::cleanPath(chosen));
    else
        m_dirEdit->setText(QDir::cleanPath(
            chosen + QLatin1Char('/') + m_version + QLatin1Char('-')
            + LocalServerManager::coreKey(currentCore())));
}

LocalServerManager::Core ServerDownloadDialog::currentCore() const
{
    return static_cast<LocalServerManager::Core>(m_coreCombo->currentData().toInt());
}

LocalServerManager::Config ServerDownloadDialog::buildConfig() const
{
    LocalServerManager::Config config;
    config.version = m_version;
    config.core = currentCore();
    config.serverDirOverride = m_dirEdit->text().trimmed();
    config.downloadOnly = true;
    return config;
}

void ServerDownloadDialog::startDownload()
{
    if (m_busy)
        return;

    const QString dir = m_dirEdit->text().trimmed();
    if (dir.isEmpty()) {
        setStatus(tr("先选一个保存位置。"), true);
        return;
    }

    m_finished = false;
    m_lastPath.clear();
    // 常驻显示但先禁用：AppDialogBase 会把卡片内容套进滚动区，
    // 内容一会长高容易把最后一行裁掉，布局保持稳定最省事
    m_openBtn->setEnabled(false);
    m_copyBtn->setEnabled(false);
    m_startBtn->setText(tr("开始下载"));
    m_progress->show();
    m_progress->setRange(0, 0);
    setBusy(true);
    setStatus(tr("正在准备下载…"));

    m_manager->setConfig(buildConfig());
    // 重新开一次：管理器失败后会回到 Idle，能直接复用；卡在别的状态就先停掉
    if (m_manager->state() != LocalServerManager::State::Idle)
        m_manager->stop();
    m_manager->start();
}

void ServerDownloadDialog::openDownloadDir()
{
    const QString dir = m_lastPath.isEmpty() ? m_dirEdit->text().trimmed()
                                             : QFileInfo(m_lastPath).absolutePath();
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void ServerDownloadDialog::copyDownloadPath()
{
    if (m_lastPath.isEmpty())
        return;
    QApplication::clipboard()->setText(m_lastPath);
    NotificationManager::showSuccess(this, tr("路径已复制"));
}

void ServerDownloadDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_coreCombo->setEnabled(!busy);
    m_dirEdit->setEnabled(!busy);
    m_browseBtn->setEnabled(!busy);
    m_startBtn->setEnabled(!busy);
    m_startBtn->setText(busy ? tr("下载中…") : (m_finished ? tr("重新下载") : tr("开始下载")));
}

void ServerDownloadDialog::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color: #e5484d;") : QString());
}
