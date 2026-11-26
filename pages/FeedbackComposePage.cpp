/**
 * @file   FeedbackComposePage.cpp
 * @brief  提交新反馈页实现
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "FeedbackComposePage.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "utils/AppVersion.h"
#include "utils/ForumAccountService.h"
#include "utils/LauncherLogger.h"
#include "utils/StyleKit.h"

namespace {
constexpr int kDescMinLength = 5;
constexpr int kDescMaxLength = 800;
constexpr int kStepsMaxLength = 800;
constexpr int kExpectedMaxLength = 400;
constexpr int kActualMaxLength = 400;
constexpr int kOsVersionMaxLength = 100;
constexpr int kAppVersionMaxLength = 40;
constexpr int kLogsMaxLength = 2000;
constexpr int kScreenshotsMaxLength = 500;
constexpr int kSceneMaxLength = 800;

/** 日志上传成功后填进日志栏的大小标注（"1.2 MB" / "850 KB"） */
QString logSizeLabel(qint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / 1048576.0, 'f', 1));
    return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 0));
}

/** 「小标题 + 说明行 + 输入框」的字段块，切换类型时整块显隐 */
QWidget *makeFieldBlock(QWidget *parent, QLabel **titleLabel, const QString &title,
                        QPlainTextEdit **edit, const QString &placeholder, int minHeight,
                        const QString &hint = QString())
{
    auto *block = new QWidget(parent);
    block->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *label = ForumKit::makeMuted(block, title);
    layout->addWidget(label);
    if (!hint.isEmpty())
        layout->addWidget(ForumKit::makeMuted(block, hint));
    auto *input = new QPlainTextEdit(block);
    input->setObjectName(QStringLiteral("forumReplyEdit"));
    input->setPlaceholderText(placeholder);
    input->setMinimumHeight(minHeight);
    layout->addWidget(input);
    if (titleLabel)
        *titleLabel = label;
    if (edit)
        *edit = input;
    return block;
}

/** 「小标题 + 单行输入框」的字段块 */
QWidget *makeInputBlock(QWidget *parent, const QString &title, QLineEdit **edit,
                        const QString &placeholder, const QString &hint = QString())
{
    auto *block = new QWidget(parent);
    block->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(ForumKit::makeMuted(block, title));
    if (!hint.isEmpty())
        layout->addWidget(ForumKit::makeMuted(block, hint));
    auto *input = new QLineEdit(block);
    input->setObjectName(QStringLiteral("forumField"));
    input->setPlaceholderText(placeholder);
    layout->addWidget(input);
    if (edit)
        *edit = input;
    return block;
}
} // namespace

FeedbackComposePage::FeedbackComposePage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            &FeedbackComposePage::updateState);
}

