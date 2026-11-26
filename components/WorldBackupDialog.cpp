/**
 * @file   WorldBackupDialog.cpp
 * @brief  世界备份与恢复弹窗实现
 * @author BlockBox Team
 * @date   2026-08-29
 */
#include "WorldBackupDialog.h"

#include "AppMessageBox.h"
#include "utils/FileExplorer.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

QString formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
}

} // namespace

void WorldBackupDialog::openForWorld(QWidget *parent, const QString &instancePath,
                                     const QString &worldName)
{
    if (instancePath.isEmpty() || worldName.isEmpty())
        return;
    WorldBackupDialog dialog(parent, instancePath, worldName);
    dialog.exec();
}

WorldBackupDialog::WorldBackupDialog(QWidget *parent, const QString &instancePath,
                                     const QString &worldName)
    : AppDialogBase(parent)
    , m_instancePath(instancePath)
    , m_worldName(worldName)
{
    initUI();
    reloadBackups();
    refreshWorldSize();

    // 备份进度与结果（管理器为全局单例，信号按队列投递到 UI 线程）
    connect(WorldBackupManager::instance(), &WorldBackupManager::backupProgress, this,
            [this](int percent, const QString &message) {
                if (m_statusLabel && !message.isEmpty()) {
                    if (percent >= 0)
                        m_statusLabel->setText(message + QString(" (%1%)").arg(percent));
                    else
                        m_statusLabel->setText(message);
                }
            });
    connect(WorldBackupManager::instance(), &WorldBackupManager::backupFinished, this,
            [this](const QString &instancePath, const QString &worldName, bool ok,
                   const QString &error, bool automatic) {
                if (instancePath != m_instancePath || worldName != m_worldName)
                    return;
                // 自动备份（如启动前备份）不打断当前弹窗的忙碌状态提示
                if (!automatic) {
                    setBusy(false);
                    reloadBackups();
                }
                if (ok) {
                    if (!automatic)
                        m_statusLabel->setText(tr("备份完成"));
                } else if (!automatic) {
                    m_statusLabel->clear();
                    AppMessageBox::warning(this, tr("备份失败"),
                                           error.isEmpty() ? tr("备份时发生未知错误") : error);
                }
            });
    connect(WorldBackupManager::instance(), &WorldBackupManager::restoreFinished, this,
            [this](const QString &instancePath, const QString &worldName, bool ok,
                   const QString &error) {
                if (instancePath != m_instancePath || worldName != m_worldName)
                    return;
                setBusy(false);
                reloadBackups();
                refreshWorldSize();
                if (ok) {
                    m_statusLabel->setText(tr("恢复完成"));
                    AppMessageBox::information(this, tr("恢复完成"),
                                               tr("世界已恢复到所选备份点。"));
                } else {
                    m_statusLabel->clear();
                    AppMessageBox::warning(this, tr("恢复失败"),
                                           error.isEmpty() ? tr("恢复时发生未知错误") : error);
                }
            });
}

