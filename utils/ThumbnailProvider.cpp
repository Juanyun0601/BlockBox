/**
 * @file   ThumbnailProvider.cpp
 * @brief  图片缩略图生成器实现
 * @author BlockBox Team
 * @date   2026-08-28
 */
#include "ThumbnailProvider.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <QImage>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

#ifndef SIIGBF_THUMBNAIL
#define SIIGBF_THUMBNAIL 0x00000000
#endif
#ifndef SIIGBF_ICONOVERLAY
#define SIIGBF_ICONOVERLAY 0x00000008
#endif

static QPixmap hbitmapToQPixmap(HBITMAP hBitmap)
{
    if (!hBitmap)
        return QPixmap();

    BITMAP bmp;
    if (!GetObject(hBitmap, sizeof(BITMAP), &bmp))
        return QPixmap();

    int width = bmp.bmWidth;
    int height = bmp.bmHeight;

    HDC hdc = GetDC(nullptr);
    HDC hMemDC = CreateCompatibleDC(hdc);
    HBITMAP hOld = (HBITMAP)SelectObject(hMemDC, hBitmap);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QImage image(width, height, QImage::Format_ARGB32);
    GetDIBits(hMemDC, hBitmap, 0, height, image.bits(), &bmi, DIB_RGB_COLORS);

    SelectObject(hMemDC, hOld);
    DeleteDC(hMemDC);
    ReleaseDC(nullptr, hdc);

    return QPixmap::fromImage(image);
}
#endif

#include <QFileInfo>
#include <QImageReader>
#include <QLoggingCategory>
#include <QLibrary>

Q_LOGGING_CATEGORY(lcThumbnail, "blockbox.thumbnail")

// 静态成员变量定义
void *ThumbnailProvider::s_sageThumbsDll = nullptr;
ThumbnailProvider::SageThumbsGetThumbnailFunc ThumbnailProvider::s_sageThumbsGetThumbnail = nullptr;

#ifdef Q_OS_WIN
/**
 * 使用Windows Shell API获取缩略图
 * @param filePath 文件路径
 * @param maxSize 最大尺寸
 * @return 缩略图
 */
static QPixmap getShellThumbnail(const QString &filePath, int maxSize)
{
    // 初始化COM（如果尚未初始化）
    static HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        qCCritical(lcThumbnail) << "Failed to initialize COM:" << hr;
        return QPixmap();
    }

    // 将文件路径转换为宽字符路径
    wchar_t wPath[MAX_PATH];
    if (filePath.toWCharArray(wPath) >= MAX_PATH) {
        qCCritical(lcThumbnail) << "File path too long";
        return QPixmap();
    }

    // 创建ShellItem
    IShellItem *pShellItem = nullptr;
    hr = SHCreateItemFromParsingName(wPath, nullptr, IID_PPV_ARGS(&pShellItem));
    if (FAILED(hr) || !pShellItem) {
        qCWarning(lcThumbnail) << "Failed to create shell item for:" << filePath;
        return QPixmap();
    }

    // 获取IShellItemImageFactory接口
    IShellItemImageFactory *pImageFactory = nullptr;
    hr = pShellItem->QueryInterface(IID_PPV_ARGS(&pImageFactory));
    if (FAILED(hr) || !pImageFactory) {
        qCWarning(lcThumbnail) << "Failed to get image factory for:" << filePath;
        pShellItem->Release();
        return QPixmap();
    }

    // 设置缩略图尺寸
    SIZE size = {maxSize, maxSize};

    // 获取缩略图
    HBITMAP hBitmap = nullptr;
    hr = pImageFactory->GetImage(size, SIIGBF_THUMBNAIL | SIIGBF_ICONOVERLAY, &hBitmap);
    if (FAILED(hr) || !hBitmap) {
        qCWarning(lcThumbnail) << "Failed to get thumbnail for:" << filePath;
        pImageFactory->Release();
        pShellItem->Release();
        return QPixmap();
    }

    // 将HBITMAP转换为QPixmap
    QPixmap result = hbitmapToQPixmap(hBitmap);
    if (result.isNull()) {
        qCWarning(lcThumbnail) << "Failed to convert HBITMAP to QPixmap";
        DeleteObject(hBitmap);
        pImageFactory->Release();
        pShellItem->Release();
        return QPixmap();
    }

    // 清理资源（QPixmap已复制数据）
    DeleteObject(hBitmap);
    pImageFactory->Release();
    pShellItem->Release();

    return result;
}
#endif

