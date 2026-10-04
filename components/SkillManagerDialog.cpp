/**
 * @file   SkillManagerDialog.cpp
 * @brief  AI 技能管理对话框实现
 * @author BlockBox Team
 * @date   2026-08-08
 */

#include "SkillManagerDialog.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "components/AppFileDialog.h"
#include "components/AppMessageBox.h"
#include "utils/plugin/PluginSafetyGuard.h"
#include "utils/skill/SkillManager.h"

SkillManagerDialog::SkillManagerDialog(QWidget *parent)
    : AppDialogBase(parent)
{
    initUI();
    rebuildList();

    connect(SkillManager::instance(), &SkillManager::skillsChanged, this, [this]() {
        rebuildList();
    });
}

void SkillManagerDialog::initUI()
{
    setWindowTitle(tr("技能管理"));
    setModal(true);

    // 居中卡片（objectName 以 Card 结尾，供 AppDialogBase 定位）
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("skillManagerCard"));
    m_card->setFixedSize(640, 520);
    outer->addWidget(m_card, 0, Qt::AlignCenter);

    QVBoxLayout *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(24, 24, 24, 20);
    cardLayout->setSpacing(12);

    // 标题行
    QHBoxLayout *titleRow = new QHBoxLayout();
    m_titleLabel = new QLabel(tr("AI 技能管理"), m_card);
    m_titleLabel->setObjectName(QStringLiteral("skillDialogTitle"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_countLabel = new QLabel(m_card);
    m_countLabel->setObjectName(QStringLiteral("skillCountLabel"));
    titleRow->addWidget(m_titleLabel);
    titleRow->addStretch();
    titleRow->addWidget(m_countLabel);
    cardLayout->addLayout(titleRow);

    // 说明
    QLabel *descLabel = new QLabel(
        tr("技能是扩展 AI 助手能力的工具包。启用后，AI 在工作模式下可自动调用技能中的工具。"),
        m_card);
    descLabel->setObjectName(QStringLiteral("skillDescLabel"));
    descLabel->setWordWrap(true);
    cardLayout->addWidget(descLabel);

    // 滚动列表
    m_scrollArea = new QScrollArea(m_card);
    m_scrollArea->setObjectName(QStringLiteral("skillScrollArea"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *listContainer = new QWidget(m_scrollArea);
    m_listLayout = new QVBoxLayout(listContainer);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(8);
    m_listLayout->addStretch();
    m_scrollArea->setWidget(listContainer);
    cardLayout->addWidget(m_scrollArea, 1);

    // 底部按钮行
    QHBoxLayout *btnRow = new QHBoxLayout();
    m_importBtn = new QPushButton(tr("导入技能"), m_card);
    m_importBtn->setObjectName(QStringLiteral("skillImportBtn"));
    m_importBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn = new QPushButton(tr("刷新"), m_card);
    m_refreshBtn->setObjectName(QStringLiteral("skillRefreshBtn"));
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn = new QPushButton(tr("关闭"), m_card);
    m_closeBtn->setObjectName(QStringLiteral("skillCloseBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);

    btnRow->addWidget(m_importBtn);
    btnRow->addWidget(m_refreshBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_closeBtn);
    cardLayout->addLayout(btnRow);

    connect(m_importBtn, &QPushButton::clicked, this, &SkillManagerDialog::onImportSkill);
    connect(m_refreshBtn, &QPushButton::clicked, this, &SkillManagerDialog::onRefresh);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void SkillManagerDialog::rebuildList()
{
    m_skills = SkillManager::instance()->skills();

    // 清空旧卡片（保留尾部 stretch）
    while (m_listLayout->count() > 1)
    {
        QLayoutItem *item = m_listLayout->takeAt(0);
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    int validCount = 0;
    for (const SkillInfo &info : m_skills)
    {
        if (!info.isValid())
            continue;
        m_listLayout->insertWidget(m_listLayout->count() - 1, createSkillCard(info));
        ++validCount;
    }

    // 空状态
    if (validCount == 0)
    {
        QLabel *empty = new QLabel(tr("暂无技能。点击「导入技能」加载 .skill 文件。"), m_card);
        empty->setObjectName(QStringLiteral("skillEmptyLabel"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QStringLiteral("color: #888; padding: 40px;"));
        m_listLayout->insertWidget(m_listLayout->count() - 1, empty);
    }

    m_countLabel->setText(tr("共 %1 个技能").arg(validCount));
}

QWidget *SkillManagerDialog::createSkillCard(const SkillInfo &info)
{
    QFrame *card = new QFrame(m_card);
    card->setObjectName(QStringLiteral("skillItemCard"));
    card->setFrameShape(QFrame::NoFrame);
    card->setStyleSheet(QStringLiteral(
        "#skillItemCard { background: rgba(255,255,255,0.05); border: 1px solid "
        "rgba(255,255,255,0.1); border-radius: 8px; }"
        "#skillItemCard:hover { border-color: rgba(255,255,255,0.2); }"
        "QLabel { background: transparent; }"));

    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);

    // 顶部行：名称 + 启用开关
    QHBoxLayout *topRow = new QHBoxLayout();
    QLabel *nameLabel = new QLabel(info.name, card);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(11);
    nameLabel->setFont(nameFont);

    QCheckBox *enableCheck = new QCheckBox(tr("启用"), card);
    enableCheck->setCursor(Qt::PointingHandCursor);
    enableCheck->setChecked(SkillManager::instance()->isEnabled(info.id));
    connect(enableCheck, &QCheckBox::toggled, this,
            [this, info](bool checked) { onToggleEnabled(info.id, checked); });

    topRow->addWidget(nameLabel);
    topRow->addStretch();
    topRow->addWidget(enableCheck);
    layout->addLayout(topRow);

    // 描述
    if (!info.description.isEmpty())
    {
        QLabel *descLabel = new QLabel(info.description, card);
        descLabel->setWordWrap(true);
        descLabel->setStyleSheet(QStringLiteral("color: #aaa;"));
        layout->addWidget(descLabel);
    }

    // 元信息行：版本 / 作者 / 工具数 / 信任状态
    QHBoxLayout *metaRow = new QHBoxLayout();
    QLabel *metaLabel = new QLabel(
        tr("v%1  ·  %2  ·  %3 个工具").arg(info.version, info.author.isEmpty() ? tr("未知作者") : info.author).arg(info.tools.size()),
        card);
    metaLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));

    bool trusted = SkillManager::instance()->isTrusted(info.id);
    QLabel *trustLabel = new QLabel(trusted ? tr("✓ 已信任") : tr("⚠ 未信任"), card);
    trustLabel->setStyleSheet(
        trusted ? QStringLiteral("color: #4CAF50; font-size: 11px;")
                : QStringLiteral("color: #FF9800; font-size: 11px;"));

    metaRow->addWidget(metaLabel);
    metaRow->addStretch();
    metaRow->addWidget(trustLabel);
    layout->addLayout(metaRow);

    // 工具列表
    if (!info.tools.isEmpty())
    {
        QStringList toolNames;
        for (const SkillTool &t : info.tools)
            toolNames.append(t.name);
        QLabel *toolsLabel = new QLabel(tr("工具：") + toolNames.join(QStringLiteral(", ")), card);
        toolsLabel->setStyleSheet(QStringLiteral("color: #bbb; font-size: 11px;"));
        toolsLabel->setWordWrap(true);
        layout->addWidget(toolsLabel);
    }

    // 操作按钮行
    QHBoxLayout *actionRow = new QHBoxLayout();
    QPushButton *detailBtn = new QPushButton(tr("查看详情"), card);
    detailBtn->setCursor(Qt::PointingHandCursor);
    detailBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid rgba(255,255,255,0.15); "
        "border-radius: 4px; padding: 3px 10px; color: #ccc; }"
        "QPushButton:hover { border-color: rgba(255,255,255,0.3); }"));
    connect(detailBtn, &QPushButton::clicked, this,
            [this, info]() { onViewDetail(info.id); });

    QPushButton *removeBtn = new QPushButton(tr("删除"), card);
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid rgba(244,67,54,0.3); "
        "border-radius: 4px; padding: 3px 10px; color: #F44336; }"
        "QPushButton:hover { background: rgba(244,67,54,0.1); }"));
    connect(removeBtn, &QPushButton::clicked, this,
            [this, info]() { onRemoveSkill(info.id); });

    actionRow->addWidget(detailBtn);
    actionRow->addStretch();
    actionRow->addWidget(removeBtn);
    layout->addLayout(actionRow);

    return card;
}

void SkillManagerDialog::onImportSkill()
{
    QString filter = tr("技能文件 (*.skill);;所有文件 (*.*)");
    QString file = AppFileDialog::getOpenFileName(this, tr("导入技能"), QString(), filter);
    if (file.isEmpty())
        return;

    SkillManager *sm = SkillManager::instance();
    SkillInfo info = sm->inspectSkillFile(file);
    if (!info.loaded)
    {
        AppMessageBox::warning(this, tr("导入失败"),
                               info.loadError.isEmpty() ? tr("解析 SKILL.md 失败") : info.loadError);
        return;
    }

    // 收集脚本内容用于危险操作扫描
    QString scripts = sm->collectScriptsContent(file);

    // 信任确认（复用插件信任流程）
    using namespace PluginSafetyGuard;
    RiskLevel declared = RiskMedium;
    if (info.permissions.contains(QStringLiteral("system")) ||
        info.permissions.contains(QStringLiteral("admin")))
        declared = RiskHigh;

    if (!confirmBeforeInstall(this, info.name, info.author, info.version,
                              info.permissions, declared, QString(), scripts))
        return; // 用户拒绝

    // 导入
    QString error;
    if (!sm->importSkill(file, &error))
    {
        AppMessageBox::warning(this, tr("导入失败"), error);
        return;
    }

    // 标记信任并默认启用
    sm->setTrusted(info.id, true);
    sm->setEnabled(info.id, true);

    rebuildList();
    AppMessageBox::information(this, tr("导入成功"),
                               tr("技能「%1」已导入、信任并启用。").arg(info.name));
}

void SkillManagerDialog::onRefresh()
{
    SkillManager::instance()->refresh();
    rebuildList();
}

void SkillManagerDialog::onToggleEnabled(const QString &id, bool enabled)
{
    SkillManager::instance()->setEnabled(id, enabled);
}

void SkillManagerDialog::onRemoveSkill(const QString &id)
{
    SkillInfo info = SkillManager::instance()->skillById(id);
    if (!info.isValid())
        return;

    auto btn = AppMessageBox::question(this, tr("删除技能"),
                                       tr("确定删除技能「%1」？此操作不可撤销。").arg(info.name));
    if (btn != AppMessageBox::Yes && btn != AppMessageBox::Ok)
        return;

    QString error;
    if (!SkillManager::instance()->removeSkill(id, &error))
        AppMessageBox::warning(this, tr("删除失败"), error);
}

void SkillManagerDialog::onViewDetail(const QString &id)
{
    SkillInfo info = SkillManager::instance()->skillById(id);
    if (!info.isValid())
        return;

    QStringList lines;
    lines.append(tr("名称：%1").arg(info.name));
    lines.append(tr("版本：%1").arg(info.version));
    lines.append(tr("作者：%1").arg(info.author.isEmpty() ? tr("未知") : info.author));
    lines.append(tr("描述：%1").arg(info.description));
    lines.append(tr("权限：%1").arg(info.permissions.isEmpty() ? tr("无") : info.permissions.join(QStringLiteral(", "))));

    lines.append(QString());
    lines.append(tr("【工具列表】"));
    for (const SkillTool &t : info.tools)
    {
        QString type = t.http.isValid() ? QStringLiteral("HTTP") : t.scriptType().toUpper();
        lines.append(QStringLiteral("  · %1 (%2) — %3").arg(t.name, type, t.description));
    }

    lines.append(QString());
    lines.append(tr("【技能说明】"));
    if (!info.markdownContent.isEmpty())
        lines.append(info.markdownContent);
    else
        lines.append(tr("(无)"));

    // 截断过长内容
    QString detail = lines.join(QLatin1Char('\n'));
    if (detail.size() > 3000)
        detail = detail.left(3000) + QStringLiteral("\n\n...(内容已截断)");

    AppMessageBox::information(this, tr("技能详情"), detail);
}
