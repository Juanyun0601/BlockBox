/**
 * 探针：量一下「发现新版本」弹窗的实际尺寸，并截图留档。
 * 用法：msgbox_probe.exe <输出目录>
 */

#include "components/AppMessageBox.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QMainWindow>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>

namespace
{
const char *kNotes =
    "本版修好了论坛账户页点「我的主题 / 我的回复 / 我的收藏」会直接跳进帖子详情的问题；"
    "聊天页整体重做，好友在线状态、已读回执、表情面板全部到位。\n\n"
    "- 登录会一直保留，下次打开启动器自动恢复登录。\n"
    "- 帖子里的每层楼都能引用上一楼、复制楼层链接。\n"
    "- 软件顶栏新增「系统消息」和「聊天」两个快捷入口。\n"
    "- 搜索页重做：整页只剩一个搜索框，输入关键词就能搜资源。\n"
    "- 修好了从新版账户页打开皮肤编辑器后「应用到账户」可能落到别的账户上的问题。\n"
    "- 个人主页的封面在软件里也按 3:1 显示了。\n"
    "- 官方论坛启用新域名 forum.mcblockbox.top。\n"
    "- 上传头像和主页封面不用再自己先压图了。\n"
    "- 启动器里的论坛新增好友和私信。\n";
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QString outDir = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(outDir);

    // 可选：传入一份真实的更新日志 Markdown，用来验证超长内容时的封顶高度
    QString longNotes;
    if (argc > 2) {
        QFile f(QString::fromLocal8Bit(argv[2]));
        if (f.open(QIODevice::ReadOnly))
            longNotes = QString::fromUtf8(f.readAll());
    }

    QMainWindow host;
    host.resize(1280, 800);
    host.show();
    QApplication::processEvents();

    // 与新版本弹窗完全一致的设置
    AppMessageBox box(&host);
    box.setIcon(AppMessageBox::Information);
    box.setWindowTitle(QString::fromUtf8("发现新版本"));
    box.setCardWidthRange(820, 1080);
    box.setInfoBrowserHeightRange(240, 640);
    box.setInfoBrowserFullWidth();
    box.setText(QString::fromUtf8("发现新版本 beta7，当前版本为 beta6，是否前往下载？"));
    box.setInformativeMarkdown(QString::fromUtf8("## 更新说明\n\n") + QString::fromUtf8(kNotes));
    box.addButton(QString::fromUtf8("更新"), AppMessageBox::AcceptRole);
    box.addButton(QString::fromUtf8("前往浏览器下载"), AppMessageBox::ActionRole);
    box.addButton(QString::fromUtf8("以后再说"), AppMessageBox::RejectRole);

    // 对照：不调宽调高时的普通提示弹窗
    AppMessageBox plain(&host);
    plain.setIcon(AppMessageBox::Information);
    plain.setWindowTitle(QString::fromUtf8("发现新版本"));
    plain.setText(QString::fromUtf8("发现新版本 beta7，当前版本为 beta6，是否前往下载？"));
    plain.setInformativeMarkdown(QString::fromUtf8("## 更新说明\n\n") + QString::fromUtf8(kNotes));
    plain.addButton(QString::fromUtf8("以后再说"), AppMessageBox::RejectRole);

    box.show();
    plain.show();

    AppMessageBox longBox(&host);
    longBox.setIcon(AppMessageBox::Information);
    longBox.setWindowTitle(QString::fromUtf8("发现新版本"));
    longBox.setCardWidthRange(820, 1080);
    longBox.setInfoBrowserHeightRange(240, 640);
    longBox.setInfoBrowserFullWidth();
    longBox.setText(QString::fromUtf8("发现新版本 beta7，当前版本为 beta6，是否前往下载？"));
    longBox.addButton(QString::fromUtf8("更新"), AppMessageBox::AcceptRole);
    longBox.addButton(QString::fromUtf8("以后再说"), AppMessageBox::RejectRole);
    if (!longNotes.isEmpty()) {
        longBox.setInformativeMarkdown(QString::fromUtf8("## 更新说明\n\n") + longNotes);
        longBox.show();
        box.hide();
        plain.hide();
    }

    QTimer::singleShot(600, [&]() {
        auto dump = [&](const char *tag, AppMessageBox &dlg, const QString &file) {
            QWidget *card = dlg.findChild<QWidget *>(QStringLiteral("appMsgBoxCard"));
            QTextBrowser *browser =
                dlg.findChild<QTextBrowser *>(QStringLiteral("appMsgBoxInfoBrowser"));
            qInfo().noquote()
                << tag << "dialog=" << dlg.size() << "card="
                << (card ? card->size() : QSize()) << "notes="
                << (browser ? browser->size() : QSize());
            if (browser) {
                qInfo().noquote() << tag << "viewport=" << browser->viewport()->size()
                                  << "docTextWidth=" << browser->document()->textWidth()
                                  << "docSize=" << browser->document()->size().toSize();
            }
            const bool saved = dlg.grab().save(QDir(outDir).filePath(file));
            qInfo().noquote() << tag << "saved=" << saved << QDir(outDir).filePath(file);
            if (card) {
                card->grab().save(QDir(outDir).filePath(
                    QStringLiteral("card_") + file));
            }
        };
        dump("NEW  ", box, QStringLiteral("msgbox_new.png"));
        box.close();
        if (longNotes.isEmpty()) {
            dump("PLAIN", plain, QStringLiteral("msgbox_plain.png"));
        } else {
            dump("LONG ", longBox, QStringLiteral("msgbox_long.png"));
            longBox.close();
        }
        app.quit();
    });

    return app.exec();
}