void WorldBackupDialog::initUI()
{
    // 居中卡片（全局 QSS 提供卡片外观，行条目使用本弹窗的内联样式）
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addStretch(1);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumSize(UiMetrics::fitToScreen(this, QSize(620, 520)));
    card->setMaximumWidth(760);
    rootLayout->addWidget(card, 0, Qt::AlignHCenter);
    rootLayout->addStretch(1);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 20);
    cardLayout->setSpacing(10);

    // 标题
    QLabel *titleLabel = new QLabel(tr("世界备份与恢复"), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    cardLayout->addWidget(titleLabel);

    // 摘要：世界名 · 当前大小 · 备份数 · 占用空间
    m_summaryLabel = new QLabel(card);
    m_summaryLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    cardLayout->addWidget(m_summaryLabel);

    // 使用提示
    m_hintLabel = new QLabel(
        tr("备份可以直接创建；恢复会覆盖当前世界，请先退出正在运行的游戏。"), card);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setObjectName(QStringLiteral("backupHintLabel"));
    cardLayout->addWidget(m_hintLabel);

    // 备份列表
    m_scrollArea = new QScrollArea(card);
    m_scrollArea->setObjectName(QStringLiteral("backupScroll"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget *listHost = new QWidget;
    listHost->setObjectName(QStringLiteral("backupListHost"));
    m_listLayout = new QVBoxLayout(listHost);
    m_listLayout->setContentsMargins(0, 0, 6, 0);
    m_listLayout->setSpacing(8);
    m_listLayout->addStretch(1);
    m_scrollArea->setWidget(listHost);
    cardLayout->addWidget(m_scrollArea, 1);

    // 列表内联样式（挂在卡片上，覆盖列表行、空态提示、设置行与状态文字）
    card->setStyleSheet(QStringLiteral(
        "QScrollArea#backupScroll { background: transparent; border: none; }"
        "QWidget#backupListHost { background: transparent; }"
        "QFrame#backupRow { background-color: rgba(128,128,128,0.14);"
        "  border: 1px solid rgba(128,128,128,0.24); border-radius: 10px; }"
        "QFrame#backupRow:hover { background-color: rgba(128,128,128,0.22); }"
        "QLabel#backupEmptyLabel { color: rgba(128,128,128,0.9); border: none;"
        "  background: transparent; font-size: 13px; }"
        "QLabel#backupStatusLabel { color: rgba(128,128,128,0.95); border: none;"
        "  background: transparent; font-size: 12px; }"
        "QLabel#backupHintLabel { color: rgba(150,90,0,0.95); border: none;"
        "  background: rgba(255,193,7,0.12); border-radius: 8px;"
        "  padding: 6px 10px; font-size: 12px; }"));

    m_emptyLabel = new QLabel(tr("还没有备份。点右下角「立即备份」创建第一份。"), listHost);
    m_emptyLabel->setObjectName(QStringLiteral("backupEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_listLayout->insertWidget(0, m_emptyLabel);

    // 状态行（备份/恢复进度与结果提示）
    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("backupStatusLabel"));
    cardLayout->addWidget(m_statusLabel);

    // 自动备份设置行
    QFrame *settingsRow = new QFrame(card);
    settingsRow->setObjectName(QStringLiteral("backupRow"));
    QHBoxLayout *settingsLayout = new QHBoxLayout(settingsRow);
    settingsLayout->setContentsMargins(12, 8, 12, 8);
    settingsLayout->setSpacing(8);

    m_autoCheck = new QCheckBox(tr("启动游戏前自动备份"), settingsRow);
    m_autoCheck->setChecked(WorldBackupManager::autoBackupEnabled());
    m_autoCheck->setToolTip(tr("只备份上次备份之后有改动的世界"));
    settingsLayout->addWidget(m_autoCheck);

    m_freqCombo = new QComboBox(settingsRow);
    m_freqCombo->addItem(tr("每次启动"));
    m_freqCombo->addItem(tr("每小时"));
    m_freqCombo->addItem(tr("每天"));
    m_freqCombo->setCurrentIndex(qBound(0, WorldBackupManager::autoBackupFrequency(),
                                        m_freqCombo->count() - 1));
    m_freqCombo->setToolTip(tr("自动备份的频率，频率不到时启动不会重复备份"));
    settingsLayout->addWidget(m_freqCombo);

    settingsLayout->addStretch();

    QLabel *keepLabel = new QLabel(tr("保留份数"), settingsRow);
    settingsLayout->addWidget(keepLabel);
    m_keepSpin = new QSpinBox(settingsRow);
    m_keepSpin->setRange(1, 100);
    m_keepSpin->setValue(WorldBackupManager::keepCount());
    m_keepSpin->setToolTip(tr("超过保留份数时自动删除最旧的备份（手动创建的也按时间一并清理）"));
    settingsLayout->addWidget(m_keepSpin);

    cardLayout->addWidget(settingsRow);

    // 恢复前快照选项
    m_snapshotCheck = new QCheckBox(tr("恢复前先为当前世界创建一份快照"), card);
    m_snapshotCheck->setChecked(WorldBackupManager::snapshotBeforeRestore());
    m_snapshotCheck->setToolTip(tr("勾选后恢复前会自动备份当前进度，恢复错了还能再退回来"));
    cardLayout->addWidget(m_snapshotCheck);

    // 备注行（创建备份时可顺手写一句）
    QHBoxLayout *noteLayout = new QHBoxLayout();
    noteLayout->setContentsMargins(0, 0, 0, 0);
    noteLayout->setSpacing(8);
    QLabel *noteLabel = new QLabel(tr("备注"), card);
    noteLayout->addWidget(noteLabel);
    m_noteEdit = new QLineEdit(card);
    m_noteEdit->setPlaceholderText(tr("可选，例如：打末影龙之前"));
    m_noteEdit->setMaxLength(60);
    noteLayout->addWidget(m_noteEdit, 1);
    cardLayout->addLayout(noteLayout);

    // 底部按钮栏
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(10);

    m_folderBtn = new QPushButton(tr("打开备份文件夹"), card);
    m_folderBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_folderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_folderBtn, &QPushButton::clicked, this, &WorldBackupDialog::openBackupFolder);
    btnLayout->addWidget(m_folderBtn);

    btnLayout->addStretch();

    m_createBtn = new QPushButton(tr("立即备份"), card);
    m_createBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    m_createBtn->setCursor(Qt::PointingHandCursor);
    m_createBtn->setToolTip(tr("把这个世界当前的样子压缩保存一份"));
    connect(m_createBtn, &QPushButton::clicked, this, &WorldBackupDialog::createBackup);
    btnLayout->addWidget(m_createBtn);

    m_closeBtn = new QPushButton(tr("关闭"), card);
    m_closeBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_closeBtn);

    cardLayout->addLayout(btnLayout);

    // 设置项变化 → 立即保存
    connect(m_autoCheck, &QCheckBox::toggled, this, [this](bool checked) {
        updateAutoControlsEnabled();
        Q_UNUSED(checked);
        saveAutoSettings();
    });
    connect(m_freqCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &WorldBackupDialog::saveAutoSettings);
    connect(m_keepSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &WorldBackupDialog::saveAutoSettings);
    connect(m_snapshotCheck, &QCheckBox::toggled, this, [](bool checked) {
        WorldBackupManager::setSnapshotBeforeRestore(checked);
    });

    updateAutoControlsEnabled();
}

void WorldBackupDialog::updateAutoControlsEnabled()
{
    const bool enabled = m_autoCheck->isChecked();
    m_freqCombo->setEnabled(enabled);
    m_keepSpin->setEnabled(enabled);
}

void WorldBackupDialog::reloadBackups()
{
    // 清空旧行（保留布局首部的空态提示与尾部的 stretch），同时清理按钮引用
    m_backups = WorldBackupManager::instance()->listBackups(m_instancePath, m_worldName);
    m_busyButtons.clear();
    while (m_listLayout->count() > 2) {
        QLayoutItem *item = m_listLayout->takeAt(1);
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }

    m_backupTotal = 0;
    for (const WorldBackupInfo &info : m_backups)
        m_backupTotal += info.archiveSize;

    m_emptyLabel->setVisible(m_backups.isEmpty());
    for (int i = 0; i < m_backups.size(); ++i) {
        m_listLayout->insertWidget(m_listLayout->count() - 1, createBackupRow(m_backups[i]));
    }
    refreshSummary();
}

void WorldBackupDialog::refreshWorldSize()
{
    const int token = ++m_sizeToken;
    m_worldSize = -1;
    refreshSummary();

    const QString worldDir = WorldBackupManager::savesDirForInstance(m_instancePath)
                             + "/" + m_worldName;
    if (!QDir(worldDir).exists()) {
        m_worldSize = 0;
        refreshSummary();
        return;
    }

    // 大存档递归统计可能几十万个文件，放后台线程算，弹窗先显示「统计中」
    auto *watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, &QFutureWatcher<qint64>::finished, this, [this, watcher, token]() {
        watcher->deleteLater();
        if (token != m_sizeToken)
            return; // 统计期间又换了世界/重开了弹窗，丢弃旧结果
        m_worldSize = watcher->result();
        refreshSummary();
    });
    watcher->setFuture(QtConcurrent::run([worldDir]() {
        return WorldBackupManager::directorySize(worldDir);
    }));
}

