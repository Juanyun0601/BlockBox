/**
 * @file   MultiThreadDownloadPage.cpp
 * @brief  多线程下载页实现：直链 + 保存位置 + 连接数 → 分段并发下载，带暂停 / 继续
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 样式复用全局 QSS 里已有的对象名（settingsCard / settingRow / settingTitle /
 * settingDesc / settingValueLabel / largeProgressBar / launchButton / browseBtn），
 * 所以浅色、深色与自定义主题切换后不需要重新上色。
 */

#include "MultiThreadDownloadPage.h"

#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/CollapsibleSectionCard.h"
#include "components/NotificationManager.h"
#include "components/OutlinedLabel.h"
#include "utils/DownloadUtils.h"
#include "utils/FileExplorer.h"

namespace {

constexpr int kThreadCountMax = 32;
constexpr int kThreadCountDefault = 8;

/**
 * @brief 往卡片里加一行设置行（左侧标题 / 说明，右侧控件区），返回右侧布局
 *
 * 与设置页同款结构：整行一条分隔线，最后一行不带分隔线。
 */
QHBoxLayout *appendRow(QVBoxLayout *cardLayout, const QString &title, const QString &desc,
                       bool lastRow = false)
{
    auto *row = new QFrame();
    row->setObjectName(QStringLiteral("settingRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (lastRow)
        row->setProperty("lastRow", true);

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(24, 14, 24, 14);
    rowLayout->setSpacing(16);

    auto *info = new QWidget(row);
    info->setObjectName(QStringLiteral("settingInfo"));
    auto *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(3);

    if (!title.isEmpty()) {
        auto *titleLabel = new QLabel(title, info);
        titleLabel->setObjectName(QStringLiteral("settingTitle"));
        infoLayout->addWidget(titleLabel);
    }
    if (!desc.isEmpty()) {
        auto *descLabel = new QLabel(desc, info);
        descLabel->setObjectName(QStringLiteral("settingDesc"));
        descLabel->setWordWrap(true);
        infoLayout->addWidget(descLabel);
    }

    rowLayout->addWidget(info, 1);
    cardLayout->addWidget(row);
    return rowLayout;
}

/** 直链里带的文件名，取不到就给个通用名 */
QString fileNameFromUrl(const QString &urlText)
{
    const QUrl url(urlText.trimmed());
    QString name = url.fileName();
    if (name.contains(QLatin1Char('%')))
        name = QUrl::fromPercentEncoding(name.toUtf8());
    name = name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("download");

    // Windows 文件名里不能出现的字符换成下划线，免得保存时直接报错
    static const QRegularExpression invalidChars(QStringLiteral("[\\\\/:*?\"<>|]"));
    name.replace(invalidChars, QStringLiteral("_"));

    // 太长的话截断，留出扩展名的位置
    constexpr int kMaxNameLength = 120;
    if (name.size() > kMaxNameLength)
        name = name.left(kMaxNameLength);
    return name;
}

} // namespace

MultiThreadDownloadPage::MultiThreadDownloadPage(QWidget *parent)
    : QWidget(parent)
{
    m_downloader = new SegmentedDownloader(this);

    buildUi();

    connect(m_downloader, &SegmentedDownloader::stateChanged, this,
            [this](SegmentedDownloader::State) {
                updateStatus();
                updateInputsEnabled();
            });

    connect(m_downloader, &SegmentedDownloader::prepared, this,
            [this](qint64 totalBytes, bool rangeSupported) {
                m_rangeSupported = rangeSupported;
                m_lastTotal = totalBytes;
                updateStatus();
            });

    connect(m_downloader, &SegmentedDownloader::progress, this,
            [this](qint64 received, qint64 total, qint64 speed, int eta) {
                m_lastReceived = received;
                m_lastTotal = total;
                m_lastSpeed = speed;
                m_lastEta = eta;
                updateStatus();
            });

    connect(m_downloader, &SegmentedDownloader::finished, this,
            [this](const QString &savePath) {
                m_finishedPath = savePath;
                NotificationManager::showSuccess(
                    window(), tr("下载完成：%1").arg(QFileInfo(savePath).fileName()));
                updateStatus();
            });

    connect(m_downloader, &SegmentedDownloader::failed, this,
            [this](const QString &message) {
                if (m_finishedPath.isEmpty())
                    m_finishedPath = m_downloader->savePath();
                NotificationManager::showError(window(), tr("下载失败：%1").arg(message));
                updateStatus();
            });

    refresh();
}

MultiThreadDownloadPage::~MultiThreadDownloadPage()
{
    // 关窗口时还在下：把连接和半成品一起收拾掉，别留一个下了一半的文件
    if (m_downloader) {
        const SegmentedDownloader::State state = m_downloader->state();
        if (state == SegmentedDownloader::State::Probing
            || state == SegmentedDownloader::State::Downloading
            || state == SegmentedDownloader::State::Paused) {
            m_downloader->cancel();
        }
    }
}

void MultiThreadDownloadPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 28, 28);
    layout->setSpacing(10);

