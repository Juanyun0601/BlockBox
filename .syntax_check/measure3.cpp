#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QFile>
#include <QRegularExpression>
#include <QFontMetrics>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // Controlled test 1: does QSS font-size update widget->font()?
    app.setStyleSheet(QStringLiteral("QLabel { font-size: 14px; } QLabel#statusLabel { font-size: 12px; }"));
    QLabel *a = new QLabel("abc");
    a->ensurePolished();
    printf("ctrl plain label fontPx=%d\n", a->font().pixelSize());
    QLabel *b = new QLabel("abc");
    b->setObjectName("statusLabel");
    b->ensurePolished();
    printf("ctrl statusLabel fontPx=%d\n", b->font().pixelSize());

    // Controlled test 2: padding on button
    app.setStyleSheet(QStringLiteral("QPushButton#bottomActionBtn { padding: 6px 14px; font-size: 12px; border:1px solid #888; }"));
    QPushButton *btn = new QPushButton("全选");
    btn->setObjectName("bottomActionBtn");
    btn->ensurePolished();
    printf("ctrl btn fontPx=%d sizeHint=%d\n", btn->font().pixelSize(), btn->sizeHint().width());

    // Full QSS: measure with fontMetrics of the polished widgets
    app.setStyleSheet(QString());
    QFile f("styles/style.qss");
    if (!f.open(QFile::ReadOnly)) { printf("cannot open qss\n"); return 1; }
    QString qss = QString::fromUtf8(f.readAll());
    qss.replace(QRegularExpression(QStringLiteral("@[A-Za-z0-9_]+@")), QStringLiteral("#888888"));
    app.setStyleSheet(qss);

    QLabel *l = new QLabel(QStringLiteral("共 12 个模组 (10 启用, 2 禁用)"));
    l->setObjectName("statusLabel");
    l->ensurePolished();
    printf("fullQSS stats fontPx=%d fmWidth=%d sizeHint=%d family=%s\n",
           l->font().pixelSize(), l->fontMetrics().horizontalAdvance(l->text()),
           l->sizeHint().width(), qPrintable(l->font().family()));

    QLabel *fl = new QLabel(QStringLiteral("分类:"));
    fl->setObjectName("filterLabel");
    fl->ensurePolished();
    printf("fullQSS filterLabel fontPx=%d sizeHint=%d\n", fl->font().pixelSize(), fl->sizeHint().width());

    QPushButton *bb = new QPushButton(QStringLiteral("全选"));
    bb->setObjectName("bottomActionBtn");
    bb->ensurePolished();
    printf("fullQSS bottomBtn fontPx=%d sizeHint=%d\n", bb->font().pixelSize(), bb->sizeHint().width());

    // Does full QSS contain the statusLabel rule text as loaded?
    int idx = qss.indexOf(QStringLiteral("QLabel#statusLabel"));
    printf("qss has statusLabel rule at %d\n", idx);
    if (idx >= 0) {
        QByteArray snippet = qss.mid(idx, 120).toUtf8();
        printf("snippet: %s\n", snippet.constData());
    }
    return 0;
}
