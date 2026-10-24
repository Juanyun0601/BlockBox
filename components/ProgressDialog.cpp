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

// Ported from MultiMC launcher/ui/dialogs/ProgressDialog.cpp (Apache-2.0)

#include "ProgressDialog.h"

#include <QKeyEvent>
#include <QDebug>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QGridLayout>
#include <QCloseEvent>

#include "../utils/net/Task.h"

ProgressDialog::ProgressDialog(QWidget *parent) : QDialog(parent), task(nullptr)
{
    setWindowTitle(tr("请稍候..."));
    setMinimumWidth(400);
    setMaximumWidth(600);

    m_statusLabel = new QLabel(tr("任务状态..."), this);
    m_statusLabel->setWordWrap(true);

    m_taskProgressBar = new QProgressBar(this);
    m_taskProgressBar->setTextVisible(false);

    m_skipButton = new QPushButton(tr("跳过"), this);
    m_skipButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QGridLayout *gridLayout = new QGridLayout(this);
    gridLayout->addWidget(m_statusLabel, 0, 0);
    gridLayout->addWidget(m_taskProgressBar, 1, 0);
    gridLayout->addWidget(m_skipButton, 2, 0);
    setLayout(gridLayout);

    this->setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setSkipButton(false);
    changeProgress(0, 100);
}

void ProgressDialog::setSkipButton(bool present, QString label)
{
    m_skipButton->setAutoDefault(false);
    m_skipButton->setDefault(false);
    m_skipButton->setFocusPolicy(Qt::ClickFocus);
    m_skipButton->setEnabled(present);
    m_skipButton->setVisible(present);
    m_skipButton->setText(label);
    updateSize();
}

void ProgressDialog::on_skipButton_clicked(bool checked)
{
    Q_UNUSED(checked);
    if (task)
        task->abort();
}

ProgressDialog::~ProgressDialog()
{
}

void ProgressDialog::updateSize()
{
    QSize qSize = QSize(480, minimumSizeHint().height());
    resize(qSize);
    setFixedSize(qSize);
}

void ProgressDialog::showWithTask(Task *task)
{
    this->task = task;

    if(!task)
    {
        qDebug() << "Programmer error: progress dialog created with null task.";
        close();
        return;
    }

    // Connect signals.
    connect(task, SIGNAL(started()), SLOT(onTaskStarted()));
    connect(task, SIGNAL(failed(QString)), SLOT(onTaskFailed(QString)));
    connect(task, SIGNAL(succeeded()), SLOT(onTaskSucceeded()));
    connect(task, SIGNAL(status(QString)), SLOT(changeStatus(const QString &)));
    connect(task, SIGNAL(progress(qint64, qint64)), SLOT(changeProgress(qint64, qint64)));

    // if this didn't connect to an already running task, invoke start
    if(!task->isRunning())
    {
        task->start();
    }
    changeProgress(task->getProgress(), task->getTotalProgress());
    changeStatus(task->getStatus());
    show();
    raise();
    activateWindow();
}

int ProgressDialog::execWithTask(Task *task)
{
    this->task = task;
    QDialog::DialogCode result;

    if(!task)
    {
        qDebug() << "Programmer error: progress dialog created with null task.";
        return Accepted;
    }

    if(handleImmediateResult(result))
    {
        return result;
    }

    // Connect signals.
    connect(task, SIGNAL(started()), SLOT(onTaskStarted()));
    connect(task, SIGNAL(failed(QString)), SLOT(onTaskFailed(QString)));
    connect(task, SIGNAL(succeeded()), SLOT(onTaskSucceeded()));
    connect(task, SIGNAL(status(QString)), SLOT(changeStatus(const QString &)));
    connect(task, SIGNAL(progress(qint64, qint64)), SLOT(changeProgress(qint64, qint64)));

    // if this didn't connect to an already running task, invoke start
    if(!task->isRunning())
    {
        task->start();
    }
    if(task->isRunning())
    {
        changeProgress(task->getProgress(), task->getTotalProgress());
        changeStatus(task->getStatus());
        return QDialog::exec();
    }
    else if(handleImmediateResult(result))
    {
        return result;
    }
    else
    {
        return QDialog::Rejected;
    }
}

bool ProgressDialog::handleImmediateResult(QDialog::DialogCode &result)
{
    if(task->isFinished())
    {
        if(task->wasSuccessful())
        {
            result = QDialog::Accepted;
        }
        else
        {
            result = QDialog::Rejected;
        }
        return true;
    }
    return false;
}

Task *ProgressDialog::getTask()
{
    return task;
}

void ProgressDialog::onTaskStarted()
{
}

void ProgressDialog::onTaskFailed(QString failure)
{
    Q_UNUSED(failure);
    // 非模态模式（showWithTask）：任务结束即自动关闭，结果由调用方展示；
    // close() 对模态 exec() 同样生效（以 Rejected 退出事件循环）
    close();
}

void ProgressDialog::onTaskSucceeded()
{
    close();
}

void ProgressDialog::changeStatus(const QString &status)
{
    m_statusLabel->setText(status);
    updateSize();
}

void ProgressDialog::changeProgress(qint64 current, qint64 total)
{
    m_taskProgressBar->setMaximum(total);
    m_taskProgressBar->setValue(current);
}

void ProgressDialog::keyPressEvent(QKeyEvent *e)
{
    if(m_skipButton->isVisible())
    {
        if (e->key() == Qt::Key_Escape)
        {
            on_skipButton_clicked(true);
            return;
        }
        else if(e->key() == Qt::Key_Tab)
        {
            m_skipButton->setFocusPolicy(Qt::StrongFocus);
            m_skipButton->setFocus();
            m_skipButton->setAutoDefault(true);
            m_skipButton->setDefault(true);
            return;
        }
    }
    QDialog::keyPressEvent(e);
}

void ProgressDialog::closeEvent(QCloseEvent *e)
{
    if (task && task->isRunning())
    {
        e->ignore();
    }
    else
    {
        QDialog::closeEvent(e);
    }
}