    auto *pageTitle = new OutlinedLabel(tr("多线程下载"), content);
    pageTitle->setObjectName(QStringLiteral("resourcesPageTitle"));
    layout->addWidget(pageTitle);

    auto *subtitle = new QLabel(
        tr("把一条下载直链拆成几段同时下载，大文件能快不少；网络抖一下会自己接着下，"
           "也可以随时暂停、继续"),
        content);
    subtitle->setObjectName(QStringLiteral("resourcesPageSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // ── 下载设置 ──
    auto *settingsCard = new CollapsibleSectionCard(tr("下载设置"), false, content);
    QVBoxLayout *settingsLayout = settingsCard->contentLayout();

    m_urlEdit = new QLineEdit(settingsCard);
    m_urlEdit->setPlaceholderText(tr("粘贴以 http:// 或 https:// 开头的直链"));
    m_urlEdit->setClearButtonEnabled(true);
    auto *pasteBtn = new QPushButton(tr("粘贴"), settingsCard);
    pasteBtn->setObjectName(QStringLiteral("browseBtn"));
    pasteBtn->setCursor(Qt::PointingHandCursor);
    connect(pasteBtn, &QPushButton::clicked, this, [this]() {
        const QString text = QGuiApplication::clipboard()->text().trimmed();
        if (text.isEmpty()) {
            NotificationManager::showInfo(window(), tr("剪贴板里没有文字"));
            return;
        }
        m_urlEdit->setText(text);
    });

    QHBoxLayout *urlRow = appendRow(
        settingsLayout, tr("下载地址"),
        tr("浏览器里能直接下的链接才算直链；网盘分享页、需要登录的页面都不行。"));
    urlRow->addWidget(m_urlEdit, 2);
    urlRow->addWidget(pasteBtn);

    m_saveEdit = new QLineEdit(settingsCard);
    m_saveEdit->setPlaceholderText(tr("选择保存位置…"));
    m_saveEdit->setReadOnly(true);
    auto *browseBtn = new QPushButton(tr("浏览…"), settingsCard);
    browseBtn->setObjectName(QStringLiteral("browseBtn"));
    browseBtn->setCursor(Qt::PointingHandCursor);
    connect(browseBtn, &QPushButton::clicked, this,
            &MultiThreadDownloadPage::browseSavePath);

    QHBoxLayout *saveRow = appendRow(
        settingsLayout, tr("保存位置"),
        tr("默认放到系统下载文件夹，文件名跟着直链走；同目录已经有同名文件时会先问你要不要覆盖。"));
    saveRow->addWidget(m_saveEdit, 2);
    saveRow->addWidget(browseBtn);

    m_threadSpin = new QSpinBox(settingsCard);
    m_threadSpin->setRange(1, kThreadCountMax);
    m_threadSpin->setValue(kThreadCountDefault);
    m_threadSpin->setSuffix(tr(" 条连接"));
    m_threadSpin->setFixedWidth(150);

    QHBoxLayout *threadRow = appendRow(
        settingsLayout, tr("连接数"),
        tr("默认 8 条，大文件建议 8~16；文件很小或服务器不支持分段时会自动少开几条。"));
    threadRow->addWidget(m_threadSpin);

    m_userAgentEdit = new QLineEdit(settingsCard);
    m_userAgentEdit->setPlaceholderText(tr("留空使用默认：BlockBox/1.0"));
    m_userAgentEdit->setClearButtonEnabled(true);

    QHBoxLayout *uaRow = appendRow(
        settingsLayout, tr("User-Agent"),
        tr("有些站点认浏览器标识才给下载，遇到「403 没权限」可以在这里填一个试试。"), true);
    uaRow->addWidget(m_userAgentEdit, 2);

    layout->addWidget(settingsCard);

    // ── 下载进度 ──
    auto *progressCard = new CollapsibleSectionCard(tr("下载进度"), false, content);
    QVBoxLayout *progressLayout = progressCard->contentLayout();

    auto *progressArea = new QWidget(progressCard);
    progressArea->setAttribute(Qt::WA_StyledBackground, false);
    auto *progressAreaLayout = new QVBoxLayout(progressArea);
    progressAreaLayout->setContentsMargins(24, 16, 24, 18);
    progressAreaLayout->setSpacing(10);

    m_progressBar = new QProgressBar(progressArea);
    m_progressBar->setObjectName(QStringLiteral("largeProgressBar"));
    m_progressBar->setTextVisible(false);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    progressAreaLayout->addWidget(m_progressBar);

    m_stateLabel = new QLabel(tr("等待开始"), progressArea);
    m_stateLabel->setObjectName(QStringLiteral("settingTitle"));
    progressAreaLayout->addWidget(m_stateLabel);

    m_detailLabel = new QLabel(tr("把直链贴到上面，点「开始下载」"), progressArea);
    m_detailLabel->setObjectName(QStringLiteral("settingDesc"));
    m_detailLabel->setWordWrap(true);
    progressAreaLayout->addWidget(m_detailLabel);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 4, 0, 0);
    buttonRow->setSpacing(10);

    m_startBtn = new QPushButton(tr("开始下载"), progressArea);
    m_startBtn->setObjectName(QStringLiteral("launchButton"));
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setMinimumHeight(40);
    m_startBtn->setMinimumWidth(132);
    connect(m_startBtn, &QPushButton::clicked, this,
            &MultiThreadDownloadPage::startDownload);

    m_pauseBtn = new QPushButton(tr("暂停"), progressArea);
    m_pauseBtn->setObjectName(QStringLiteral("browseBtn"));
    m_pauseBtn->setCursor(Qt::PointingHandCursor);
    m_pauseBtn->setMinimumHeight(40);
    m_pauseBtn->setMinimumWidth(88);
    connect(m_pauseBtn, &QPushButton::clicked, this, [this]() {
        if (!m_downloader)
            return;
        if (m_downloader->state() == SegmentedDownloader::State::Paused)
            m_downloader->resume();
        else
            m_downloader->pause();
    });

    m_cancelBtn = new QPushButton(tr("取消"), progressArea);
    m_cancelBtn->setObjectName(QStringLiteral("browseBtn"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setMinimumHeight(40);
    m_cancelBtn->setMinimumWidth(88);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_downloader) {
            m_downloader->cancel();
            NotificationManager::showInfo(window(), tr("已取消下载，没下完的文件也删掉了"));
        }
    });

