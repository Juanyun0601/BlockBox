#ifndef EDITINSTANCEPAGE_H
#define EDITINSTANCEPAGE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QMap>

class EditInstancePage : public QWidget
{
    Q_OBJECT

public:
    explicit EditInstancePage(QWidget *parent = nullptr);
    ~EditInstancePage();

    void setInstancePath(const QString &instancePath);
    void setInstanceName(const QString &name);
    void setGameVersion(const QString &version);
    void setLoaderInfo(const QString &loader);

    QString selectedGameVersion() const { return m_selectedVersion; }
    QString selectedLoaderName() const { return m_selectedLoaderName; }
    QString selectedLoaderVersion() const { return m_selectedLoaderVersion; }

signals:
    void modifyRequested(const QString &instancePath,
                         const QString &newVersion,
                         const QString &loaderName,
                         const QString &loaderVersion);

private:
    void initUI();
    void refreshLoaderCards();
    void updateVersionDisplay();
    QWidget *createLoaderCard(const QString &name, const QString &displayName,
                              const QString &iconColor, bool installed,
                              const QString &installedVersion = QString());

    struct LoaderEntry {
        QString displayName;
        QString iconColor;
        bool installed;
        QString installedVersion;
        QWidget *card;
        QLabel *statusLabel;
        QPushButton *actionBtn;
        QPushButton *removeBtn;
    };

    QString m_instancePath;
    QString m_instanceName;
    QString m_gameVersion;
    QString m_loaderInfo;

    // 暂存用户选择
    QString m_selectedVersion;
    QString m_selectedLoaderName;
    QString m_selectedLoaderVersion;

    // UI
    QLabel *m_titleLabel;
    QLabel *m_hintLabel;
    QLabel *m_instanceNameLabel;
    QLabel *m_currentVersionIcon;
    QLabel *m_currentVersionLabel;
    QLabel *m_currentLoaderLabel;
    QPushButton *m_changeVersionBtn;
    QWidget *m_loaderCardsContainer;
    QVBoxLayout *m_loaderCardsLayout;
    QPushButton *m_startModifyBtn;

    QMap<QString, LoaderEntry> m_loaders;
};

#endif // EDITINSTANCEPAGE_H