void WorldBackupDialog::refreshSummary()
{
    const QString sizeText = m_worldSize < 0 ? tr("统计中…") : formatSize(m_worldSize);
    m_summaryLabel->setText(tr("世界：%1 · 当前大小：%2 · 备份 %3 份 · 占用 %4")
                                .arg(m_worldName, sizeText,
                                     QString::number(m_backups.size()),
                                     formatSize(m_backupTotal)));
}

QWidget *WorldBackupDialog::createBackupRow(const WorldBackupInfo &info)
{
    QFrame *row = new QFrame(this);
    row->setObjectName(QStringLiteral("backupRow"));

    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(10);

    // 左侧：时间 + 自动/手动标记 + 大小与备注
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(3);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(6);

    QLabel *timeLabel = new QLabel(info.createdAt.toString("yyyy-MM-dd HH:mm:ss"), row);
    QFont boldFont = timeLabel->font();
    boldFont.setBold(true);
    timeLabel->setFont(boldFont);
    titleLayout->addWidget(timeLabel);

    QLabel *typeChip = new QLabel(info.automatic ? tr("自动") : tr("手动"), row);
    typeChip->setObjectName(QStringLiteral("modChip"));
    const QColor themeColor(ThemeManager::instance()->currentThemeColor());
    typeChip->setStyleSheet(QString("color: white; background-color: %1; border-radius: 9px;"
                                    "padding: 2px 8px; font-size: 11px;")
                                .arg(themeColor.name()));
    titleLayout->addWidget(typeChip);
    titleLayout->addStretch();
    infoLayout->addLayout(titleLayout);

    QStringList details;
    details << tr("原始大小 %1").arg(formatSize(info.sourceSize));
    details << tr("压缩后 %1").arg(formatSize(info.archiveSize));
    if (!info.gameVersion.isEmpty())
        details << info.gameVersion;
    QLabel *detailLabel = new QLabel(details.join(QStringLiteral(" · ")), row);
    detailLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    infoLayout->addWidget(detailLabel);

    if (!info.note.isEmpty()) {
        QLabel *noteLabel = new QLabel(info.note, row);
        noteLabel->setObjectName(QStringLiteral("backupStatusLabel"));
        noteLabel->setWordWrap(true);
        infoLayout->addWidget(noteLabel);
    }

    layout->addLayout(infoLayout, 1);

    // 右侧操作按钮
    QPushButton *restoreBtn = new QPushButton(tr("恢复"), row);
    restoreBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    restoreBtn->setCursor(Qt::PointingHandCursor);
    restoreBtn->setToolTip(tr("把这个备份解压回去，覆盖当前世界"));
    connect(restoreBtn, &QPushButton::clicked, this, [this, info]() { restoreBackup(info); });
    layout->addWidget(restoreBtn);
    m_busyButtons << restoreBtn;

    QPushButton *deleteBtn = new QPushButton(tr("删除"), row);
    deleteBtn->setObjectName(QStringLiteral("appDialogBtn"));
    deleteBtn->setCursor(Qt::PointingHandCursor);
    connect(deleteBtn, &QPushButton::clicked, this, [this, info]() { deleteBackup(info); });
    layout->addWidget(deleteBtn);
    m_busyButtons << deleteBtn;

    return row;
}

