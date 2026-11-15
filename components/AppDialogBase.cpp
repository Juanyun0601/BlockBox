#include "AppDialogBase.h"

#include <QApplication>
#include <QEvent>
#include <QHideEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include "utils/UiMetrics.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
/// 所有存活的 AppDialogBase：安卓返回键需要找"当前最上层弹窗"来关闭，
/// 覆盖层模式不是顶层窗口，topLevelWidgets() 找不到它们，只能自行登记。
QList<AppDialogBase *> g_activeDialogs;
} // namespace

const QList<AppDialogBase *> &AppDialogBase::activeDialogs()
{
    return g_activeDialogs;
}

AppDialogBase *AppDialogBase::topMostVisibleDialog()
{
    // 叠放时后 show 的在上层：从登记表尾部往前找第一个可见的
    for (int i = g_activeDialogs.size() - 1; i >= 0; --i) {
        if (g_activeDialogs[i] && g_activeDialogs[i]->isVisible())
            return g_activeDialogs[i];
    }
    return nullptr;
}

AppDialogBase::AppDialogBase(QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
#ifdef Q_OS_ANDROID
    // 安卓：Qt 独立顶层窗口的几何/输入映射在真机上不可靠（实测输入坐标被
    // 错误缩放、遮罩盖不满窗口、卡片外露出游离滚动条）。改为宿主窗口内的
    // 覆盖层控件：挂到宿主窗口下、铺满客户区，靠自身遮罩拦截点击，
    // 键盘/返回键焦点走控件焦点链。
    setWindowFlags(Qt::Widget);
    setWindowModality(Qt::NonModal);
#else
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::WindowModal);
#endif

    // 清除全局 QSS 中 QDialog 通用规则的背景/边框影响：
    // 弹窗自身必须透明，模糊背景 + 暗色遮罩由基类 paintEvent 绘制。
    // （子类若调用 setStyleSheet 会整体覆盖此处，同样不再受全局 QDialog 规则影响）
    setStyleSheet(QStringLiteral("QDialog { background-color: transparent; border: none; }"));

    // 首次显示前先按宿主窗口预定位，并标记为“已手动定位”，
    // 避免无边框对话框以默认位置（屏幕左上角）映射，超出软件范围。
    fitCardToScreen();
    positionOverWindow();
    setAttribute(Qt::WA_Moved, true);

    g_activeDialogs.append(this);
}

AppDialogBase::~AppDialogBase()
{
    g_activeDialogs.removeOne(this);
}

int AppDialogBase::exec()
{
#ifdef Q_OS_ANDROID
    // 覆盖层模式：嵌套事件循环等 finished，输入拦截靠铺满宿主的遮罩本身。
    if (m_execLoop) {
        qWarning("AppDialogBase::exec: Recursive loop detected for dialog '%s'",
                 qPrintable(objectName()));
        return Rejected;
    }
    QEventLoop loop;
    m_execLoop = &loop;
    connect(this, &QDialog::finished, &loop, &QEventLoop::quit);
    show();
    const int ret = loop.exec(QEventLoop::DialogExec);
    m_execLoop = nullptr;
    return ret;
#else
    return QDialog::exec();
#endif
}

QWidget *AppDialogBase::findCardWidget() const
{
    const QList<QWidget *> children = findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : children) {
        if (w->objectName().endsWith(QStringLiteral("Card")))
            return w;
    }
    return nullptr;
}

