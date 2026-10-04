/**
 * @file   InstanceSelectDialog.cpp
 * @brief  实例选择弹窗实现
 * @author BlockBox Team
 * @date   2026-08-24
 */
#include "InstanceSelectDialog.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QFrame>

#include "utils/SettingsManager.h"

InstanceSelectDialog::InstanceSelectDialog(QWidget *parent, const QString &defaultPath)
    : AppDialogBase(parent)
    , m_listWidget(nullptr)
    , m_confirmBtn(nullptr)
    , m_cancelBtn(nullptr)
    , m_hintLabel(nullptr)
    , m_defaultPath(defaultPath)
{
    initUI();
    loadInstances();
}

QString InstanceSelectDialog::selectInstance(QWidget *parent, const QString &defaultInstancePath)
{
    InstanceSelectDialog dialog(parent, defaultInstancePath);
    if (dialog.exec() == QDialog::Accepted)
        return dialog.m_selectedPath;
    return QString();
}

void InstanceSelectDialog::initUI()
{
    // 居中卡片（使用全局 QSS 样式）
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setFixedSize(480, 520);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 20);
    cardLayout->setSpacing(16);

    // 标题
    QLabel *titleLabel = new QLabel(tr("选择目标实例"), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    cardLayout->addWidget(titleLabel);

    // 提示
    m_hintLabel = new QLabel(tr("将文件导入到以下实例："), card);
    m_hintLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    cardLayout->addWidget(m_hintLabel);

    // 实例列表
    m_listWidget = new QListWidget(card);
    m_listWidget->setObjectName(QStringLiteral("instanceListWidget"));
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    cardLayout->addWidget(m_listWidget, 1);

    // 按钮栏
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(10);
    btnLayout->addStretch();

    m_cancelBtn = new QPushButton(tr("取消"), card);
    m_cancelBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_cancelBtn->setFixedSize(80, 34);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelBtn);

    m_confirmBtn = new QPushButton(tr("确认导入"), card);
    m_confirmBtn->setObjectName(QStringLiteral("appDialogPrimaryBtn"));
    m_confirmBtn->setFixedSize(100, 34);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setEnabled(false);
    connect(m_confirmBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(m_confirmBtn);

    cardLayout->addLayout(btnLayout);

    // 列表选中变化 → 启用/禁用确认按钮
    connect(m_listWidget, &QListWidget::itemSelectionChanged, this, [this]() {
        m_confirmBtn->setEnabled(!m_listWidget->selectedItems().isEmpty());
    });

    // 双击直接确认
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this]() {
        if (!m_listWidget->selectedItems().isEmpty())
            accept();
    });
}

void InstanceSelectDialog::loadInstances()
{
    QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    int defaultRow = -1;

    for (const InstanceFolderInfo &folder : folders)
    {
        QDir versionsDir(folder.path + "/versions");
        if (!versionsDir.exists())
            continue;

        QDirIterator it(versionsDir.path(), QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::NoIteratorFlags);
        while (it.hasNext())
        {
            QString instancePath = it.next();
            QFileInfo instanceInfo(instancePath);
            QString instanceName = instanceInfo.fileName();

            if (instanceName.startsWith("."))
                continue;

            QFile versionJsonFile(instancePath + "/" + instanceName + ".json");
            if (!versionJsonFile.exists())
                continue;

            // 读取版本和加载器信息
            QString gameVersion;
            QString loaderType;
            if (versionJsonFile.open(QIODevice::ReadOnly))
            {
                QByteArray jsonData = versionJsonFile.readAll();
                QJsonParseError parseError;
                QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);
                if (parseError.error == QJsonParseError::NoError)
                {
                    QJsonObject rootObj = jsonDoc.object();
                    if (rootObj.contains("inheritsFrom"))
                        gameVersion = rootObj["inheritsFrom"].toString();
                    else if (rootObj.contains("clientVersion"))
                        gameVersion = rootObj["clientVersion"].toString();
                    else if (rootObj.contains("id"))
                        gameVersion = rootObj["id"].toString();

                    auto detectLoader = [](const QString &text) -> QString {
                        QString lower = text.toLower();
                        if (lower.contains("neoforge")) return "NeoForge";
                        if (lower.contains("forge")) return "Forge";
                        if (lower.contains("fabric")) return "Fabric";
                        if (lower.contains("quilt")) return "Quilt";
                        return QString();
                    };
                    for (auto it2 = rootObj.constBegin(); it2 != rootObj.constEnd(); ++it2) {
                        loaderType = detectLoader(it2.key());
                        if (!loaderType.isEmpty()) break;
                        if (it2.value().isString()) {
                            loaderType = detectLoader(it2.value().toString());
                            if (!loaderType.isEmpty()) break;
                        }
                    }
                }
                versionJsonFile.close();
            }

            // 构建显示文本
            QString displayText = instanceName;
            if (!loaderType.isEmpty() || !gameVersion.isEmpty())
            {
                QStringList parts;
                if (!loaderType.isEmpty()) parts << loaderType;
                if (!gameVersion.isEmpty() && gameVersion != instanceName) parts << gameVersion;
                displayText += "  (" + parts.join(" · ") + ")";
            }

            QListWidgetItem *item = new QListWidgetItem(displayText, m_listWidget);
            item->setData(Qt::UserRole, instancePath);
            item->setSizeHint(QSize(0, 44));

            // 默认选中
            if (!m_defaultPath.isEmpty() && instancePath == m_defaultPath)
            {
                defaultRow = m_listWidget->count() - 1;
            }
        }
    }

    if (defaultRow >= 0)
    {
        m_listWidget->setCurrentRow(defaultRow);
    }

    // 空列表提示
    if (m_listWidget->count() == 0)
    {
        m_hintLabel->setText(tr("没有找到已安装的实例，请先安装一个实例。"));
        m_confirmBtn->setEnabled(false);
    }
}
