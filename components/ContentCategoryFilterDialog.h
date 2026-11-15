#ifndef CONTENTCATEGORYFILTERDIALOG_H
#define CONTENTCATEGORYFILTERDIALOG_H

#include "AppDialogBase.h"

#include "utils/mod/ModData.h"

#include <QString>
#include <QListWidget>

class QLabel;
class QPushButton;

/**
 * @brief 在线资源分类筛选弹窗：居中卡片，左右两栏（Modrinth 分类 / CurseForge 分类）
 *
 * 每栏单选一个分类，首项「全部」= 不限；两栏互相独立，可只筛一边。
 * 点击已选中的分类可取消（回到「全部」）。
 * 分类数据异步加载：弹窗可以先打开，加载完成后由页面回填。
 */
class ContentCategoryFilterDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @param modrinthSupported Modrinth 不提供的内容类型（如世界存档）隐藏该栏
     */
    explicit ContentCategoryFilterDialog(bool modrinthSupported, QWidget *parent = nullptr);

    /** 各栏当前选中的分类 id（空 = 不限） */
    QString selectedModrinthCategory() const;
    QString selectedCurseforgeCategory() const;

    /** 打开前预设两栏的选中项（空 = 全部） */
    void setSelection(const QString &modrinthId, const QString &curseforgeId);

    /** 某一栏还在请求中：显示加载占位（数据到位后由页面调用 set*Categories 回填） */
    void setModrinthLoading();
    void setCurseforgeLoading();

    /** 填充某一栏的分类列表（重复调用会整栏重建） */
    void setModrinthCategories(const QList<ModCategory> &categories);
    void setCurseforgeCategories(const QList<ModCategory> &categories);

    /** 某一栏加载失败：显示失败占位 */
    void setModrinthFailed();
    void setCurseforgeFailed();

signals:
    /** 点击确定且筛选有变化时发出；两个 id 均可为空（= 不限） */
    void filterApplied(const QString &modrinthId, const QString &curseforgeId);

private:
    void initUI();
    QWidget *buildColumn(const QString &title, QListWidget **outList);
    /** 填充单栏列表：首项「全部」+ 分类项，并恢复选中态 */
    void populateList(QListWidget *list, const QList<ModCategory> &categories);
    /** 列表显示占位行（加载中 / 失败 / 空），不可点选 */
    void setPlaceholder(QListWidget *list, const QString &text);
    /** 点击已选中的分类 = 取消筛选，回到「全部」 */
    void handleItemClicked(QListWidget *list, QListWidgetItem *item);
    /** 读取列表当前选中项的分类 id（空 = 不限） */
    static QString selectedIdOf(QListWidget *list);

    bool m_modrinthSupported;
    QString m_selectedModrinthId;
    QString m_selectedCurseforgeId;

    QWidget *m_modrinthColumn = nullptr;
    QWidget *m_curseforgeColumn = nullptr;
    QListWidget *m_modrinthList = nullptr;
    QListWidget *m_curseforgeList = nullptr;
    QPushButton *m_resetBtn = nullptr;
    QPushButton *m_okBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
};

#endif // CONTENTCATEGORYFILTERDIALOG_H
