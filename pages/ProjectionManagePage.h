#ifndef PROJECTIONMANAGEPAGE_H
#define PROJECTIONMANAGEPAGE_H

#include <QWidget>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QList>
#include <QFileInfo>

class ProjectionManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectionManagePage(QWidget *parent = nullptr);
    ~ProjectionManagePage();

    void setInstancePath(const QString &path);

signals:
    /** 请求编辑投影文件（点击卡片上的编辑按钮或双击卡片） */
    void projectionEditRequested(const QString &filePath);

private slots:
    void onSearchTextChanged(const QString &text);
    void onAddProjectionClicked();
    void onDeleteProjectionClicked();
    void onBlockEditorClicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();
    void loadProjections();
    void clearCards();
    void placeCards();
    void showEmptyHint(bool show);
    QString projectionsDirPath() const;
    void deleteProjectionFile(const QString &filePath);
    void updateNativeEditorButton();

    QLineEdit *m_searchEdit;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    QLabel *m_emptyLabel;

    QWidget *m_bottomBar;
    QPushButton *m_openFolderBtn;
    QPushButton *m_addProjectionBtn;
    QPushButton *m_deleteProjectionBtn;
    QPushButton *m_blockEditorBtn;

    QList<QFileInfo> m_projectionFiles;
    QString m_currentSearch;
    QString m_instancePath;
};

#endif
