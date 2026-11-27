/**
 * @file   translation_compose_format_probe.cpp
 * @brief  翻译页「详细信息」格式栏探针：真实页面 + 真实 style.qss，浅色 / 深色各截一张，
 *         并逐项验证加粗、标题字号、列表、清格式、撤销这些按钮真的改到了正文。
 *
 * 页面本身要求登录才显示表单，探针不碰账号，直接把表单区显示出来；
 * 打开页面的预填逻辑照常走（原文 HTML 传进去，编辑器里就是原文）。
 *
 * 输出：probe_out/translation_format_light.png、translation_format_dark.png、
 *       translation_format_light_narrow.png
 */

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextList>

#include <cstdio>

#include "pages/TranslationComposePage.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"
#include "utils/mod/ModData.h"

namespace {

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

bool expect(bool ok, const QString &what)
{
    std::fprintf(stderr, "[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
    return ok;
}

QPushButton *findButton(QWidget *root, const QString &text)
{
    const QList<QPushButton *> buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

/** 光标处（第一个字符）的字号，用来判断「标题」有没有真的放大 */
qreal fontPointSizeAt(QTextEdit *edit, int blockNumber)
{
    QTextCursor cursor(edit->document());
    cursor.setPosition(edit->document()->findBlockByNumber(blockNumber).position());
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
    return cursor.charFormat().fontPointSize();
}

bool hasListAt(QTextEdit *edit, int blockNumber)
{
    QTextCursor cursor(edit->document());
    cursor.setPosition(edit->document()->findBlockByNumber(blockNumber).position() + 1);
    return cursor.currentList() != nullptr;
}

const QString kOriginalHtml = QStringLiteral(
    "<p>Sodium is a modern rendering engine for Minecraft.</p>"
    "<h3>Features</h3>"
    "<p>Improved performance, <b>fixed lighting</b>, and better <i>FPS</i>.</p>"
    "<ul><li>Faster chunk rendering</li><li>Better memory usage</li></ul>"
    "<p>Visit <a href=\"https://modrinth.com/mod/sodium\">the project page</a>.</p>");

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // 探针不碰用户自己的账号与主题设置：把 QSettings 指到临时 ini，
    // 论坛账号服务读到的是「未登录」，不会真的去论坛拉翻译，截图也更稳定
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QDir::tempPath() + QStringLiteral("/blockbox_translation_probe"));

    QDir().mkpath(QStringLiteral("probe_out"));

    ThemeManager::instance()->applyThemeColor();
    applyWorkspaceQss();
    QApplication::processEvents();

    TranslationComposePage page;
    page.resize(1040, 1000);

    ModInfo info;
    info.id = QStringLiteral("sodium");
    info.name = QStringLiteral("Sodium");
    info.source = QStringLiteral("modrinth");
    page.open(info, QStringLiteral("A modern rendering engine for Minecraft"), kOriginalHtml);

    // 探针不登录：直接把「登录后的表单」显示出来，登录态区域收起来
    bool ok = true;
    QWidget *mainHost = page.findChild<QWidget *>(QStringLiteral("translationMainHost"));
    QWidget *loginHost = page.findChild<QWidget *>(QStringLiteral("translationLoginHost"));
    ok &= expect(mainHost != nullptr, QStringLiteral("找到登录后的表单区"));
    if (mainHost)
        mainHost->setVisible(true);
    if (loginHost)
        loginHost->setVisible(false);

    page.show();
    settle(500);

    QWidget *bar = page.findChild<QWidget *>(QStringLiteral("translationFormatBar"));
    QTextEdit *edit = page.findChild<QTextEdit *>(QStringLiteral("forumReplyEdit"));
    QComboBox *styleCombo = page.findChild<QComboBox *>(QStringLiteral("translationStyleCombo"));
    ok &= expect(bar != nullptr, QStringLiteral("格式栏已插到详细信息编辑框上方"));
    ok &= expect(edit != nullptr, QStringLiteral("详细信息编辑框存在"));
    ok &= expect(styleCombo != nullptr, QStringLiteral("段落样式下拉存在"));
    if (!bar || !edit || !styleCombo)
        return 1;

    // ── 功能：加粗 ──
    edit->setHtml(kOriginalHtml);
    {
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->document()->firstBlock().position());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        edit->setTextCursor(cursor);
        if (QPushButton *bold = findButton(bar, QStringLiteral("B")))
            bold->click();
        settle(100);
        QTextCursor at(edit->document());
        at.setPosition(edit->document()->firstBlock().position() + 1);
        ok &= expect(at.charFormat().fontWeight() >= QFont::DemiBold,
                     QStringLiteral("点「B」后选区文字变粗"));
        ok &= expect(edit->toHtml().contains(QStringLiteral("font-weight:700")),
                     QStringLiteral("加粗写进了提交用的 HTML"));
    }