void AppDialogBase::fitCardToScreen()
{
    // 居中卡片兜底：无论子类把卡片设计成多大，都不能超过所在屏幕的可用范围。
    // 移动端屏幕小，卡片被压回屏内后内容可能仍放不下，这里统一包一层垂直滚动区：
    // 弹窗整体始终完整显示在屏幕内，卡内内容上下滚动查看。
    // 桌面端内容放得下时滚动条不出现，外观与行为无任何变化。
    QWidget *card = findCardWidget();
    if (!card)
        return;

    const QSize avail = UiMetrics::fitToScreen(this, QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    if (avail.isEmpty())
        return;

    // 只把最小尺寸压到屏内；最小尺寸缩小后最大尺寸（若设置过）仍然更大，
    // resize 可以正常生效，卡片内部布局继续按缩小后的宽度自适应/滚动
    const QSize minSize = card->minimumSize();
    const QSize clamped = minSize.boundedTo(avail);

    // 已经包过滚动区（重复 showEvent / 子类自带）：只继续压最小尺寸，不重复包
    const QList<QScrollArea *> scrolls =
        card->findChildren<QScrollArea *>(QString(), Qt::FindDirectChildrenOnly);
    if (!scrolls.isEmpty()) {
        if (clamped != minSize)
            card->setMinimumSize(clamped);
        return;
    }

    QLayout *lay = card->layout();
    if (!lay)
        return;

    // 把卡片原有布局整体挪进内容容器，再套一层滚动区：
    // 布局边距（卡片内边距）跟着内容一起滚动，视觉与原排版一致
    QWidget *inner = new QWidget(card);
    inner->setObjectName(QStringLiteral("appDialogCardContent"));
    inner->setLayout(lay);

    QScrollArea *scroll = new QScrollArea(card);
    scroll->setObjectName(QStringLiteral("appDialogCardScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // 滚动条收细并圆角化：默认样式的方形灰条会顶着卡片圆角边、
    // 与卡内说明区自己的滚动条叠成"双滚动条"，观感很糟
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea#appDialogCardScroll { background: transparent; }"
        "QScrollArea#appDialogCardScroll QScrollBar:vertical {"
        "    background: transparent; width: 6px; margin: 6px 2px; }"
        "QScrollArea#appDialogCardScroll QScrollBar::handle:vertical {"
        "    background: rgba(128, 128, 128, 110); border-radius: 3px; min-height: 30px; }"
        "QScrollArea#appDialogCardScroll QScrollBar::add-line:vertical,"
        "QScrollArea#appDialogCardScroll QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollArea#appDialogCardScroll QScrollBar::add-page:vertical,"
        "QScrollArea#appDialogCardScroll QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollArea#appDialogCardScroll QScrollBar:horizontal {"
        "    background: transparent; height: 6px; margin: 2px 6px; }"
        "QScrollArea#appDialogCardScroll QScrollBar::handle:horizontal {"
        "    background: rgba(128, 128, 128, 110); border-radius: 3px; min-width: 30px; }"
        "QScrollArea#appDialogCardScroll QScrollBar::add-line:horizontal,"
        "QScrollArea#appDialogCardScroll QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollArea#appDialogCardScroll QScrollBar::add-page:horizontal,"
        "QScrollArea#appDialogCardScroll QScrollBar::sub-page:horizontal { background: transparent; }"));
    scroll->setWidget(inner);

    QVBoxLayout *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->setSpacing(0);
    cardLay->addWidget(scroll);

    if (clamped != minSize)
        card->setMinimumSize(clamped);
    card->resize(clamped.boundedTo(card->maximumSize()));
}

QWidget *AppDialogBase::hostWindow() const
{
    QWidget *host = parentWidget() ? parentWidget()->window() : nullptr;
    if (!host)
        host = QApplication::activeWindow();
    if (!host) {
        // 兜底：取当前可见的顶层窗口，避免无 parent / 无活动窗口时定位失效
        const QWidgetList tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops) {
            if (w->isVisible() && w != this) {
                host = w;
                break;
            }
        }
    }
    return host;
}

void AppDialogBase::positionOverWindow()
{
    QWidget *host = hostWindow();
    if (!host || host == this)
        return;

#ifdef Q_OS_ANDROID
    // 覆盖层模式：直接挂到宿主窗口下并铺满其客户区。
    // 宿主窗口旋转/软键盘收放会 Resize，用事件过滤器跟着重新铺满。
    if (parentWidget() != host) {
        const bool wasVisible = isVisible();
        if (wasVisible)
            setVisible(false); // 改父控件会隐式显示，先压住
        setParent(host);
        if (wasVisible)
            setVisible(true);
    }
    setGeometry(QRect(QPoint(0, 0), host->size()));
    host->installEventFilter(this);
    return;
#endif

    // host 客户端区左上角在屏幕上的位置
    const QPoint hostTopLeft = host->mapToGlobal(QPoint(0, 0));
    const QSize hostSize = host->geometry().size();

#ifdef Q_OS_WIN
    // Windows：用 SetWindowPos 以屏幕绝对坐标定位。
    // 不能用 setGeometry 传“相对 parent 控件的本地坐标”：对带 parent 的
    // frameless 顶层弹窗，Qt 的相对坐标会被错误解释，弹窗会落到屏幕左上角
    // 而非精确覆盖宿主窗口（ModelSelectDialog 等不设置位置的弹窗反而正常，
    // 因为 QDialog 默认会把它居中于父窗口）。
    QWindow *win = windowHandle();
    if (!win) {
        (void)winId();          // 强制创建 platform window
        win = windowHandle();
    }
    if (win) {
        HWND hwnd = reinterpret_cast<HWND>(win->winId());
        if (hwnd) {
            SetWindowPos(hwnd, nullptr,
                         hostTopLeft.x(), hostTopLeft.y(),
                         hostSize.width(), hostSize.height(),
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS);
            setAttribute(Qt::WA_Moved, true);
            return;
        }
    }
#endif

    // 回退：对话框可能挂在 host 或其内部子控件下，setGeometry 是相对父控件坐标的，
    // 因此需要把屏幕坐标换算成父控件本地坐标，保证对话框精确覆盖 host 客户端区
    QWidget *p = parentWidget();
    if (p)
        setGeometry(QRect(p->mapFromGlobal(hostTopLeft), hostSize));
    else
        setGeometry(QRect(hostTopLeft, hostSize));

    // 标记为已手动定位，窗口系统/首次显示流程不得把窗口重置到默认位置
    setAttribute(Qt::WA_Moved, true);
}