bool ThumbnailProvider::setSageThumbsLibraryPath(const QString &dllPath)
{
    if (dllPath.isEmpty()) {
        qCWarning(lcThumbnail) << "SageThumbs library path is empty";
        return false;
    }

    // 如果已经加载，先卸载
    if (s_sageThumbsDll) {
        QLibrary *lib = static_cast<QLibrary*>(s_sageThumbsDll);
        lib->unload();
        delete lib;
        s_sageThumbsDll = nullptr;
        s_sageThumbsGetThumbnail = nullptr;
    }

    QLibrary *lib = new QLibrary(dllPath);
    if (!lib->load()) {
        qCWarning(lcThumbnail) << "Failed to load SageThumbs library:" << lib->errorString();
        delete lib;
        return false;
    }

    // 获取函数指针
    // 注意：实际函数名需要根据SageThumbs的导出符号确定
    // 这里假设函数名为 "SageThumbs_GetThumbnail"
    s_sageThumbsGetThumbnail = reinterpret_cast<SageThumbsGetThumbnailFunc>(
        lib->resolve("SageThumbs_GetThumbnail"));

    if (!s_sageThumbsGetThumbnail) {
        qCWarning(lcThumbnail) << "Failed to resolve SageThumbs_GetThumbnail function";
        lib->unload();
        delete lib;
        return false;
    }

    s_sageThumbsDll = lib;
    qCInfo(lcThumbnail) << "SageThumbs library loaded successfully:" << dllPath;
    return true;
}

QPixmap ThumbnailProvider::getThumbnail(const QString &filePath, int maxSize, Backend backend)
{
    if (filePath.isEmpty()) {
        return QPixmap();
    }

    // 检查文件是否存在
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qCWarning(lcThumbnail) << "File does not exist:" << filePath;
        return QPixmap();
    }

    // 根据后端选择实现
    switch (backend) {
    case Qt:
        // 仅使用Qt内置图片加载器
        break;

    case Shell:
#ifdef Q_OS_WIN
        // 仅使用Windows Shell API
        {
            QPixmap shellThumb = getShellThumbnail(filePath, maxSize);
            if (!shellThumb.isNull()) {
                return shellThumb;
            }
        }
#endif
        break;

    case SageThumbs:
#ifdef Q_OS_WIN
        // 直接调用SageThumbs库
        if (s_sageThumbsGetThumbnail) {
            wchar_t wPath[MAX_PATH];
            if (filePath.toWCharArray(wPath) < MAX_PATH) {
                void *bitmap = nullptr;
                int result = s_sageThumbsGetThumbnail(wPath, maxSize, &bitmap);
                if (result == 0 && bitmap) {
                    // 假设bitmap是HBITMAP
                    HBITMAP hBitmap = static_cast<HBITMAP>(bitmap);
                    QPixmap pix = hbitmapToQPixmap(hBitmap);
                    DeleteObject(hBitmap);
                    if (!pix.isNull()) {
                        return pix;
                    }
                }
            }
        }
#endif
        break;

    case Auto:
    default:
        // 自动选择：先尝试Qt，再尝试Shell，最后尝试SageThumbs
        // 1. Qt内置
        {
            QPixmap pix(filePath);
            if (!pix.isNull()) {
                return pix.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        }

#ifdef Q_OS_WIN
        // 2. Windows Shell API
        {
            QPixmap shellThumb = getShellThumbnail(filePath, maxSize);
            if (!shellThumb.isNull()) {
                return shellThumb;
            }
        }
#endif

        // 3. SageThumbs
#ifdef Q_OS_WIN
        if (s_sageThumbsGetThumbnail) {
            wchar_t wPath[MAX_PATH];
            if (filePath.toWCharArray(wPath) < MAX_PATH) {
                void *bitmap = nullptr;
                int result = s_sageThumbsGetThumbnail(wPath, maxSize, &bitmap);
                if (result == 0 && bitmap) {
                    HBITMAP hBitmap = static_cast<HBITMAP>(bitmap);
                    QPixmap pix = hbitmapToQPixmap(hBitmap);
                    DeleteObject(hBitmap);
                    if (!pix.isNull()) {
                        return pix;
                    }
                }
            }
        }
#endif
        break;
    }

    qCDebug(lcThumbnail) << "Failed to load thumbnail for:" << filePath;
    return QPixmap();
}

bool ThumbnailProvider::isImageFile(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }

    // 检查文件扩展名
    QString suffix = fileInfo.suffix().toLower();
    QStringList imageFormats = supportedImageFormats();
    return imageFormats.contains(suffix);
}

QStringList ThumbnailProvider::supportedImageFormats()
{
    // Qt默认支持的格式
    QStringList formats;
    formats << "png" << "jpg" << "jpeg" << "gif" << "bmp" << "ico" << "svg";

    // Qt可选支持的格式（如果编译了相应插件）
    #if defined(QT_NO_IMAGEFORMAT_JPEG)
    formats.removeAll("jpg");
    formats.removeAll("jpeg");
    #endif

    #if defined(QT_NO_IMAGEFORMAT_GIF)
    formats.removeAll("gif");
    #endif

    #if defined(QT_NO_IMAGEFORMAT_BMP)
    formats.removeAll("bmp");
    #endif

    // Windows Shell支持的其他格式（通过SageThumbs等）
    // 这些格式可能需要额外的库支持
    formats << "webp" << "tiff" << "tif" << "psd" << "raw" << "cr2" << "nef" << "arw"
            << "dng" << "ppm" << "pgm" << "pbm" << "tga" << "exr" << "hdr";

    return formats;
}