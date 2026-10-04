#include "AppDialogBase.h"

#include <QApplication>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

AppDialogBase::AppDialogBase(QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::WindowModal);

    // 清除全局 QSS 中 QDialog 通用规则的背景/边框影响：
    // 弹窗自身必须透明，模糊背景 + 暗色遮罩由基类 paintEvent 绘制。
    // （子类若调用 setStyleSheet 会整体覆盖此处，同样不再受全局 QDialog 规则影响）
    setStyleSheet(QStringLiteral("QDialog { background-color: transparent; border: none; }"));

    // 首次显示前先按宿主窗口预定位，并标记为“已手动定位”，
    // 避免无边框对话框以默认位置（屏幕左上角）映射，超出软件范围。
    positionOverWindow();
    setAttribute(Qt::WA_Moved, true);
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
    positionOverWindow();
    setAttribute(Qt::WA_Moved, true);

    // 确保弹窗真正获得激活与输入焦点：剪贴板等后台触发场景下
    // 宿主可能处于失焦状态，若不激活则弹窗看似弹出却无法点击。
    raise();
    activateWindow();

    QTimer::singleShot(0, this, [this]() {
        if (!isVisible())
            return;
        positionOverWindow();
        captureBlurBackground();
        update();
    });
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
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}
