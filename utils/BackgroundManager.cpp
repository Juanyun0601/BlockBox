#include "BackgroundManager.h"

#include <QDebug>
#include <QFileInfo>
#include <QMutex>
#include <QSettings>

BackgroundManager* BackgroundManager::m_instance = nullptr;
static QMutex s_bgInstanceMutex;

BackgroundManager::BackgroundManager(QObject *parent)
    : QObject(parent)
    , m_mode(Classic)
    , m_solidColor("#f5f5f5")
    , m_blurRadius(0)
{
    loadFromSettings();
}

BackgroundManager::~BackgroundManager()
{
    saveToSettings();
}

BackgroundManager* BackgroundManager::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&s_bgInstanceMutex);
        if (!m_instance) {
            m_instance = new BackgroundManager();
        }
    }
    return m_instance;
}

BackgroundManager::BackgroundMode BackgroundManager::currentMode() const
{
    return m_mode;
}

void BackgroundManager::setMode(BackgroundMode mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        saveToSettings();
        emit backgroundChanged();
    }
}

QString BackgroundManager::solidColor() const
{
    return m_solidColor;
}

void BackgroundManager::setSolidColor(const QString &color)
{
    if (m_solidColor != color) {
        m_solidColor = color;
        saveToSettings();
        if (m_mode == SolidColor) {
            emit backgroundChanged();
        }
    }
}

QString BackgroundManager::imagePath() const
{
    return m_imagePath;
}

void BackgroundManager::setImagePath(const QString &path)
{
    if (m_imagePath != path) {
        m_imagePath = path;
        saveToSettings();
        if (m_mode == Image) {
            emit backgroundChanged();
        }
    }
}

QString BackgroundManager::bingImagePath() const
{
    return m_bingImagePath;
}

void BackgroundManager::setBingImagePath(const QString &path)
{
    if (m_bingImagePath != path) {
        m_bingImagePath = path;
        saveToSettings();
        if (m_mode == Bing) {
            emit backgroundChanged();
        }
    }
}

int BackgroundManager::blurRadius() const
{
    return m_blurRadius;
}

void BackgroundManager::setBlurRadius(int radius)
{
    radius = qBound(0, radius, 64);
    if (m_blurRadius != radius) {
        m_blurRadius = radius;
        saveToSettings();
        if (m_mode == Image || m_mode == Bing) {
            emit backgroundChanged();
        }
    }
}

QString BackgroundManager::backgroundStyleSheet() const
{
    // 主内容区内的所有页面容器统一透明，让自定义背景（纯色/图片）完整透出。
    // 用通用后代选择器替代逐个页面硬编码：覆盖所有页面及其嵌套堆栈子页，
    // 未来新增页面也无需改动此清单。style.qss 中 `QWidget { background-color: @BG_BASE@ }`
    // 会把未覆盖的页面涂成基色（浅色主题下即白色），导致背景出现白色块。
    QString pageTransparency =
        "QStackedWidget { background: transparent; }\n"
        "QFrame#settingsLeftFrame, QFrame#settingsRightFrame { background: transparent; }\n"
        "QWidget#settingsHeader { background: transparent; }\n"
        "QStackedWidget#settingsContentStack { background: transparent; }\n"
        "QWidget#settingsGeneralContent, QWidget#settingsInterfaceContent,\n"
        "QWidget#settingsGameContent, QWidget#settingsInstanceContent,\n"
        "QWidget#settingsJavaContent, QWidget#settingsAdvancedContent,\n"
        "QWidget#settingsKeyBindContent { background: transparent; }\n"
        "SideBar { background: transparent; }\n"
        // 主堆栈及各页面内的嵌套堆栈：所有直接子页一律透明（含未来新增页面）
        "QWidget#contentWrapper QStackedWidget > QWidget { background: transparent; }\n"
        // 页面内滚动区域与视口透明
        "QWidget#contentWrapper QScrollArea { background: transparent; }\n"
        "QWidget#contentWrapper QScrollArea > QWidget > QWidget { background: transparent; }\n";

    switch (m_mode) {
    case SolidColor:
        return QString(
            "QMainWindow { background: %1; }\n"
            "QWidget#contentWrapper { background: transparent; }\n"
            "%2"
        ).arg(m_solidColor, pageTransparency);

    case Image:
        if (!m_imagePath.isEmpty()) {
            QString escapedPath = m_imagePath;
            escapedPath.replace("\\", "/");
            return QString(
                "QMainWindow { background: url(%1) no-repeat center; }\n"
                "QWidget#contentWrapper { background: transparent; }\n"
                "%2"
            ).arg(escapedPath, pageTransparency);
        }
        return pageTransparency;

    case FlowLight:
        // 流光模式：由 BackgroundWidget 绘制动画，页面保持透明透出背景
        return QString(
            "QMainWindow { background: #F8FAFC; }\n"
            "QWidget#contentWrapper { background: transparent; }\n"
            "%1"
        ).arg(pageTransparency);

    case Rotating:
        // 旋转全景模式：由 BackgroundWidget 绘制缓慢旋转的图片，页面保持透明透出背景
        return QString(
            "QMainWindow { background: #0F172A; }\n"
            "QWidget#contentWrapper { background: transparent; }\n"
            "%1"
        ).arg(pageTransparency);

    case Bing:
        // 必应壁纸模式：由 BackgroundWidget 绘制下载的壁纸，页面保持透明透出背景。
        // 深色底可避免壁纸下载完成前的白屏闪烁
        return QString(
            "QMainWindow { background: #0F172A; }\n"
            "QWidget#contentWrapper { background: transparent; }\n"
            "%1"
        ).arg(pageTransparency);

    case Classic:
    default:
        return QString();
    }
}

void BackgroundManager::loadFromSettings()
{
    QSettings settings("BlockBox", "Settings");
    m_mode = static_cast<BackgroundMode>(settings.value("background/mode", static_cast<int>(Classic)).toInt());
    m_solidColor = settings.value("background/solidColor", "#f5f5f5").toString();
    m_imagePath = settings.value("background/imagePath", "").toString();
    m_bingImagePath = settings.value("background/bingPath", "").toString();
    m_blurRadius = settings.value("background/blurRadius", 0).toInt();
}

void BackgroundManager::saveToSettings()
{
    QSettings settings("BlockBox", "Settings");
    settings.setValue("background/mode", static_cast<int>(m_mode));
    settings.setValue("background/solidColor", m_solidColor);
    settings.setValue("background/imagePath", m_imagePath);
    settings.setValue("background/bingPath", m_bingImagePath);
    settings.setValue("background/blurRadius", m_blurRadius);
}
