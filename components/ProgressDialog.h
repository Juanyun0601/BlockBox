/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Ported from MultiMC launcher/ui/dialogs/ProgressDialog (Apache-2.0).
// BlockBox adaptation: the dialog is built in code instead of a .ui file
// (same widgets: statusLabel / taskProgressBar / skipButton).

#pragma once

#include <QDialog>
#include <memory>

class Task;
class QLabel;
class QProgressBar;
class QPushButton;

class ProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProgressDialog(QWidget *parent = 0);
    ~ProgressDialog();

    void updateSize();

    int execWithTask(Task *task);

    /// BlockBox adaptation: non-modal variant — shows the dialog and returns
    /// immediately so the rest of the app stays usable while the task runs;
    /// the dialog closes itself when the task finishes.
    void showWithTask(Task *task);

    void setSkipButton(bool present, QString label = QString());

    Task *getTask();

public
slots:
    void onTaskStarted();
    void onTaskFailed(QString failure);
    void onTaskSucceeded();

    void changeStatus(const QString &status);
    void changeProgress(qint64 current, qint64 total);

private
slots:
    void on_skipButton_clicked(bool checked);

protected:
    virtual void keyPressEvent(QKeyEvent *e);
    virtual void closeEvent(QCloseEvent *e);

private:
    bool handleImmediateResult(QDialog::DialogCode &result);

private:
    QLabel *m_statusLabel;
    QProgressBar *m_taskProgressBar;
    QPushButton *m_skipButton;

    Task *task;
};
