/**
 * @file   SkillManagerDialog.h
 * @brief  AI 技能管理对话框声明
 * @author BlockBox Team
 * @date   2026-08-08
 *
 * 从 AI 助手页面入口打开，提供技能的查看 / 启用禁用 / 导入 / 删除 / 信任确认。
 * 继承 AppDialogBase 获得模糊背景与居中卡片样式。
 */
#ifndef SKILLMANAGERDIALOG_H
#define SKILLMANAGERDIALOG_H

#include <QDialog>
#include <QList>

#include "components/AppDialogBase.h"
#include "utils/skill/SkillInfo.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

class SkillManagerDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit SkillManagerDialog(QWidget *parent = nullptr);

private slots:
    void onImportSkill();
    void onRefresh();
    void onToggleEnabled(const QString &id, bool enabled);
    void onRemoveSkill(const QString &id);
    void onViewDetail(const QString &id);

private:
    void initUI();
    void rebuildList();
    QWidget *createSkillCard(const SkillInfo &info);

    QWidget *m_card;              // 居中卡片容器
    QLabel *m_titleLabel;
    QLabel *m_countLabel;
    QScrollArea *m_scrollArea;
    QVBoxLayout *m_listLayout;
    QPushButton *m_importBtn;
    QPushButton *m_refreshBtn;
    QPushButton *m_closeBtn;

    QList<SkillInfo> m_skills;
};

#endif // SKILLMANAGERDIALOG_H
