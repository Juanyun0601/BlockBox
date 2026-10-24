#include "AppMessageBox.h"

#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtMath>

#include "utils/LanguageManager.h"
#include "utils/ThemeManager.h"

AppMessageBox::AppMessageBox(QWidget *parent)
    : AppDialogBase(parent)
    , m_card(nullptr)
    , m_iconLabel(nullptr)
    , m_titleLabel(nullptr)
    , m_textLabel(nullptr)
    , m_informativeLabel(nullptr)
    , m_infoBrowser(nullptr)
    , m_buttonRow(nullptr)
    , m_buttonLayout(nullptr)
    , m_defaultButton(nullptr)
    , m_clickedButton(nullptr)
    , m_resultButton(NoButton)
    , m_icon(NoIcon)
    , m_buttons(NoButton)
    , m_hasCustomButtons(false)
{
    setObjectName(QStringLiteral("appMessageBox"));

    initUI();
    initStyle();

    hide();
}

AppMessageBox::~AppMessageBox() = default;

void AppMessageBox::initUI()
{
    QGridLayout *mainLayout = new QGridLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("appMsgBoxCard"));
    m_card->setMinimumWidth(420);
    m_card->setMaximumWidth(560);

    QVBoxLayout *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(28, 24, 28, 22);
    cardLayout->setSpacing(0);

    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->setSpacing(18);
    topLayout->setContentsMargins(0, 0, 0, 0);

    m_iconLabel = new QLabel(m_card);
    m_iconLabel->setObjectName(QStringLiteral("appMsgBoxIconLabel"));
    m_iconLabel->setFixedSize(44, 44);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->hide();

    QVBoxLayout *midLayout = new QVBoxLayout();
    midLayout->setSpacing(8);
    midLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(m_card);
    m_titleLabel->setObjectName(QStringLiteral("appMsgBoxTitleLabel"));
    m_titleLabel->setWordWrap(true);

    m_textLabel = new QLabel(m_card);
    m_textLabel->setObjectName(QStringLiteral("appMsgBoxTextLabel"));
    m_textLabel->setWordWrap(true);
    m_textLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_informativeLabel = new QLabel(m_card);
    m_informativeLabel->setObjectName(QStringLiteral("appMsgBoxInformativeLabel"));
    m_informativeLabel->setWordWrap(true);
    m_informativeLabel->hide();

    // Markdown 说明区（更新日志等）：只读 QTextBrowser，过长时内部滚动
    m_infoBrowser = new QTextBrowser(m_card);
    m_infoBrowser->setObjectName(QStringLiteral("appMsgBoxInfoBrowser"));
    m_infoBrowser->setOpenExternalLinks(true);
    m_infoBrowser->setOpenLinks(true);
    m_infoBrowser->setFrameShape(QFrame::NoFrame);
    m_infoBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_infoBrowser->setLineWrapMode(QTextEdit::WidgetWidth);
    m_infoBrowser->document()->setDocumentMargin(0);
    m_infoBrowser->setMinimumHeight(120);
    m_infoBrowser->setMaximumHeight(340);
    m_infoBrowser->hide();

    midLayout->addWidget(m_titleLabel);
    midLayout->addWidget(m_textLabel);
    midLayout->addWidget(m_informativeLabel);
    midLayout->addWidget(m_infoBrowser);

    topLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);
    topLayout->addLayout(midLayout, 1);

    cardLayout->addLayout(topLayout);
    cardLayout->addSpacing(18);

    m_buttonRow = new QWidget(m_card);
    m_buttonRow->setObjectName(QStringLiteral("appMsgBoxButtonRow"));
    m_buttonLayout = new QHBoxLayout(m_buttonRow);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_buttonLayout->setSpacing(10);
    m_buttonLayout->addStretch();

    cardLayout->addWidget(m_buttonRow);

    mainLayout->addWidget(m_card, 0, 0, Qt::AlignCenter);
}