void AppDialogBase::captureBlurBackground()
{
#ifdef Q_OS_ANDROID
    // 覆盖层模式下宿主抓图会把遮罩自己抓进去（递归变暗），且手机端抓大图
    // 拖慢弹窗打开速度。安卓端直接用纯色暗遮罩，不做模糊背景。
    return;
#endif
    QWidget *host = hostWindow();
    if (!host || host == this)
        return;

    // 宿主窗口最小化或尚未可见时抓图不可靠且会拖慢主线程，
    // 此时跳过模糊背景（仅保留暗色遮罩），避免弹窗表现为"无响应/不可交互"。
    if (host->isMinimized() || !host->isVisible())
        return;

    QPixmap original = host->grab();
    if (original.isNull())
        return;

    // 用屏幕坐标对齐：对话框 (0,0) 相对 host 客户端区 (0,0) 的偏移，
    // 无论对话框实际落在哪里，都能让模糊背景与画面精确对齐
    m_blurOffset = host->mapToGlobal(QPoint(0, 0)) - mapToGlobal(QPoint(0, 0));

    // 高 DPI 安全：QImage 不区分 dpr，尺寸始终是设备像素
    const qreal dpr = original.devicePixelRatio();
    QImage img = original.toImage();
    const QSize devSize = img.size();

    QSize small(devSize.width() / 6, devSize.height() / 6);
    if (small.width() < 1)
        small.setWidth(1);
    if (small.height() < 1)
        small.setHeight(1);

    img = img.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .scaled(devSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    m_blurredBackground = QPixmap::fromImage(img);
    m_blurredBackground.setDevicePixelRatio(dpr);
}

void AppDialogBase::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    if (!m_blurredBackground.isNull())
        painter.drawPixmap(m_blurOffset, m_blurredBackground);
    painter.fillRect(rect(), QColor(0, 0, 0, 90));
}

void AppDialogBase::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    // 显示前已在构造函数/exec 中预定位过；这里再同步校准一次，
    // 并用事件循环延迟重新定位：在 Windows 上首次映射窗口时，
    // showEvent 期间直接 setGeometry 可能被显示流程覆盖，
    // 延迟到事件循环执行可确保弹窗最终精确居中覆盖宿主窗口。
    fitCardToScreen();
    positionOverWindow();
    setAttribute(Qt::WA_Moved, true);

    // 确保弹窗真正获得激活与输入焦点：剪贴板等后台触发场景下
    // 宿主可能处于失焦状态，若不激活则弹窗看似弹出却无法点击。
    raise();
#ifdef Q_OS_ANDROID
    // 覆盖层不是窗口，activateWindow 无意义；要拿到返回键（Esc）事件
    // 必须把焦点收进弹窗自身。
    setFocus(Qt::OtherFocusReason);
#else
    activateWindow();
#endif

    QTimer::singleShot(0, this, [this]() {
        if (!isVisible())
            return;
        positionOverWindow();
        captureBlurBackground();
        update();
    });
}

void AppDialogBase::hideEvent(QHideEvent *event)
{
#ifdef Q_OS_ANDROID
    // 覆盖层模式的弹窗关闭后把焦点还给宿主窗口：返回键、键盘唤起都
    // 走焦点链，焦点若残留在已隐藏的弹窗上，后续按键会全部落空
    if (QWidget *host = hostWindow())
        host->activateWindow();
#endif
    QDialog::hideEvent(event);
}

bool AppDialogBase::eventFilter(QObject *watched, QEvent *event)
{
    // 覆盖层模式：宿主窗口大小变化（旋转、软键盘收放）时跟着重新铺满，
    // 并把居中卡片压回新的可用范围
    if (event->type() == QEvent::Resize && watched == hostWindow()) {
        positionOverWindow();
        fitCardToScreen();
    }
    return QDialog::eventFilter(watched, event);
}

void AppDialogBase::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    if (isVisible()) {
        captureBlurBackground();
        positionOverWindow();
    }
}

void AppDialogBase::mousePressEvent(QMouseEvent *event)
{
    // 点击居中卡片以外的遮罩区域即关闭弹窗
    if (event->button() == Qt::LeftButton) {
        if (QWidget *card = findCardWidget()) {
            if (!card->geometry().contains(event->pos())) {
                reject();
                return;
            }
        }
    }
    QDialog::mousePressEvent(event);
}

void AppDialogBase::keyPressEvent(QKeyEvent *event)
{
    // 安卓系统返回键与 Esc 同语义：关掉当前弹窗。
    // 不处理时返回键会一路穿透到系统层，直接把整个应用退掉
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Back) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}
