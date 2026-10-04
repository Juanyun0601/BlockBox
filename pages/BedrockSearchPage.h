#ifndef BEDROCKSEARCHPAGE_H
#define BEDROCKSEARCHPAGE_H

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include <QList>

#include "utils/bedrock/BedrockInstanceManager.h"

/**
 * @brief 基岩版搜索结果条目
 */
struct BedrockSearchResult {
    QString title;
    QString subtitle;
    QString description;
    QString iconText;

    enum Kind { Instance, Pack } kind = Instance;

    QString instanceId;  // 所属基岩版实例（实例/附加包通用）
    QString packPath;    // 附加包绝对路径（仅 Pack 使用）
};

/**
 * @brief 基岩版搜索页 — 独立于 Java 版搜索页
 *
 * 仅检索基岩版相关内容：
 *   - 基岩版实例（BedrockInstanceManager）
 *   - 各实例 com.mojang 下的资源包 / 行为包 / 皮肤包 / 地图
 * 点击实例进入基岩版实例选择页；点击附加包打开其所在文件夹。
 */
class BedrockSearchPage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockSearchPage(QWidget *parent = nullptr);

    void setSearchFocus();
    void clearSearch();

signals:
    void navigateToBedrockInstance(const QString &instanceId);
    void openPackFolder(const QString &packPath);

private:
    void initUI();
    QWidget *createSearchBar();
    QWidget *createFilterBar();
    QWidget *createSubFilterBar();
    QWidget *createResultsArea();
    void onFilterChanged(int index);
    void onSubFilterChanged(int index);
    void performSearch(const QString &keyword);
    QList<BedrockSearchResult> searchInstances(const QString &keyword);
    QList<BedrockSearchResult> searchPacks(const QString &keyword, int packKind);
    void showResults(const QList<BedrockSearchResult> &results);
    void clearResults();
    void onResultClicked(int index);
    bool eventFilter(QObject *obj, QEvent *event) override;

    QLineEdit *m_searchInput = nullptr;
    QComboBox *m_filterCombo = nullptr;

    QComboBox *m_subFilterCombo = nullptr;

    QScrollArea *m_resultsScroll = nullptr;
    QWidget *m_resultsContainer = nullptr;
    QVBoxLayout *m_resultsLayout = nullptr;
    QLabel *m_resultsTitle = nullptr;
    QLabel *m_noResultsLabel = nullptr;

    QList<BedrockSearchResult> m_currentResults;
    int m_currentFilter = 0;
    int m_currentSubFilter = 0;
};

#endif // BEDROCKSEARCHPAGE_H
