/**
 * @file   ResourceFilterDialog.h
 * @brief  在线资源列表页的「单列筛选」弹窗
 * @author BlockBox Team
 * @date   2026-11-20
 *
 * 资源页的「投影」「插件」两个列表没有 Modrinth / CurseForge 那样的
 * 多来源分类，筛选维度只有一列（投影看文件格式，插件看安装状态）。
 * 这里复用在线资源筛选弹窗同一套卡片 / 列表 / 按钮样式，保证观感一致。
 */
#pragma once

#include <QPair>
#include <QString>
#include <QVector>

#include "AppDialogBase.h"

class QLabel;
class QListWidget;
class QPushButton;

class ResourceFilterDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @param title       弹窗标题（如「投影筛选」）
     * @param hint        顶部说明文字
     * @param columnTitle 列表上方的小标题（如「文件格式」）
     */
    ResourceFilterDialog(const QString &title, const QString &hint,
                         const QString &columnTitle, QWidget *parent = nullptr);

    /** 选项：first 为标识（空字符串表示「全部」），second 为展示文字 */
    void setOptions(const QVector<QPair<QString, QString>> &options);

    /** 打开前恢复上次的选中项 */
    void setSelectedId(const QString &id);

    /** 当前选中的标识（空字符串表示「全部」） */
    QString selectedId() const;

signals:
    /** 点了「确定」且选中项有变化时发出 */
    void filterApplied(const QString &id);

private:
    void initUI();
    void populateList();

    QString m_title;
    QString m_hint;
    QString m_columnTitle;
    QVector<QPair<QString, QString>> m_options;
    QString m_selectedId;

    QLabel *m_columnTitleLabel = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_resetBtn = nullptr;
};
