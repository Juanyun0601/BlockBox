/**
 * @file   mail_feedback_probe.cpp
 * @brief  「邮件反馈」对话框版面探针：浅色 / 深色各截一张图，肉眼核对排版与文案
 *
 * 直接实例化 MailFeedbackDialog，套用工作区里的 style.qss（探针没有嵌入资源），
 * 输出 probe_out/mail_feedback_light.png 与 probe_out/mail_feedback_dark.png。
 */

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QPixmap>

#include <cstdio>

#include "components/MailFeedbackDialog.h"
#include "pages/FeedbackPage.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace
{

void settle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
}

void applyWorkspaceQss()
{
    QFile qssFile(QStringLiteral("styles/style.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text))
        qApp->setStyleSheet(StyleKit::resolve(QString::fromUtf8(qssFile.readAll())));
}

bool shoot(MailFeedbackDialog *dialog, const QString &path)
{
    dialog->show();
    settle(700);
    const QPixmap shot = dialog->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(), shot.height(),
            ok ? "ok" : "FAILED");
    dialog->hide();
    settle(100);
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QDir().mkpath(QStringLiteral("probe_out"));

    ThemeManager::instance()->applyThemeColor();
    applyWorkspaceQss();
    QApplication::processEvents();

    bool ok = true;

    MailFeedbackDialog light;
    light.resize(620, 600);
    ok = shoot(&light, QStringLiteral("probe_out/mail_feedback_light.png")) && ok;

    // 反馈页顶栏：确认「邮件反馈」入口与「提交新反馈」并排，不抢主按钮
    FeedbackPage feedback;
    feedback.resize(1180, 700);
    feedback.show();
    settle(800);
    const QPixmap pageShot = feedback.grab();
    fprintf(stderr, "[probe] shot probe_out/feedback_mail_entry.png %dx%d\n", pageShot.width(),
            pageShot.height());
    ok = pageShot.save(QStringLiteral("probe_out/feedback_mail_entry.png")) && ok;
    feedback.hide();
    settle(100);

    ThemeManager::setTheme(&app, ThemeManager::DarkTheme);
    applyWorkspaceQss();
    QApplication::processEvents();

    MailFeedbackDialog dark;
    dark.resize(620, 600);
    ok = shoot(&dark, QStringLiteral("probe_out/mail_feedback_dark.png")) && ok;

    return ok ? 0 : 1;
}
