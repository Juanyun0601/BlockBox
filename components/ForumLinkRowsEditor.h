/**
 * @file   ForumLinkRowsEditor.h
 * @brief  相关链接编辑器：发布插件 / 投影时绑定官网、GitHub 仓库、反馈等入口
 * @author BlockBox Team
 * @date   2026-11-15
 *
 * 预设链接点一下就加一行（名称已填好，只补地址），也可以加自定义行随意起名；
 * 同一页（发帖页的「相关链接」卡片、详情页的编辑弹窗）共用这一个控件，
 * 保证两处添加 / 删除 / 校验的行为一致。最多 6 条，地址留空的行会被忽略。
 */
#pragma once

#include <QVector>
#include <QWidget>

#include "utils/ForumContentService.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

class ForumLinkRowsEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ForumLinkRowsEditor(QWidget *parent = nullptr);

    /** 用已有链接铺满编辑器（编辑弹窗回显用），空列表 = 只留预设芯片 */
    void setLinks(const QVector<ForumResourceLink> &links);

    /** 收集当前填写的内容：补全 https:// 前缀、跳过空行，最多 6 条 */
    QVector<ForumResourceLink> links() const;

private:
    struct LinkRow
    {
        QWidget *host = nullptr;
        QLineEdit *labelEdit = nullptr;
        QLineEdit *urlEdit = nullptr;
        QPushButton *removeButton = nullptr;
        QString presetLabel;        ///< 来自预设时的默认名称（自定义行为空）
        QPushButton *presetChip = nullptr;   ///< 对应的预设芯片（自定义行为空）
    };

    void buildChips();
    void addRow(const QString &presetLabel, const QString &placeholder,
                const QString &url = QString(), QPushButton *chip = nullptr);
    /** 按 host 控件定位并删除一行，顺带还原对应预设芯片的勾选状态 */
    void removeRowByHost(QWidget *host);
    void updateChips();
    void refreshHint();

    QWidget *m_chipsHost = nullptr;
    QVector<QPushButton *> m_presetChips;   ///< 与「官网 / GitHub / 反馈」预设一一对应
    QPushButton *m_customChip = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QLabel *m_hintLabel = nullptr;
    QVector<LinkRow> m_rows;
};