void WorldBackupDialog::createBackup()
{
    if (!WorldBackupManager::listWorldNames(m_instancePath).contains(m_worldName)) {
        AppMessageBox::warning(this, tr("无法创建备份"),
                              tr("未找到世界目录「%1」，它可能已经被删除。")
                                  .arg(m_worldName));
        return;
    }
    const QString note = m_noteEdit->text().trimmed();
    m_noteEdit->clear();
    setBusy(true, tr("正在创建备份..."));
    WorldBackupManager::instance()->createBackupAsync(m_instancePath, m_worldName, note, false);
}

void WorldBackupDialog::restoreBackup(const WorldBackupInfo &info)
{
    const QString timeText = info.createdAt.toString("yyyy-MM-dd HH:mm:ss");
    const QString text = tr("确定要将世界「%1」恢复到 %2 的备份吗？\n\n"
                            "恢复会覆盖当前世界，%3")
                            .arg(m_worldName, timeText,
                                 WorldBackupManager::snapshotBeforeRestore()
                                     ? tr("当前世界会先自动备份为快照。")
                                     : tr("当前世界的进度将丢失！"));
    if (AppMessageBox::question(this, tr("恢复世界"), text) != AppMessageBox::Yes)
        return;

    setBusy(true, tr("正在恢复..."));
    WorldBackupManager::instance()->restoreBackupAsync(m_instancePath, m_worldName, info.id);
}

void WorldBackupDialog::deleteBackup(const WorldBackupInfo &info)
{
    const QString timeText = info.createdAt.toString("yyyy-MM-dd HH:mm:ss");
    const QString text = tr("确定要删除 %1 的备份吗？\n删除后无法恢复。").arg(timeText);
    if (AppMessageBox::question(this, tr("删除备份"), text) != AppMessageBox::Yes)
        return;

    QString error;
    if (!WorldBackupManager::instance()->deleteBackupSync(m_instancePath, m_worldName,
                                                          info.id, &error)) {
        AppMessageBox::warning(this, tr("删除失败"), error);
        return;
    }
    reloadBackups();
    m_statusLabel->setText(tr("已删除 %1 的备份").arg(timeText));
}

void WorldBackupDialog::openBackupFolder()
{
    QDir().mkpath(WorldBackupManager::worldBackupDir(m_instancePath, m_worldName));
    FileExplorer::open(this, WorldBackupManager::worldBackupDir(m_instancePath, m_worldName));
}

void WorldBackupDialog::setBusy(bool busy, const QString &message)
{
    m_createBtn->setEnabled(!busy);
    m_closeBtn->setEnabled(!busy);
    m_folderBtn->setEnabled(!busy);
    m_noteEdit->setEnabled(!busy);
    m_autoCheck->setEnabled(!busy);
    m_snapshotCheck->setEnabled(!busy);
    m_freqCombo->setEnabled(!busy && m_autoCheck->isChecked());
    m_keepSpin->setEnabled(!busy && m_autoCheck->isChecked());
    for (QPushButton *btn : m_busyButtons)
        btn->setEnabled(!busy);
    if (busy && !message.isEmpty())
        m_statusLabel->setText(message);
    if (!busy)
        m_statusLabel->clear();
}

void WorldBackupDialog::saveAutoSettings()
{
    WorldBackupManager::setAutoBackupEnabled(m_autoCheck->isChecked());
    WorldBackupManager::setAutoBackupFrequency(m_freqCombo->currentIndex());
    WorldBackupManager::setKeepCount(m_keepSpin->value());
}