void AppMessageBox::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();

    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg      = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder  = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString titleColor  = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor   = isLight ? "#4a4a4a" : "#c9c9c9";
    const QString infoColor   = isLight ? "#8a8a8a" : "#909090";
    const QString btnBg       = isLight ? "#f5f5f5" : "#3b3b40";
    const QString btnBorder   = isLight ? "#d4d4d4" : "#55555a";
    const QString btnText     = isLight ? "#333333" : "#e8e8e8";

    const QString style = QString(
        "QWidget#appMsgBoxCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 16px;"
        "}"
        ""
        "QLabel#appMsgBoxTitleLabel {"
        "    color: %3;"
        "    background-color: transparent;"
        "    font-size: 15px;"
        "    font-weight: bold;"
        "}"
        ""
        "QLabel#appMsgBoxTextLabel {"
        "    color: %4;"
        "    background-color: transparent;"
        "    font-size: 13px;"
        "}"
        ""
        "QLabel#appMsgBoxInformativeLabel {"
        "    color: %5;"
        "    background-color: transparent;"
        "    font-size: 12px;"
        "}"
        ""
        "QTextBrowser#appMsgBoxInfoBrowser {"
        "    color: %4;"
        "    background-color: transparent;"
        "    border: none;"
        "    font-size: 12px;"
        "}"
        ""
        "QScrollArea#appMsgBoxInfoScroll {"
        "    background: transparent;"
        "    border: none;"
        "}"
        ""
        "QTextBrowser#appMsgBoxInfoBrowser QScrollBar:vertical {"
        "    background: transparent;"
        "    width: 8px;"
        "    margin: 0;"
        "}"
        ""
        "QTextBrowser#appMsgBoxInfoBrowser QScrollBar::handle:vertical {"
        "    background: %7;"
        "    border-radius: 4px;"
        "    min-height: 30px;"
        "}"
        ""
        "QTextBrowser#appMsgBoxInfoBrowser QScrollBar::add-line:vertical,"
        "QTextBrowser#appMsgBoxInfoBrowser QScrollBar::sub-line:vertical {"
        "    height: 0;"
        "}"
        ""
        "QWidget#appMsgBoxButtonRow {"
        "    background-color: transparent;"
        "}"
        ""
        "QPushButton#appMsgBoxBtn {"
        "    background-color: %6;"
        "    border: 1px solid %7;"
        "    border-radius: 12px;"
        "    color: %8;"
        "    padding: 7px 22px;"
        "    font-size: 13px;"
        "}"
        ""
        "QPushButton#appMsgBoxBtn:hover {"
        "    background-color: %9;"
        "    border-color: %9;"
        "}"
        ""
        "QPushButton#appMsgBoxBtn:pressed {"
        "    background-color: %9;"
        "    border-color: %9;"
        "}"
        ""
        "QPushButton#appMsgBoxBtn[primary=\"true\"] {"
        "    background-color: %10;"
        "    border-color: %10;"
        "    color: #ffffff;"
        "}"
        ""
        "QPushButton#appMsgBoxBtn[primary=\"true\"]:hover {"
        "    background-color: %11;"
        "    border-color: %11;"
        "}"
    ).arg(cardBg, cardBorder, titleColor, textColor, infoColor,
           btnBg, btnBorder, btnText, QStringLiteral("rgba(0,0,0,28)"),
           themeColor, themeHover);

    setStyleSheet(style);
}

void AppMessageBox::setText(const QString &text)
{
    m_text = text;
    if (m_textLabel)
        m_textLabel->setText(m_text);
}

QString AppMessageBox::text() const
{
    return m_text;
}

void AppMessageBox::setInformativeText(const QString &text)
{
    m_informativeText = text;
    if (m_infoBrowser)
        m_infoBrowser->hide();
    if (m_informativeLabel) {
        m_informativeLabel->setText(m_informativeText);
        m_informativeLabel->setVisible(!m_informativeText.isEmpty());
    }
}

void AppMessageBox::setInformativeMarkdown(const QString &markdown)
{
    m_informativeText = markdown;
    if (m_informativeLabel)
        m_informativeLabel->hide();
    if (!m_infoBrowser)
        return;

    if (markdown.trimmed().isEmpty()) {
        m_infoBrowser->clear();
        m_infoBrowser->hide();
        return;
    }

    applyMarkdownDocumentStyle();
    m_infoBrowser->setMarkdown(markdown);
    m_infoBrowser->show();
    updateMarkdownBrowserHeight();
}

