#ifndef NOTIFICATIONHISTORYDIALOG_H
#define NOTIFICATIONHISTORYDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QListWidget>
#include <QPushButton>

#include "NotificationCard.h"
#include "NotificationManager.h"

class NotificationHistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NotificationHistoryDialog(QWidget *parent = nullptr);
    ~NotificationHistoryDialog() override;

private slots:
    void onFilterChanged(int index);
    void onClearClicked();

private:
    void initUI();
    void loadHistory();

    QComboBox *m_filterCombo;
    QListWidget *m_listWidget;
    QPushButton *m_clearButton;
    QPushButton *m_closeButton;
    QList<NotificationRecord> m_allRecords;
};

#endif
