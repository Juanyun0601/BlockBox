#ifndef CONTENTLISTPAGE_H
#define CONTENTLISTPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include "../utils/content/ContentData.h"

class BlurLoadingOverlay;

class ContentListPage : public QWidget
{
    Q_OBJECT

public:
    explicit ContentListPage(ContentType contentType, QWidget *parent = nullptr);
    ~ContentListPage();

    void loadInstances();

signals:
    void contentClicked(const ModInfo &info);

private slots:
    void onInstanceChanged(int index);
    void onRefreshClicked();
    void onFilterChanged(int index);
    void onToggleMod(const ModInfo &info);
    void onDeleteMod(const ModInfo &info);
    void onContentContextMenu(const QPoint &pos);

private:
    void initUI();
    void setupConnections();
    void refreshContentList();
    void doScan(const QString &folderPath);
    void onScanFinished(const LocalModList &result);
    void clearCards();
    void placeCards();
    void showLoading(bool show);
    void showEmptyHint(bool show);
    void applyFilter();

    bool eventFilter(QObject *obj, QEvent *event) override;

    enum FilterMode { FilterAll = 0, FilterEnabled = 1, FilterDisabled = 2 };

    // Content type
    ContentType m_contentType;
    ContentTypeConfig m_config;

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
    LocalModList m_modList;
    QList<QWidget*> m_cardWidgets;
    int m_currentFilter;
    bool m_isLoading;
};

#endif // CONTENTLISTPAGE_H