void FeedbackComposePage::buildUi()
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
    layout->setSpacing(12);

    // ── 未登录态 ──
    m_loginHost = new QWidget(content);
    m_loginHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *loginLayout = new QVBoxLayout(m_loginHost);
    loginLayout->setContentsMargins(0, 60, 0, 60);
    loginLayout->setSpacing(12);
    auto *loginHint = new QLabel(tr("登录论坛账号后就可以提交反馈了，处理进度会在反馈列表里同步"), m_loginHost);
    loginHint->setObjectName(QStringLiteral("forumStatus"));
    loginHint->setAlignment(Qt::AlignCenter);
    loginLayout->addWidget(loginHint);
    auto *loginButton = new QPushButton(tr("去登录"), m_loginHost);
    loginButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    loginButton->setCursor(Qt::PointingHandCursor);
    loginButton->setFixedHeight(34);
    connect(loginButton, &QPushButton::clicked, this, &FeedbackComposePage::loginRequested);
    loginLayout->addWidget(loginButton, 0, Qt::AlignHCenter);
    layout->addWidget(m_loginHost);

    // ── 登录后的提交表单 ──
    m_mainHost = new QWidget(content);
    m_mainHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *mainLayout = new QVBoxLayout(m_mainHost);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(10);

    auto *formCard = ForumKit::makeCard(m_mainHost);
    auto *formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(20, 16, 20, 16);
    formLayout->setSpacing(10);

    formLayout->addWidget(ForumKit::makeSectionTitle(formCard, tr("提交反馈")));
    formLayout->addWidget(ForumKit::makeMuted(
        formCard, tr("带 * 的为必填项；信息填得越完整，问题定位和修复得越快。")));

    // 反馈类型
    auto *typeRow = new QHBoxLayout();
    typeRow->setSpacing(10);
    typeRow->addWidget(ForumKit::makeMuted(formCard, tr("反馈类型 *")));
    m_typeCombo = new QComboBox(formCard);
    m_typeCombo->setObjectName(QStringLiteral("forumField"));
    m_typeCombo->addItem(tr("🐛 问题反馈"));
    m_typeCombo->addItem(tr("💡 功能建议"));
    m_typeCombo->setFixedWidth(160);
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, [this](int) { applyType(); });
    typeRow->addWidget(m_typeCombo);
    typeRow->addStretch(1);
    formLayout->addLayout(typeRow);

    // ── 问题反馈字段（与 GitHub Issue 模板一致） ──

    // 启动器版本（必填，预填当前版本）
    m_appVersionField = makeInputBlock(formCard, tr("启动器版本 *"), &m_appVersionEdit,
                                       tr("例如 beta7"), tr("在「设置 → 关于」里能看到版本号"));
    m_appVersionEdit->setText(AppVersion::current());
    m_appVersionEdit->setMaxLength(kAppVersionMaxLength);
    formLayout->addWidget(m_appVersionField);

    // 运行平台（必填下拉）
    m_platformField = new QWidget(formCard);
    m_platformField->setAttribute(Qt::WA_StyledBackground, false);
    {
        auto *platformLayout = new QVBoxLayout(m_platformField);
        platformLayout->setContentsMargins(0, 0, 0, 0);
        platformLayout->setSpacing(4);
        platformLayout->addWidget(ForumKit::makeMuted(m_platformField, tr("运行平台 *")));
        m_platformCombo = new QComboBox(m_platformField);
        m_platformCombo->setObjectName(QStringLiteral("forumField"));
        m_platformCombo->addItem(tr("Windows（x64）"));
        m_platformCombo->addItem(tr("Windows（ARM64）"));
        m_platformCombo->addItem(tr("Android"));
        m_platformCombo->addItem(tr("鸿蒙 HarmonyOS"));
        m_platformCombo->addItem(tr("其他（请在问题描述里说明）"));
        m_platformCombo->setFixedWidth(240);
        platformLayout->addWidget(m_platformCombo);
    }
    formLayout->addWidget(m_platformField);

    // 系统版本（选填）
    m_osVersionField = makeInputBlock(formCard, tr("系统版本（可选）"), &m_osVersionEdit,
                                      tr("例如 Windows 11 23H2 / Android 14"));
    m_osVersionEdit->setMaxLength(kOsVersionMaxLength);
    formLayout->addWidget(m_osVersionField);

    // 问题描述（必填）
    {
        QLabel *title = nullptr;
        QWidget *descField = makeFieldBlock(formCard, &title, tr("问题描述 *"), &m_descEdit,
                                            tr("说说发生了什么，涉及哪个页面、哪个实例，越具体越好。"), 90);
        m_descTitle = title;
        formLayout->addWidget(descField);
    }

    // 复现步骤（必填，与 GitHub 模板一样预填编号）
    m_stepsField = makeFieldBlock(formCard, nullptr, tr("复现步骤 *"), &m_stepsEdit,
                                  QString(), 90,
                                  tr("按顺序写下你点过哪些地方，方便照着再试一遍"));
    m_stepsEdit->setPlainText(QStringLiteral("1. \n2. \n3. \n"));
    formLayout->addWidget(m_stepsField);

    // 期望结果（选填）
    m_expectedField = makeFieldBlock(formCard, nullptr, tr("期望结果（可选）"), &m_expectedEdit,
                                     tr("例如：安装完成后自动打开新实例。"), 60);
    formLayout->addWidget(m_expectedField);

    // 实际结果（选填）
    m_actualField = makeFieldBlock(formCard, nullptr, tr("实际结果（可选）"), &m_actualEdit,
                                   tr("如果有报错弹窗，把提示文字原样贴进来。"), 60);
    formLayout->addWidget(m_actualField);

    // 日志（选填）：标题行右侧带「一键上传日志」，导出 + 上传一次完成
    {
        auto *logsBlock = new QWidget(formCard);
        logsBlock->setAttribute(Qt::WA_StyledBackground, false);
        auto *logsLayout = new QVBoxLayout(logsBlock);
        logsLayout->setContentsMargins(0, 0, 0, 0);
        logsLayout->setSpacing(4);
        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(10);
        titleRow->addWidget(ForumKit::makeMuted(logsBlock, tr("日志（可选）")));
        titleRow->addStretch(1);
        m_uploadLogButton = new QPushButton(tr("一键上传日志"), logsBlock);
        m_uploadLogButton->setObjectName(QStringLiteral("forumGhostButton"));
        m_uploadLogButton->setCursor(Qt::PointingHandCursor);
        connect(m_uploadLogButton, &QPushButton::clicked, this, &FeedbackComposePage::uploadLog);
        titleRow->addWidget(m_uploadLogButton);
        logsLayout->addLayout(titleRow);
        m_logsEdit = new QPlainTextEdit(logsBlock);
        m_logsEdit->setObjectName(QStringLiteral("forumReplyEdit"));
        m_logsEdit->setPlaceholderText(
            tr("点「一键上传日志」自动附上完整运行日志；也可以手动粘贴报错内容，很长时贴最后几十行。"));
        m_logsEdit->setMinimumHeight(80);
        logsLayout->addWidget(m_logsEdit);
        // 提前说清日志会脱敏，用户才敢点「一键上传日志」
        QLabel *logPrivacyHint = ForumKit::makeMuted(
            logsBlock, tr("上传前会自动隐藏日志里的用户名、邮箱、IP 和登录令牌等隐私信息。"));
        logPrivacyHint->setWordWrap(true);
        logsLayout->addWidget(logPrivacyHint);
        m_logsField = logsBlock;
        formLayout->addWidget(logsBlock);
    }

    // 截图 / 录屏（选填）
    m_screenshotsField = makeFieldBlock(formCard, nullptr, tr("截图 / 录屏（可选）"), &m_screenshotsEdit,
                                        tr("能直接说明问题的截图或录屏，可以贴图片链接或文字说明。"), 60);
    formLayout->addWidget(m_screenshotsField);

    // ── 功能建议字段 ──
    m_sceneField = makeFieldBlock(formCard, nullptr, tr("使用场景（可选）"), &m_sceneEdit,
                                  tr("什么时候会用到、能解决什么问题"), 80);
    formLayout->addWidget(m_sceneField);

    mainLayout->addWidget(formCard);

    // 提交行
    auto *submitCard = ForumKit::makeCard(m_mainHost);
    auto *submitLayout = new QHBoxLayout(submitCard);
    submitLayout->setContentsMargins(20, 12, 20, 12);
    submitLayout->setSpacing(10);
    m_statusLabel = new QLabel(submitCard);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    submitLayout->addWidget(m_statusLabel, 1);
    m_submitButton = new QPushButton(tr("提交反馈"), submitCard);
    m_submitButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_submitButton->setCursor(Qt::PointingHandCursor);
    m_submitButton->setFixedHeight(34);
    m_submitButton->setMinimumWidth(110);
    connect(m_submitButton, &QPushButton::clicked, this, &FeedbackComposePage::submit);
    submitLayout->addWidget(m_submitButton);
    mainLayout->addWidget(submitCard);
    mainLayout->addStretch(1);

    layout->addWidget(m_loginHost);
    layout->addWidget(m_mainHost);

    scroll->setWidget(content);
    outer->addWidget(scroll);

    applyType();
}

