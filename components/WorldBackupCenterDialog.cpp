/**
 * @file   WorldBackupCenterDialog.cpp
 * @brief  实例级「存档备份」总览弹窗实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "WorldBackupCenterDialog.h"

#include "AppMessageBox.h"
#include "WorldBackupDialog.h"
#include "utils/FileExplorer.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"

#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

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

void WorldBackupCenterDialog::openForInstance(QWidget *parent, const QString &instancePath)
{
    if (instancePath.isEmpty())
        return;
    WorldBackupCenterDialog dialog(parent, instancePath);
    dialog.exec();
}

WorldBackupCenterDialog::WorldBackupCenterDialog(QWidget *parent, const QString &instancePath)
    : AppDialogBase(parent)
    , m_instancePath(instancePath)
{
    initUI();
    reload();
}

qint64 WorldBackupCenterDialog::backupsSize(const QList<WorldBackupInfo> &backups)
{
    qint64 total = 0;
    for (const WorldBackupInfo &info : backups)
        total += info.archiveSize;
    return total;
}

void WorldBackupCenterDialog::initUI()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addStretch(1);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumSize(UiMetrics::fitToScreen(this, QSize(620, 480)));
    card->setMaximumWidth(760);
    rootLayout->addWidget(card, 0, Qt::AlignHCenter);
    rootLayout->addStretch(1);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 20);
    cardLayout->setSpacing(10);

    QLabel *titleLabel = new QLabel(tr("存档备份"), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    cardLayout->addWidget(titleLabel);

    m_summaryLabel = new QLabel(card);
    m_summaryLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    m_summaryLabel->setWordWrap(true);
    cardLayout->addWidget(m_summaryLabel);

    QLabel *hintLabel = new QLabel(
        tr("每个存档的备份单独存放；存档被误删后，这里仍能把它备份的版本恢复回来。"),
        card);
    hintLabel->setWordWrap(true);
    hintLabel->setObjectName(QStringLiteral("backupHintLabel"));
    cardLayout->addWidget(hintLabel);

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

    card->setStyleSheet(QStringLiteral(
        "QScrollArea#backupScroll { background: transparent; border: none; }"
        "QWidget#backupListHost { background: transparent; }"
        "QFrame#backupRow { background-color: rgba(128,128,128,0.14);"
        "  border: 1px solid rgba(128,128,128,0.24); border-radius: 10px; }"
        "QFrame#backupRow:hover { background-color: rgba(128,128,128,0.22); }"
        "QLabel#backupEmptyLabel { color: rgba(128,128,128,0.9); border: none;"
        "  background: transparent; font-size: 13px; }"
        "QLabel#backupHintLabel { color: rgba(150,90,0,0.95); border: none;"
        "  background: rgba(255,193,7,0.12); border-radius: 8px;"
        "  padding: 6px 10px; font-size: 12px; }"));

    m_emptyLabel = new QLabel(tr("这个实例还没有任何存档备份。\n"
                                 "在存档卡片上点「备份与恢复」即可创建第一份，"
                                 "也可以在「设置 > 全局游戏设置 > 存档备份」里开启启动前自动备份。"),
                              listHost);
    m_emptyLabel->setObjectName(QStringLiteral("backupEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_listLayout->insertWidget(0, m_emptyLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(10);
    btnLayout->addStretch();

    m_closeBtn = new QPushButton(tr("关闭"), card);
    m_closeBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_closeBtn);
    cardLayout->addLayout(btnLayout);
}

void WorldBackupCenterDialog::reload()
{
    // 清掉旧行（保留首部空态提示与尾部 stretch）
    while (m_listLayout->count() > 2) {
        QLayoutItem *item = m_listLayout->takeAt(1);
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }

    const QStringList worlds =
        WorldBackupManager::instance()->listBackupWorldNames(m_instancePath);
    const QStringList existingWorlds = WorldBackupManager::listWorldNames(m_instancePath);

    int totalCount = 0;
    qint64 totalSize = 0;
    for (const QString &worldName : worlds) {
        const QList<WorldBackupInfo> backups =
            WorldBackupManager::instance()->listBackups(m_instancePath, worldName);
        totalCount += backups.size();
        totalSize += backupsSize(backups);
        m_listLayout->insertWidget(m_listLayout->count() - 1,
                                   createWorldRow(worldName, backups,
                                                  existingWorlds.contains(worldName)));
    }

    m_emptyLabel->setVisible(worlds.isEmpty());
    m_summaryLabel->setText(tr("实例：%1 · %2 个存档有备份 · 共 %3 份 · 占用 %4")
                                .arg(QDir(m_instancePath).dirName(),
                                     QString::number(worlds.size()),
                                     QString::number(totalCount),
                                     formatSize(totalSize)));
}

QWidget *WorldBackupCenterDialog::createWorldRow(const QString &worldName,
                                                 const QList<WorldBackupInfo> &backups,
                                                 bool worldExists)
{
    QFrame *row = new QFrame(this);
    row->setObjectName(QStringLiteral("backupRow"));

    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(10);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(3);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(6);

    QLabel *nameLabel = new QLabel(worldName, row);
    QFont boldFont = nameLabel->font();
    boldFont.setBold(true);
    nameLabel->setFont(boldFont);
    titleLayout->addWidget(nameLabel);

    if (!worldExists) {
        QLabel *deletedChip = new QLabel(tr("存档已删除"), row);
        deletedChip->setStyleSheet(QStringLiteral(
            "color: white; background-color: #E53935; border-radius: 9px;"
            "padding: 2px 8px; font-size: 11px;"));
        titleLayout->addWidget(deletedChip);
    }
    titleLayout->addStretch();
    infoLayout->addLayout(titleLayout);

    const QDateTime latest = backups.isEmpty() ? QDateTime() : backups.first().createdAt;
    QLabel *detailLabel = new QLabel(
        tr("%1 份备份 · 占用 %2 · 最新 %3")
            .arg(QString::number(backups.size()),
                 formatSize(backupsSize(backups)),
                 latest.isValid() ? latest.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                  : tr("—")),
        row);
    detailLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    infoLayout->addWidget(detailLabel);

    layout->addLayout(infoLayout, 1);

    QPushButton *manageBtn = new QPushButton(worldExists ? tr("管理备份") : tr("恢复备份"), row);
    manageBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    manageBtn->setCursor(Qt::PointingHandCursor);
    connect(manageBtn, &QPushButton::clicked, this, [this, worldName]() {
        WorldBackupDialog::openForWorld(this, m_instancePath, worldName);
        reload(); // 单世界弹窗里可能增删了备份
    });
    layout->addWidget(manageBtn);

    QPushButton *folderBtn = new QPushButton(tr("文件夹"), row);
    folderBtn->setObjectName(QStringLiteral("appDialogBtn"));
    folderBtn->setCursor(Qt::PointingHandCursor);
    folderBtn->setToolTip(tr("打开这个存档的备份文件夹"));
    connect(folderBtn, &QPushButton::clicked, this, [this, worldName]() {
        const QString dir = WorldBackupManager::worldBackupDir(m_instancePath, worldName);
        QDir().mkpath(dir);
        FileExplorer::open(this, dir);
    });
    layout->addWidget(folderBtn);

    return row;
}
