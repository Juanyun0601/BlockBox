/**
 * @file   ModSettingsDialog.cpp
 * @brief  本地模组设置对话框实现
 * @author BlockBox Team
 * @date   2026-08-29
 */

#include "ModSettingsDialog.h"

#include <QVBoxLayout>

#include "../pages/LocalModSettingsPage.h"

ModSettingsDialog::ModSettingsDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_settingsPage(nullptr)
{
    setObjectName(QStringLiteral("modSettingsDialog"));
    setWindowTitle(tr("模组设置"));

    // 复用 appDialogCard 样式;卡片需容纳完整的设置页表单
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setFixedSize(820, 640);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 4, 4, 4);
    cardLayout->setSpacing(0);

    m_settingsPage = new LocalModSettingsPage(card);
    cardLayout->addWidget(m_settingsPage);

    connect(m_settingsPage, &LocalModSettingsPage::modSettingsChanged,
            this, &ModSettingsDialog::modSettingsChanged);
}

ModSettingsDialog::~ModSettingsDialog()
{
}

void ModSettingsDialog::setMod(const ModInfo &info, const QString &instancePath)
{
    m_settingsPage->setCurrentMod(info, instancePath);
}
