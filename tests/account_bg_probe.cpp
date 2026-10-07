/**
 * @file   account_bg_probe.cpp
 * @brief  账户管理页中间皮肤预览"透出设置页自定义背景"验证探针
 *
 * 复刻主窗口背景结构（品红背景层 + 页面铺满其上），按模式预置 BackgroundManager
 * 设置后创建 AccountManagePage，采样皮肤预览控件角落像素并截图：
 *   solid  — 以自定义纯色背景（品红 #FF20E0）创建：预览区角落应为品红（透出）
 *   classic— 以 Classic 背景创建：预览区角落应为主题色（非品红）
 *   live   — 以 Classic 创建，1s 后切到自定义纯色背景：预览区随后应透出品红
 *
 * 输出：tests/account_bg_probe_<mode>.png；退出码 0=符合预期，1=不符合。
 * 构建：本探针复用 build-win-release 的 release 目标文件链接（去掉 main.o，
 * 加入本文件编译出的 .o，库参数与主程序链接行一致）。
 * 注意：探针会改写 QSettings("BlockBox","Settings") 的 background 键组与
 * skinModelBackground，运行前请备份注册表 HKCU\Software\BlockBox，结束后恢复。
 */
#include "pages/AccountManagePage.h"
#include "utils/BackgroundManager.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWidget>

#include <cstdio>

namespace {

// 模拟主窗口 BackgroundWidget 的自定义背景层
class MagentaLayer : public QWidget
{
public:
    explicit MagentaLayer(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xFF, 0x20, 0xE0));
    }
};

// 顶层窗口：品红背景层在底，账户管理页铺满其上
class ProbeWindow : public QWidget
{
public:
    ProbeWindow()
    {
        setWindowTitle(QStringLiteral("account bg probe"));
        m_bg = new MagentaLayer(this);
        m_page = new AccountManagePage(this);
        // 无账户时页面会切到空态并隐藏皮肤预览，强制显示以便采样
        if (QWidget *empty = m_page->findChild<QWidget *>(QStringLiteral("accountEmptyState")))
            empty->hide();
        if (QWidget *skin = m_page->findChild<QWidget *>(QStringLiteral("skin3DWidget")))
            skin->show();
        resize(1100, 720);
    }

    QWidget *skin() const
    {
        return m_page->findChild<QWidget *>(QStringLiteral("skin3DWidget"));
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        m_bg->setGeometry(rect());
        m_bg->lower();
        m_page->setGeometry(rect());
    }

private:
    MagentaLayer *m_bg = nullptr;
    AccountManagePage *m_page = nullptr;
};

bool isMagenta(const QColor &c)
{
    return c.red() > 200 && c.blue() > 150 && c.green() < 120;
}

} // namespace

int main(int argc, char **argv)
{
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral("solid");

    // 与主程序一致的默认表面格式（Skin3DWidget 使用 330 core 着色器）
    QSurfaceFormat glFormat;
    glFormat.setVersion(3, 3);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
    glFormat.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(glFormat);

    // 备份将被改写的设置键，结束后还原
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("Settings"));
    const QVariant oldMode = settings.value(QStringLiteral("background/mode"));
    const QVariant oldColor = settings.value(QStringLiteral("background/solidColor"));
    const QVariant oldSkinBg = settings.value(QStringLiteral("skinModelBackground"));
    auto restore = [&settings, &oldMode, &oldColor, &oldSkinBg]() {
        if (oldMode.isValid())
            settings.setValue(QStringLiteral("background/mode"), oldMode);
        else
            settings.remove(QStringLiteral("background/mode"));
        if (oldColor.isValid())
            settings.setValue(QStringLiteral("background/solidColor"), oldColor);
        if (oldSkinBg.isValid())
            settings.setValue(QStringLiteral("skinModelBackground"), oldSkinBg);
        else
            settings.remove(QStringLiteral("skinModelBackground"));
        settings.sync();
    };

    const bool startCustom = (mode != QStringLiteral("classic"));
    settings.setValue(QStringLiteral("background/mode"),
                      startCustom ? int(BackgroundManager::SolidColor)
                                  : int(BackgroundManager::Classic));
    settings.setValue(QStringLiteral("background/solidColor"), QStringLiteral("#FF20E0"));
    settings.setValue(QStringLiteral("skinModelBackground"), QStringLiteral("theme"));
    settings.sync();

    QApplication app(argc, argv);

    ProbeWindow win;
    win.show();

    if (mode == QStringLiteral("live"))
    {
        QTimer::singleShot(1000, []() {
            BackgroundManager::instance()->setSolidColor(QStringLiteral("#FF20E0"));
            BackgroundManager::instance()->setMode(BackgroundManager::SolidColor);
        });
    }

    QTimer::singleShot(2500, [&]() {
        QWidget *skin = win.skin();
        QColor corner;
        QPoint topLeft;
        if (skin)
        {
            topLeft = skin->mapTo(&win, QPoint(0, 0));
            const QImage frame = win.grab().toImage();
            // 预览控件角落（模型居中，角落应为背景）与控件外一像素（对照）
            corner = frame.pixelColor(topLeft + QPoint(24, 24));
            const QString png = QStringLiteral("account_bg_probe_%1.png").arg(mode);
            frame.save(png);
            fprintf(stderr, "[probe] mode=%s corner=(%d,%d,%d) png=%s\n",
                    qPrintable(mode), corner.red(), corner.green(), corner.blue(),
                    qPrintable(png));
        }
        else
        {
            fprintf(stderr, "[probe] skin3DWidget not found\n");
        }

        const bool expectMagenta = (mode != QStringLiteral("classic"));
        const bool pass = skin && (isMagenta(corner) == expectMagenta);
        fprintf(stderr, "[probe] expect=%s actual=%s -> %s\n",
                expectMagenta ? "magenta" : "non-magenta",
                isMagenta(corner) ? "magenta" : "non-magenta",
                pass ? "PASS" : "FAIL");
        restore();
        std::fflush(stderr);
        app.exit(pass ? 0 : 1);
    });

    const int rc = app.exec();
    restore();
    return rc;
}
