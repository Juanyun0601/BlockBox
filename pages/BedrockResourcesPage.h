/**
 * @file   BedrockResourcesPage.h
 * @brief  基岩版实例助手 - 资源管理页面，管理当前实例的存档/资源包/行为包/皮肤包
 * @author BlockBox Team
 * @date   2026-08-25
 *
 * 用于 BedrockInstanceAssistantWindow 的「资源管理」标签页。窗口宽度较窄（450px），
 * 采用单列垂直卡片列表，顶部通过 Tab 切换资源类型：
 *  - 存档：扫描 minecraftWorlds/ 目录下的子目录
 *  - 资源包：扫描 resource_packs/ 目录下的子目录
 *  - 行为包：扫描 behavior_packs/ 目录下的子目录
 *  - 皮肤包：扫描 skin_packs/ 目录下的子目录
 *
 * 支持启用/禁用（重命名目录前缀）、删除、搜索、全选等操作。
 */
#ifndef BEDROCKRESOURCESPAGE_H
#define BEDROCKRESOURCESPAGE_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QEvent;
class QResizeEvent;
class QShowEvent;

class BedrockResourcesPage : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int highlightPos READ highlightPos WRITE setHighlightPos)

public:
    explicit BedrockResourcesPage(QWidget *parent = nullptr);
    ~BedrockResourcesPage() override;

    /** 设置当前实例 com.mojang 路径并刷新当前类型的资源列表 */
    void setInstancePath(const QString &path);

    /** 手动刷新当前类型的资源列表 */
    void refreshList();

    int highlightPos() const { return m_highlightPos; }
    void setHighlightPos(int pos);

signals:
    void navItemClicked(int childIndex);

private slots:
    void onSearchTextChanged(const QString &text);
    void onSelectAllClicked();
    void onOpenFolderClicked();
    void onDeleteClicked();

private:
    /** 资源类型 */
    enum ResourceType
    {
        TypeWorlds = 0,       // 存档
        TypeResourcePacks = 1, // 资源包
        TypeBehaviorPacks = 2, // 行为包
        TypeSkinPacks = 3      // 皮肤包
    };

    /** 文件型资源条目 */
    struct FileEntry
    {
        QString filePath;
        QString fileName;
        qint64 fileSize = 0;
        QDateTime lastModified;
        bool isDir = false;
        bool isEnabled = true;  // 目录名不含 "[disabled]" 前缀

        bool operator==(const FileEntry &other) const
        {
            return filePath == other.filePath;
        }
    };

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void initUI();
    void setupConnections();
    QString resolveResourceDir() const;
    QString typeDisplayName() const;

    QPushButton *createTabButton(const QString &text, int index);
    void applyTabThemeStyles();
    void switchTab(int index);
    void updateHighlightGeometry();

    void clearCards();
    void scanFiles();
    void showEmptyHint(bool show);
    void applyFilterAndSearch();
    QString formatFileSize(qint64 bytes) const;
    void toggleCardSelection(int idx);
    void deselectAllCards();
    void updateBottomBarState();
    void updateStatsLabel();
    QWidget *createFileCard(const FileEntry &entry);

    // ---- UI ----
    QPushButton *m_tabWorldsBtn;
    QPushButton *m_tabResourcePacksBtn;
    QPushButton *m_tabBehaviorPacksBtn;
    QPushButton *m_tabSkinPacksBtn;
    QWidget *m_tabBar;
    QWidget *m_tabHighlight;
    QPropertyAnimation *m_tabAnim;
    QList<QPushButton *> m_tabButtons;
    int m_highlightPos;
    QLineEdit *m_searchEdit;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QVBoxLayout *m_cardLayout;
    QLabel *m_emptyLabel;

    // 底部操作栏
    QWidget *m_bottomBar;
    QPushButton *m_selectAllBtn;
    QPushButton *m_openFolderBtn;
    QPushButton *m_deleteBtn;

    // ---- 数据 ----
    ResourceType m_currentType;
    QString m_instancePath;  // com.mojang 目录路径
    QString m_currentSearch;
    bool m_allSelected;

    QList<FileEntry> m_fileList;
    QList<QWidget *> m_cardWidgets;
    QSet<int> m_selectedIndices;
};

#endif // BEDROCKRESOURCESPAGE_H