void AppMessageBox::applyMarkdownDocumentStyle()
{
    if (!m_infoBrowser)
        return;

    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString textColor   = isLight ? "#4a4a4a" : "#c9c9c9";
    const QString headingColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString linkColor   = tm->currentThemeColor();
    const QString codeBg      = isLight ? "rgba(0,0,0,0.06)" : "rgba(255,255,255,0.08)";

    // 必须在 setMarkdown 之前设置，才能作用到生成的富文本节点
    m_infoBrowser->document()->setDefaultStyleSheet(QStringLiteral(
        "body, p, li, td { color: %1; font-size: 12px; }"
        "h1, h2, h3, h4, h5, h6 { color: %2; font-size: 13px; margin-top: 8px; margin-bottom: 4px; }"
        "a { color: %3; }"
        "code, pre { color: %1; background-color: %4; }"
        "ul, ol { margin-top: 2px; margin-bottom: 2px; }"
        "blockquote { color: %1; margin-left: 0; padding-left: 8px; }"
    ).arg(textColor, headingColor, linkColor, codeBg));
}

void AppMessageBox::updateMarkdownBrowserHeight()
{
    // 用 isHidden 而非 isVisible：弹窗 exec 前父控件尚未显示，isVisible 恒为 false
    if (!m_infoBrowser || m_infoBrowser->isHidden())
        return;

    // 短内容贴合内容高度；长内容封顶后内部滚动，避免弹窗撑满屏幕
    m_infoBrowser->document()->adjustSize();
    const int contentH = qCeil(m_infoBrowser->document()->size().height()) + 12;
    constexpr int kMinNotesHeight = 120;
    constexpr int kMaxNotesHeight = 340;
    m_infoBrowser->setFixedHeight(qBound(kMinNotesHeight, contentH, kMaxNotesHeight));
}

void AppMessageBox::setIcon(Icon icon)
{
    m_icon = icon;
    if (!m_iconLabel)
        return;
    if (m_icon == NoIcon) {
        m_iconLabel->clear();
        m_iconLabel->hide();
        m_iconLabel->setProperty("type", QString());
        m_iconLabel->style()->polish(m_iconLabel);
        return;
    }
    switch (m_icon) {
    case Information:
        m_iconLabel->setProperty("type", QStringLiteral("info"));
        break;
    case Warning:
        m_iconLabel->setProperty("type", QStringLiteral("warning"));
        break;
    case Critical:
        m_iconLabel->setProperty("type", QStringLiteral("error"));
        break;
    case Question:
    default:
        m_iconLabel->setProperty("type", QStringLiteral("question"));
        break;
    }
    m_iconLabel->setPixmap(makeIconPixmap(m_icon));
    m_iconLabel->style()->polish(m_iconLabel);
    m_iconLabel->show();
}

