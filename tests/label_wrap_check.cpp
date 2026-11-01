/**
 * @file   label_wrap_check.cpp
 * @brief  校验「当前日志：<长路径>」在 settingDesc 标签里的折行效果（零宽空格插入方案）
 *
 * 仅用于人工查看：把一个 settingDesc 风格的 QLabel 放在固定宽度容器里截图，
 * 确认超长路径能在路径中间折行、不再把行宽撑爆。
 */

#include <QApplication>
#include <QFile>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString breakablePathText(const QString &path)
{
    QString result;
    result.reserve(path.size() * 2);
    for (const QChar ch : path) {
        result.append(ch);
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('/') || ch == QLatin1Char('-')
            || ch == QLatin1Char('_') || ch == QLatin1Char('.'))
            result.append(QChar(0x200B));
    }
    return result;
}

QLabel *makeDesc(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(parent);
    label->setObjectName("settingDesc");
    label->setWordWrap(true);
    label->setText(text);
    return label;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QFile qss(QStringLiteral("../styles/style.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    const QString rawPath = QStringLiteral(
        "C:\\Users\\Administrator\\AppData\\Roaming\\BlockBox\\logs\\launcher-2026-10-25.log");
    const QString plainText = QStringLiteral("当前日志：") + rawPath;
    const QString wrappedText = QStringLiteral("当前日志：") + breakablePathText(rawPath);

    QWidget box;
    box.setObjectName("wrapCheckBox");
    QVBoxLayout *layout = new QVBoxLayout(&box);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    QLabel *plain = makeDesc(plainText, &box);
    QLabel *wrapped = makeDesc(wrappedText, &box);
    layout->addWidget(plain);
    layout->addWidget(wrapped);
    box.resize(300, 240);

    qInfo().noquote() << "plain  minWidth=" << plain->minimumSizeHint().width()
                      << " height=" << plain->heightForWidth(268);
    qInfo().noquote() << "wrapped minWidth=" << wrapped->minimumSizeHint().width()
                      << " height=" << wrapped->heightForWidth(268);

    box.show();
    QApplication::processEvents();
    const bool ok = box.grab().save(QStringLiteral("label_wrap_check.png"));
    qInfo() << "saved" << ok;
    return ok ? 0 : 1;
}