    m_openDirBtn = new QPushButton(tr("打开所在文件夹"), progressArea);
    m_openDirBtn->setObjectName(QStringLiteral("browseBtn"));
    m_openDirBtn->setCursor(Qt::PointingHandCursor);
    m_openDirBtn->setMinimumHeight(40);
    connect(m_openDirBtn, &QPushButton::clicked, this,
            &MultiThreadDownloadPage::openDownloadFolder);

    buttonRow->addWidget(m_startBtn);
    buttonRow->addWidget(m_pauseBtn);
    buttonRow->addWidget(m_cancelBtn);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_openDirBtn);
    progressAreaLayout->addLayout(buttonRow);

    progressLayout->addWidget(progressArea);

    layout->addWidget(progressCard);

    layout->addStretch(1);

    auto *hint = new QLabel(
        tr("能不能快取决于服务器：有些站点只给单 IP 很低的带宽，分几段也快不了多少；"
           "百度网盘、夸克网盘这类分享页需要专用客户端，贴到这里是下不动的。"),
        content);
    hint->setObjectName(QStringLiteral("forumMuted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    scroll->setWidget(content);
    outer->addWidget(scroll);

    // 直链变化时把默认文件名带上（用户手选过保存位置就不再自动改）
    connect(m_urlEdit, &QLineEdit::textChanged, this,
            [this](const QString &) { syncSuggestedPath(); });
    connect(m_saveEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        updateStatus();
    });

    updateStatus();
    updateInputsEnabled();
}

void MultiThreadDownloadPage::refresh()
{
    if (!m_downloader)
        return;
    const auto state = m_downloader->state();
    if (state == SegmentedDownloader::State::Idle && m_saveEdit
        && m_saveEdit->text().trimmed().isEmpty()) {
        syncSuggestedPath();
    }
    updateStatus();
    updateInputsEnabled();
}

QString MultiThreadDownloadPage::defaultDownloadDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    return dir;
}

void MultiThreadDownloadPage::syncSuggestedPath()
{
    if (!m_autoSavePath || !m_saveEdit || !m_urlEdit)
        return;

    const QString urlText = m_urlEdit->text().trimmed();
    const QString name = fileNameFromUrl(urlText);
    m_saveEdit->setText(QDir::toNativeSeparators(
        QDir(defaultDownloadDir()).filePath(name)));
}