void AppMessageBox::setIconPixmap(const QPixmap &pixmap)
{
    m_customPixmap = pixmap;
    if (!m_iconLabel)
        return;
    if (m_customPixmap.isNull()) {
        m_iconLabel->clear();
        m_iconLabel->hide();
        return;
    }
    m_iconLabel->setPixmap(m_customPixmap.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_iconLabel->show();
}

void AppMessageBox::setStandardButtons(StandardButtons buttons)
{
    m_buttons = buttons;
    buildButtons();
}

void AppMessageBox::setDefaultButton(StandardButton which)
{
    QPushButton *btn = button(which);
    if (btn)
        setDefaultButton(btn);
}

void AppMessageBox::setDefaultButton(QPushButton *button)
{
    m_defaultButton = button;
    if (m_defaultButton)
        m_defaultButton->setDefault(true);
}

QPushButton *AppMessageBox::addButton(const QString &text, ButtonRole role)
{
    QPushButton *btn = new QPushButton(text, m_buttonRow);
    btn->setObjectName(QStringLiteral("appMsgBoxBtn"));
    btn->setCursor(Qt::PointingHandCursor);
    if (role == AcceptRole || role == YesRole)
        btn->setProperty("primary", true);

    m_buttonMap.insert(btn, NoButton);
    m_hasCustomButtons = true;
    if (m_buttonLayout)
        m_buttonLayout->addWidget(btn);

    connect(btn, &QPushButton::clicked, this, &AppMessageBox::onButtonClicked);
    return btn;
}

QPushButton *AppMessageBox::button(StandardButton which) const
{
    return m_buttonByStandard.value(which, nullptr);
}

QPushButton *AppMessageBox::clickedButton() const
{
    return m_clickedButton;
}

void AppMessageBox::setButtonText(StandardButton which, const QString &text)
{
    QPushButton *btn = button(which);
    if (btn)
        btn->setText(text);
}

void AppMessageBox::buildButtons()
{
    if (!m_buttonLayout)
        return;

    while (QLayoutItem *item = m_buttonLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    m_buttonMap.clear();
    m_buttonByStandard.clear();
    m_defaultButton = nullptr;
    m_hasCustomButtons = false;

    m_buttonLayout->addStretch();

    if (m_buttons == NoButton)
        return;

    static const StandardButton order[] = {
        Ok, Save, SaveAll, Open, Yes, YesToAll, No, NoToAll,
        Abort, Retry, Ignore, Close, Cancel, Discard, Help, Apply, Reset, RestoreDefaults
    };
    for (StandardButton b : order) {
        if (m_buttons & b)
            m_buttonLayout->addWidget(createStandardButton(b));
    }
}

QPushButton *AppMessageBox::createStandardButton(StandardButton button)
{
    QPushButton *btn = new QPushButton(standardButtonText(button), m_buttonRow);
    btn->setObjectName(QStringLiteral("appMsgBoxBtn"));
    btn->setCursor(Qt::PointingHandCursor);

    const ButtonRole role = standardButtonRole(button);
    if (role == AcceptRole || role == YesRole)
        btn->setProperty("primary", true);

    m_buttonMap.insert(btn, button);
    m_buttonByStandard.insert(button, btn);
    connect(btn, &QPushButton::clicked, this, &AppMessageBox::onButtonClicked);
    return btn;
}

QString AppMessageBox::standardButtonText(StandardButton button) const
{
    const LanguageManager::Language lang = LanguageManager::instance()->currentLanguage();
    // 按语言取文案：简体 / 繁體 / English / Español / 日本語 / Русский
    auto pick = [lang](const QString &zh, const QString &trad, const QString &en,
                       const QString &es, const QString &ja, const QString &ru) -> QString {
        switch (lang) {
        case LanguageManager::Chinese:            return zh;
        case LanguageManager::ChineseTraditional: return trad;
        case LanguageManager::Spanish:            return es;
        case LanguageManager::Japanese:           return ja;
        case LanguageManager::Russian:            return ru;
        case LanguageManager::English:
        default:                                  return en;
        }
    };

    switch (button) {
    case Ok:
        return pick(QStringLiteral("确定"), QStringLiteral("確定"), QStringLiteral("OK"),
                    QStringLiteral("Aceptar"), QStringLiteral("OK"), QStringLiteral("ОК"));
    case Cancel:
        return pick(QStringLiteral("取消"), QStringLiteral("取消"), QStringLiteral("Cancel"),
                    QStringLiteral("Cancelar"), QStringLiteral("キャンセル"), QStringLiteral("Отмена"));
    case Yes:
        return pick(QStringLiteral("是"), QStringLiteral("是"), QStringLiteral("Yes"),
                    QStringLiteral("Sí"), QStringLiteral("はい"), QStringLiteral("Да"));
    case No:
        return pick(QStringLiteral("否"), QStringLiteral("否"), QStringLiteral("No"),
                    QStringLiteral("No"), QStringLiteral("いいえ"), QStringLiteral("Нет"));
    case Close:
        return pick(QStringLiteral("关闭"), QStringLiteral("關閉"), QStringLiteral("Close"),
                    QStringLiteral("Cerrar"), QStringLiteral("閉じる"), QStringLiteral("Закрыть"));
    case Apply:
        return pick(QStringLiteral("应用"), QStringLiteral("套用"), QStringLiteral("Apply"),
                    QStringLiteral("Aplicar"), QStringLiteral("適用"), QStringLiteral("Применить"));
    case Reset:
        return pick(QStringLiteral("重置"), QStringLiteral("重設"), QStringLiteral("Reset"),
                    QStringLiteral("Restablecer"), QStringLiteral("リセット"), QStringLiteral("Сброс"));
    case Help:
        return pick(QStringLiteral("帮助"), QStringLiteral("說明"), QStringLiteral("Help"),
                    QStringLiteral("Ayuda"), QStringLiteral("ヘルプ"), QStringLiteral("Справка"));
    case Save:
        return pick(QStringLiteral("保存"), QStringLiteral("儲存"), QStringLiteral("Save"),
                    QStringLiteral("Guardar"), QStringLiteral("保存"), QStringLiteral("Сохранить"));
    case Open:
        return pick(QStringLiteral("打开"), QStringLiteral("開啟"), QStringLiteral("Open"),
                    QStringLiteral("Abrir"), QStringLiteral("開く"), QStringLiteral("Открыть"));
    default:
        break;
    }
    return QString();
}

AppMessageBox::ButtonRole AppMessageBox::standardButtonRole(StandardButton button) const
{
    switch (button) {
    case Ok:
    case Save:
    case SaveAll:
    case Open:
    case Yes:
    case YesToAll:
    case Retry:
    case Apply:
        return AcceptRole;
    case Help:
        return HelpRole;
    default:
        return RejectRole;
    }
}

QPixmap AppMessageBox::makeIconPixmap(Icon icon) const
{
    if (icon == NoIcon)
        return QPixmap();

    const int s = 44;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QColor bg;
    QString glyph;
    switch (icon) {
    case Information:
        bg = QColor("#2196F3"); glyph = QStringLiteral("i"); break;
    case Warning:
        bg = QColor("#FF9800"); glyph = QStringLiteral("!"); break;
    case Critical:
        bg = QColor("#F44336"); glyph = QStringLiteral("\u2715"); break;
    case Question:
    default:
        bg = QColor("#2196F3"); glyph = QStringLiteral("?"); break;
    }

    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, s, s);

    QFont f = p.font();
    f.setPointSize(16);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, s, s), Qt::AlignCenter, glyph);

    return pm;
}