void FeedbackComposePage::open()
{
    updateState();
}

void FeedbackComposePage::updateState()
{
    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    m_mainHost->setVisible(loggedIn);
    m_loginHost->setVisible(!loggedIn);
}

void FeedbackComposePage::applyType()
{
    const bool isBug = m_typeCombo->currentIndex() == 0;
    m_descTitle->setText(isBug ? tr("问题描述 *") : tr("建议内容 *"));
    m_descEdit->setPlaceholderText(isBug
                                       ? tr("说说发生了什么，涉及哪个页面、哪个实例，越具体越好。")
                                       : tr("想要什么功能，一句话说清。"));
    m_appVersionField->setVisible(isBug);
    m_platformField->setVisible(isBug);
    m_osVersionField->setVisible(isBug);
    m_stepsField->setVisible(isBug);
    m_expectedField->setVisible(isBug);
    m_actualField->setVisible(isBug);
    m_logsField->setVisible(isBug);
    m_screenshotsField->setVisible(isBug);
    m_sceneField->setVisible(!isBug);
}

/** 一键上传运行日志：导出到临时文件 → 传到论坛 → 把链接填进日志栏 */
void FeedbackComposePage::uploadLog()
{
    if (!m_uploadLogButton->isEnabled())
        return;

    // 没记过日志（设置里关了运行日志 / 日志文件还不存在）就先说明白，别传个空壳上去
    if (!QFileInfo::exists(LauncherLogger::logFilePath()) || LauncherLogger::logSize() <= 0)
    {
        NotificationManager::showInfo(
            this, tr("还没有可上传的运行日志：可能已在「设置 → 高级设置」里关闭了日志记录"));
        return;
    }

    // 先导出（带上版本、系统信息的导出头），再整份上传
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                            + QStringLiteral("/BlockBoxFeedback");
    QDir().mkpath(tempDir);
    const QString exportPath = tempDir + QStringLiteral("/launcher-log-%1.log")
                                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString exportError;
    if (!LauncherLogger::exportLog(exportPath, &exportError))
    {
        NotificationManager::showInfo(this, tr("日志导出失败：%1").arg(exportError));
        return;
    }

    m_uploadLogButton->setEnabled(false);
    setStatus(tr("正在上传日志…"));

    FeedbackService::instance()->uploadLogFile(
        exportPath,
        [this, exportPath](const QString &url, const QString &, qint64 size) {
            QFile::remove(exportPath);
            m_uploadLogButton->setEnabled(true);
            setStatus(QString());
            const QString line = tr("运行日志已上传（%1）：%2")
                                     .arg(logSizeLabel(size),
                                          ForumAccountService::instance()->absoluteUrl(url));
            const QString existing = m_logsEdit->toPlainText();
            m_logsEdit->setPlainText(existing.isEmpty() ? line : existing + QLatin1Char('\n') + line);
            NotificationManager::showInfo(this, tr("日志已上传，链接已填进日志栏"));
        },
        [this, exportPath](const QString &message, int) {
            QFile::remove(exportPath);
            m_uploadLogButton->setEnabled(true);
            setStatus(QString());
            NotificationManager::showInfo(this, tr("日志上传失败：%1").arg(message));
        });
}