void MultiThreadDownloadPage::browseSavePath()
{
    QString startPath = m_saveEdit ? m_saveEdit->text().trimmed() : QString();
    if (startPath.isEmpty()) {
        startPath = QDir(defaultDownloadDir())
                        .filePath(fileNameFromUrl(m_urlEdit ? m_urlEdit->text() : QString()));
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("选择保存位置"), startPath);
    if (path.isEmpty())
        return;

    m_saveEdit->setText(QDir::toNativeSeparators(path));
    m_autoSavePath = false;
}

void MultiThreadDownloadPage::startDownload()
{
    if (!m_downloader)
        return;

    const QString urlText = m_urlEdit ? m_urlEdit->text().trimmed() : QString();
    if (urlText.isEmpty()) {
        AppMessageBox::warning(this, tr("多线程下载"), tr("先把下载直链贴进来"));
        if (m_urlEdit)
            m_urlEdit->setFocus();
        return;
    }

    const QUrl url(urlText);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) {
        AppMessageBox::warning(this, tr("多线程下载"),
                               tr("链接要以 http:// 或 https:// 开头，"
                                  "网页地址、网盘分享页这类链接贴进来也下不了"));
        return;
    }

    QString savePath = m_saveEdit ? m_saveEdit->text().trimmed() : QString();
    if (savePath.isEmpty()) {
        m_autoSavePath = true;
        syncSuggestedPath();
        savePath = m_saveEdit ? m_saveEdit->text().trimmed() : QString();
    }
    if (savePath.isEmpty()) {
        AppMessageBox::warning(this, tr("多线程下载"), tr("请先选择文件保存到哪里"));
        return;
    }

    if (QFileInfo::exists(savePath)) {
        const auto choice = AppMessageBox::question(
            this, tr("文件已存在"),
            tr("「%1」已经存在，要覆盖掉它重新下吗？").arg(QFileInfo(savePath).fileName()),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
        if (choice != AppMessageBox::Yes)
            return;
    }

    m_rangeSupported = true;
    m_finishedPath.clear();
    m_lastReceived = 0;
    m_lastTotal = -1;
    m_lastSpeed = 0;
    m_lastEta = -1;

    m_downloader->setSegmentCount(m_threadSpin ? m_threadSpin->value() : kThreadCountDefault);
    m_downloader->setUserAgent(m_userAgentEdit ? m_userAgentEdit->text() : QString());
    m_downloader->start(url, savePath);

    updateStatus();
    updateInputsEnabled();
}

void MultiThreadDownloadPage::updateInputsEnabled()
{
    const bool busy = m_downloader
                      && (m_downloader->state() == SegmentedDownloader::State::Probing
                          || m_downloader->state() == SegmentedDownloader::State::Downloading
                          || m_downloader->state() == SegmentedDownloader::State::Paused);

    if (m_urlEdit)
        m_urlEdit->setEnabled(!busy);
    if (m_saveEdit)
        m_saveEdit->setEnabled(!busy);
    if (m_threadSpin)
        m_threadSpin->setEnabled(!busy);
    if (m_userAgentEdit)
        m_userAgentEdit->setEnabled(!busy);
}

