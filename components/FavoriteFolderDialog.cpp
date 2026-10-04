/**
 * @file   FavoriteFolderDialog.cpp
 * @brief  新建/重命名收藏夹对话框实现
 * @author BlockBox Team
 * @date   2026-07-18
 */
#include "FavoriteFolderDialog.h"

#include "../layouts/FlowLayout.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

FavoriteFolderDialog::FavoriteFolderDialog(Mode mode, QWidget *parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_nameEdit(nullptr)
    , m_confirmBtn(nullptr)
    , m_presetsContainer(nullptr)
{
    initUI();
}

void FavoriteFolderDialog::initUI()
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setAttribute(Qt::WA_DeleteOnClose, false);

    QString title = (m_mode == CreateMode) ? tr("新建收藏夹") : tr("重命名收藏夹");
    setWindowTitle(title);
    setFixedWidth(360);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(14);

    QLabel *titleLabel = new QLabel(title, this);
    layout->addWidget(titleLabel);

    QLabel *hintLabel = new QLabel(tr("请输入收藏夹名称："), this);
    layout->addWidget(hintLabel);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("例如：建筑模组、优化模组..."));
    m_nameEdit->setMaxLength(40);
    layout->addWidget(m_nameEdit);

    // 名称预设容器（点击即填入，仅新建分类时显示）
    m_presetsContainer = new QWidget(this);
    m_presetsContainer->hide();
    layout->addWidget(m_presetsContainer);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    QPushButton *cancelBtn = new QPushButton(tr("取消"), this);
    cancelBtn->setObjectName("cancelButton");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    m_confirmBtn = new QPushButton(tr("确定"), this);
    m_confirmBtn->setEnabled(false);
    connect(m_confirmBtn, &QPushButton::clicked, this, &FavoriteFolderDialog::onConfirm);
    btnLayout->addWidget(m_confirmBtn);

    layout->addLayout(btnLayout);

    connect(m_nameEdit, &QLineEdit::textChanged, this, &FavoriteFolderDialog::onNameChanged);
}

void FavoriteFolderDialog::setInitialName(const QString &name)
{
    m_nameEdit->setText(name);
    m_nameEdit->selectAll();
    m_nameEdit->setFocus();
}

void FavoriteFolderDialog::setPresets(const QStringList &presets)
{
    if (!m_presetsContainer)
    {
        return;
    }

    // 清空旧的预设
    QLayout *oldLayout = m_presetsContainer->layout();
    if (oldLayout)
    {
        QLayoutItem *item;
        while ((item = oldLayout->takeAt(0)) != nullptr)
        {
            if (QWidget *w = item->widget())
            {
                w->deleteLater();
            }
            delete item;
        }
        delete oldLayout;
    }

    if (presets.isEmpty())
    {
        m_presetsContainer->hide();
        return;
    }

    // 可点击的预设胶囊按钮，点击即填入名称
    FlowLayout *flow = new FlowLayout(m_presetsContainer, 0, 6, 6);
    for (const QString &name : presets)
    {
        QPushButton *btn = new QPushButton(name, m_presetsContainer);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, name]() {
            m_nameEdit->setText(name);
            m_nameEdit->setFocus();
        });
        flow->addWidget(btn);
    }
    m_presetsContainer->show();
}

QString FavoriteFolderDialog::folderName() const
{
    return m_nameEdit->text().trimmed();
}

void FavoriteFolderDialog::onNameChanged(const QString &text)
{
    m_confirmBtn->setEnabled(!text.trimmed().isEmpty());
}

void FavoriteFolderDialog::onConfirm()
{
    if (folderName().isEmpty())
    {
        return;
    }
    accept();
}
