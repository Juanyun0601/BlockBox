#ifndef MODLISTPAGE_H
#define MODLISTPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include "../utils/mod/ModScanner.h"
#include "../utils/mod/ModData.h"

class BlurLoadingOverlay;

class ModListPage : public QWidget
{
    Q_OBJECT

public:
    explicit ModListPage(QWidget *parent = nullptr);
    ~ModListPage();

    void loadInstances();

private slots:
    void onInstanceChanged(int index);
    void onRefreshClicked();
    void onFilterChanged(int index);
    void onScanCompleted(const LocalModList &result);
    void onScanFailed(const QString &error);
    void onToggleMod(const ModInfo &info);
    void onDeleteMod(const ModInfo &info);
    void onModContextMenu(const QPoint &pos);

private:
    void initUI();
    void setupConnections();
    void refreshModList();
    void clearCards();
    void placeCards();
    void showLoading(bool show);
    void showEmptyHint(bool show);
    void applyFilter();

    enum FilterMode { FilterAll = 0, FilterEnabled = 1, FilterDisabled = 2 };

    // UI elements
    QComboBox *m_instanceCombo;
    QComboBox *m_filterCombo;
    QPushButton *m_refreshBtn;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    BlurLoadingOverlay *m_loadingOverlay;
    QLabel *m_emptyLabel;

    // Data
    ModScanner *m_scanner;
    LocalModList m_modList;
    QList<QWidget*> m_cardWidgets;
    int m_currentFilter;
    bool m_isLoading;
};

#endif // MODLISTPAGE_H