void MultiThreadDownloadPage::updateStatus()
{
    if (!m_downloader || !m_stateLabel || !m_progressBar)
        return;

    const auto state = m_downloader->state();
    const qint64 received = m_lastReceived;
    const qint64 total = m_lastTotal;

    // ── 进度条 ──
    if (total > 0) {
        const int permille = static_cast<int>(
            qBound<qint64>(qint64(0), received * 1000 / total, qint64(1000)));
        m_progressBar->setRange(0, 1000);
        m_progressBar->setValue(permille);
    } else if (state == SegmentedDownloader::State::Downloading) {
        // 总大小未知（分块传输）：用忙碌态进度条，至少让人看出还在动
        m_progressBar->setRange(0, 0);
    } else if (state == SegmentedDownloader::State::Completed) {
        // 单连接且服务器没给总大小时也算下完了，进度条直接拉满
        m_progressBar->setRange(0, 1000);
        m_progressBar->setValue(1000);
    } else {
        m_progressBar->setRange(0, 1000);
        m_progressBar->setValue(0);
    }

    // ── 状态标题 ──
    QString stateText;
    switch (state) {
    case SegmentedDownloader::State::Idle:
        stateText = tr("等待开始");
        break;
    case SegmentedDownloader::State::Probing:
        stateText = tr("正在连接服务器…");
        break;
    case SegmentedDownloader::State::Downloading:
        stateText = m_rangeSupported
                        ? tr("下载中 · %1 条连接").arg(m_downloader->activeSegmentCount())
                        : tr("下载中 · 单连接（服务器不支持分段）");
        break;
    case SegmentedDownloader::State::Paused:
        stateText = tr("已暂停");
        break;
    case SegmentedDownloader::State::Completed:
        stateText = tr("下载完成");
        break;
    case SegmentedDownloader::State::Failed:
        stateText = tr("下载失败");
        break;
    case SegmentedDownloader::State::Cancelled:
        stateText = tr("已取消");
        break;
    }
    m_stateLabel->setText(stateText);

    // ── 明细：大小 / 速度 / 剩余时间 / 错误原因 ──
    QString detail;
    switch (state) {
    case SegmentedDownloader::State::Idle:
        detail = tr("把直链贴到上面，点「开始下载」");
        break;
    case SegmentedDownloader::State::Probing:
        detail = tr("正在读取文件大小与服务器是否支持分段…");
        break;
    case SegmentedDownloader::State::Downloading:
    case SegmentedDownloader::State::Paused:
    case SegmentedDownloader::State::Completed: {
        if (total > 0) {
            const int percent = static_cast<int>(
                qBound<qint64>(qint64(0), received * 100 / total, qint64(100)));
            detail = tr("%1% · 已下载 %2 / %3")
                         .arg(percent)
                         .arg(DownloadUtils::formatSize(received),
                              DownloadUtils::formatSize(total));
        } else {
            detail = tr("已下载 %1").arg(DownloadUtils::formatSize(received));
        }
        if (state == SegmentedDownloader::State::Downloading && m_lastSpeed > 0) {
            detail += tr(" · %1").arg(DownloadUtils::formatSpeed(m_lastSpeed));
            if (m_lastEta >= 0)
                detail += tr(" · 剩余 %1").arg(DownloadUtils::formatEta(m_lastEta));
        } else if (state == SegmentedDownloader::State::Paused) {
            detail += tr(" · 点「继续」接着下");
        } else if (state == SegmentedDownloader::State::Completed) {
            detail += tr(" · 已保存到 %1")
                          .arg(QDir::toNativeSeparators(m_downloader->savePath()));
        }
        break;
    }
    case SegmentedDownloader::State::Failed:
        detail = m_downloader->errorMessage().isEmpty()
                     ? tr("下载没有成功，可以换个链接或调小连接数再试")
                     : m_downloader->errorMessage();
        break;
    case SegmentedDownloader::State::Cancelled:
        detail = tr("已取消，没下完的部分也删掉了");
        break;
    }
    m_detailLabel->setText(detail);

    // ── 按钮 ──
    const bool busy = state == SegmentedDownloader::State::Probing
                      || state == SegmentedDownloader::State::Downloading
                      || state == SegmentedDownloader::State::Paused;

    if (m_startBtn) {
        m_startBtn->setEnabled(!busy);
        m_startBtn->setText(state == SegmentedDownloader::State::Failed ? tr("重新开始")
                                                                       : tr("开始下载"));
    }

    if (m_pauseBtn) {
        const bool canPause =
            (state == SegmentedDownloader::State::Downloading && m_rangeSupported)
            || state == SegmentedDownloader::State::Paused;
        m_pauseBtn->setEnabled(canPause);
        m_pauseBtn->setText(state == SegmentedDownloader::State::Paused ? tr("继续")
                                                                        : tr("暂停"));
    }

    if (m_cancelBtn)
        m_cancelBtn->setEnabled(busy);

    if (m_openDirBtn) {
        const bool finishedOrFailed = state == SegmentedDownloader::State::Completed
                                      || state == SegmentedDownloader::State::Failed;
        // 失败时成品不存在，但 *.part 还在，同样让人能去目录里看一眼
        m_openDirBtn->setEnabled(finishedOrFailed && !existingDownloadPath().isEmpty());
    }
}

QString MultiThreadDownloadPage::existingDownloadPath() const
{
    if (!m_downloader)
        return QString();

    const QString target =
        m_finishedPath.isEmpty() ? m_downloader->savePath() : m_finishedPath;
    if (!target.isEmpty() && QFileInfo::exists(target))
        return target;

    const QString partial = m_downloader->partialPath();
    if (!partial.isEmpty() && QFileInfo::exists(partial))
        return partial;

    return QString();
}

void MultiThreadDownloadPage::openDownloadFolder()
{
    const QString path = existingDownloadPath();
    if (path.isEmpty())
        return;
    FileExplorer::open(this, path);
}