    // ── 功能：标题字号 ──
    edit->setHtml(kOriginalHtml);
    {
        const qreal before = fontPointSizeAt(edit, 0);
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->document()->findBlockByNumber(0).position() + 1);
        edit->setTextCursor(cursor);
        QMetaObject::invokeMethod(styleCombo, "activated", Q_ARG(int, 2));
        settle(100);
        const qreal after = fontPointSizeAt(edit, 0);
        ok &= expect(after > before + 1.0,
                     QStringLiteral("选「标题」后整段字号变大（%1pt → %2pt）")
                         .arg(before)
                         .arg(after));
    }

    // ── 功能：列表（用干净的段落，别跟原文里自带的列表混在一起看） ──
    edit->setHtml(QStringLiteral("<p>One</p><p>Two</p><p>Three</p>"));
    {
        const QTextBlock secondBlock = edit->document()->findBlockByNumber(1);
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->document()->findBlockByNumber(0).position());
        cursor.setPosition(secondBlock.position() + secondBlock.length() - 1,
                           QTextCursor::KeepAnchor);
        edit->setTextCursor(cursor);
        if (QPushButton *bullet = findButton(bar, QStringLiteral("• 列表")))
            bullet->click();
        settle(100);
        ok &= expect(hasListAt(edit, 0) && hasListAt(edit, 1),
                     QStringLiteral("点「• 列表」后选中的两段都变成列表项"));
        ok &= expect(edit->toHtml().contains(QStringLiteral("<ul")),
                     QStringLiteral("列表写进了提交用的 HTML"));

        // 再点一次退出列表
        if (QPushButton *bullet = findButton(bar, QStringLiteral("• 列表")))
            bullet->click();
        settle(100);
        ok &= expect(!hasListAt(edit, 0) && !hasListAt(edit, 1),
                     QStringLiteral("再点一次退出列表"));
        ok &= expect(!edit->toHtml().contains(QStringLiteral("<ul")),
                     QStringLiteral("退出列表后 HTML 里不再有 <ul>"));
    }

    // ── 功能：清除格式（加粗 / 缩进都还原，图片不丢） ──
    edit->setHtml(QStringLiteral(
        "<p align=\"center\"><span style=\"font-weight:700; font-style:italic\">Bold</span>"
        "<img src=\"x.png\" width=\"20\" height=\"20\"/></p>"));
    {
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->document()->firstBlock().position());
        cursor.select(QTextCursor::BlockUnderCursor);
        edit->setTextCursor(cursor);
        if (QPushButton *clear = findButton(bar, QStringLiteral("清除格式")))
            clear->click();
        settle(100);
        QTextCursor at(edit->document());
        at.setPosition(edit->document()->firstBlock().position());
        at.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
        ok &= expect(at.charFormat().fontWeight() < QFont::DemiBold,
                     QStringLiteral("清除格式后文字不再加粗"));
        ok &= expect(!edit->toHtml().contains(QStringLiteral("text-align:center")),
                     QStringLiteral("清除格式后段落不再居中"));
        ok &= expect(edit->toHtml().contains(QStringLiteral("<img")),
                     QStringLiteral("清除格式不会把正文里的图片清掉"));
    }

    // ── 撤销按钮跟着历史走 ──
    {
        edit->setHtml(kOriginalHtml);
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->document()->firstBlock().position());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        edit->setTextCursor(cursor);
        if (QPushButton *bold = findButton(bar, QStringLiteral("B")))
            bold->click();
        settle(100);
        QPushButton *undo = findButton(bar, QStringLiteral("↶"));
        ok &= expect(undo != nullptr && undo->isEnabled(),
                     QStringLiteral("改过正文后「撤销」按钮可用"));
    }

    // ── 重新走一遍打开页面的流程：预填后格式栏要回到「正文 / 未加粗」 ──
    page.open(info, QStringLiteral("A modern rendering engine for Minecraft"), kOriginalHtml);
    if (mainHost)
        mainHost->setVisible(true);
    if (loginHost)
        loginHost->setVisible(false);
    settle(300);
    {
        QPushButton *bold = findButton(bar, QStringLiteral("B"));
        ok &= expect(bold != nullptr && !bold->isChecked(),
                     QStringLiteral("换资源重新预填后，工具栏不会残留上一次的加粗"));
    }

    // ── 截图 ──
    settle(200);
    QPixmap lightShot = page.grab();
    ok &= expect(lightShot.save(QStringLiteral("probe_out/translation_format_light.png")),
                 QStringLiteral("浅色截图已保存"));

    // 窄窗口：格式栏要自动换行，不能把编辑框挤没
    page.resize(600, 1000);
    settle(400);
    QPixmap narrowShot = page.grab();
    ok &= expect(narrowShot.save(QStringLiteral("probe_out/translation_format_narrow.png")),
                 QStringLiteral("窄窗口截图已保存"));
    page.resize(1040, 1000);
    settle(200);

    ThemeManager::setTheme(&app, ThemeManager::DarkTheme);
    applyWorkspaceQss();
    settle(400);
    QPixmap darkShot = page.grab();
    ok &= expect(darkShot.save(QStringLiteral("probe_out/translation_format_dark.png")),
                 QStringLiteral("深色截图已保存"));

    return ok ? 0 : 1;
}