void AppMessageBox::setWindowTitle(const QString &title)
{
    QWidget::setWindowTitle(title);
    refreshTitleLabel();
}

void AppMessageBox::refreshTitleLabel()
{
    if (!m_titleLabel)
        return;
    const QString title = windowTitle();
    m_titleLabel->setText(title);
    m_titleLabel->setVisible(!title.isEmpty());
}

int AppMessageBox::exec()
{
    m_resultButton = NoButton;
    m_clickedButton = nullptr;
    refreshTitleLabel();
    positionOverWindow();
    setWindowModality(Qt::WindowModal);
    QDialog::exec();
    return int(m_resultButton);
}

void AppMessageBox::closeWithButton(QPushButton *button)
{
    m_clickedButton = button;
    m_resultButton = m_buttonMap.value(button, NoButton);
    done(int(m_resultButton));
}

void AppMessageBox::onButtonClicked()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    closeWithButton(btn);
}

void AppMessageBox::positionOverWindow()
{
    AppDialogBase::positionOverWindow();
}

void AppMessageBox::captureBlurBackground()
{
    AppDialogBase::captureBlurBackground();
}

void AppMessageBox::showEvent(QShowEvent *event)
{
    AppDialogBase::showEvent(event);
    refreshTitleLabel();
    if (m_defaultButton)
        m_defaultButton->setFocus();
}

void AppMessageBox::paintEvent(QPaintEvent *event)
{
    AppDialogBase::paintEvent(event);
}

void AppMessageBox::resizeEvent(QResizeEvent *event)
{
    AppDialogBase::resizeEvent(event);
}

void AppMessageBox::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (QPushButton *cancelBtn = button(Cancel)) {
            cancelBtn->click();
            return;
        }
        closeWithButton(nullptr);
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_defaultButton) {
        m_defaultButton->click();
        return;
    }
    QDialog::keyPressEvent(event);
}

AppMessageBox::StandardButton AppMessageBox::question(QWidget *parent, const QString &title,
                                                     const QString &text, StandardButtons buttons,
                                                     StandardButton defaultButton)
{
    AppMessageBox box(parent);
    box.setWindowTitle(title);
    box.setText(text);
    box.setIcon(Question);
    box.setStandardButtons(buttons);
    box.setDefaultButton(defaultButton);
    return static_cast<StandardButton>(box.exec());
}

void AppMessageBox::information(QWidget *parent, const QString &title, const QString &text,
                                StandardButtons buttons, StandardButton defaultButton)
{
    AppMessageBox box(parent);
    box.setWindowTitle(title);
    box.setText(text);
    box.setIcon(Information);
    box.setStandardButtons(buttons);
    box.setDefaultButton(defaultButton);
    box.exec();
}

void AppMessageBox::warning(QWidget *parent, const QString &title, const QString &text,
                            StandardButtons buttons, StandardButton defaultButton)
{
    AppMessageBox box(parent);
    box.setWindowTitle(title);
    box.setText(text);
    box.setIcon(Warning);
    box.setStandardButtons(buttons);
    box.setDefaultButton(defaultButton);
    box.exec();
}

void AppMessageBox::critical(QWidget *parent, const QString &title, const QString &text,
                             StandardButtons buttons, StandardButton defaultButton)
{
    AppMessageBox box(parent);
    box.setWindowTitle(title);
    box.setText(text);
    box.setIcon(Critical);
    box.setStandardButtons(buttons);
    box.setDefaultButton(defaultButton);
    box.exec();
}