void FeedbackComposePage::submit()
{
    const bool isBug = m_typeCombo->currentIndex() == 0;

    FeedbackDraft draft;
    draft.type = isBug ? QStringLiteral("bug") : QStringLiteral("feature");
    draft.description = m_descEdit->toPlainText().trimmed();

    if (draft.description.size() < kDescMinLength)
    {
        NotificationManager::showInfo(this, isBug
                                                ? tr("问题描述至少要 %1 个字").arg(kDescMinLength)
                                                : tr("建议内容至少要 %1 个字").arg(kDescMinLength));
        m_descEdit->setFocus();
        return;
    }
    if (draft.description.size() > kDescMaxLength)
    {
        NotificationManager::showInfo(this, tr("描述最多 %1 个字").arg(kDescMaxLength));
        return;
    }

    if (isBug)
    {
        draft.appVersion = m_appVersionEdit->text().trimmed();
        if (draft.appVersion.isEmpty())
        {
            NotificationManager::showInfo(this, tr("启动器版本不能为空：在「设置 → 关于」里能看到"));
            m_appVersionEdit->setFocus();
            return;
        }
        draft.platform = m_platformCombo->currentText();
        draft.osVersion = m_osVersionEdit->text().trimmed();
        draft.steps = m_stepsEdit->toPlainText().trimmed();
        if (draft.steps.isEmpty())
        {
            NotificationManager::showInfo(this, tr("复现步骤不能为空：按顺序写下你点过哪些地方"));
            m_stepsEdit->setFocus();
            return;
        }
        if (draft.steps.size() > kStepsMaxLength)
        {
            NotificationManager::showInfo(this, tr("复现步骤最多 %1 个字").arg(kStepsMaxLength));
            return;
        }
        draft.expected = m_expectedEdit->toPlainText().trimmed();
        draft.actual = m_actualEdit->toPlainText().trimmed();
        draft.logs = m_logsEdit->toPlainText().trimmed();
        draft.screenshots = m_screenshotsEdit->toPlainText().trimmed();
        if (draft.expected.size() > kExpectedMaxLength)
        {
            NotificationManager::showInfo(this, tr("期望结果最多 %1 个字").arg(kExpectedMaxLength));
            return;
        }
        if (draft.actual.size() > kActualMaxLength)
        {
            NotificationManager::showInfo(this, tr("实际结果最多 %1 个字").arg(kActualMaxLength));
            return;
        }
        if (draft.logs.size() > kLogsMaxLength)
        {
            NotificationManager::showInfo(this, tr("日志最多 %1 个字").arg(kLogsMaxLength));
            return;
        }
        if (draft.screenshots.size() > kScreenshotsMaxLength)
        {
            NotificationManager::showInfo(this, tr("截图 / 录屏最多 %1 个字").arg(kScreenshotsMaxLength));
            return;
        }
    }
    else
    {
        draft.scene = m_sceneEdit->toPlainText().trimmed();
        if (draft.scene.size() > kSceneMaxLength)
        {
            NotificationManager::showInfo(this, tr("使用场景最多 %1 个字").arg(kSceneMaxLength));
            return;
        }
    }

    m_submitButton->setEnabled(false);
    setStatus(tr("正在提交…"));

    FeedbackService::instance()->submitFeedback(
        draft,
        [this](int) {
            m_submitButton->setEnabled(true);
            NotificationManager::showInfo(this, tr("反馈已提交，感谢你的反馈！处理进度会在反馈列表里更新。"));
            emit submitted();
        },
        [this](const QString &message, int) {
            m_submitButton->setEnabled(true);
            setStatus(message, true);
        });
}

void FeedbackComposePage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
