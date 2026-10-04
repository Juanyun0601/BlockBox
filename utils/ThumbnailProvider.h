/**
 * @file   ThumbnailProvider.h
 * @brief  图片缩略图生成器 - 支持多种图片格式（通过Windows Shell API或SageThumbs）
 * @author BlockBox Team
 * @date   2026-08-28
 */
#ifndef THUMBNAILPROVIDER_H
#define THUMBNAILPROVIDER_H

#include <QString>
#include <QPixmap>
#include <QSize>

class ThumbnailProvider
{
public:
    enum Backend
    {
        Auto,       // 自动选择最佳后端
        Qt,         // 仅使用Qt内置图片加载器
        Shell,      // 使用Windows Shell API（IShellItemImageFactory）
        SageThumbs  // 直接调用SageThumbs库
    };

    /**
     * 获取文件缩略图
     * @param filePath 文件路径
     * @param maxSize 缩略图最大尺寸（宽或高）
     * @param backend 后端选择（默认自动）
     * @return 缩略图，如果失败返回空QPixmap
     */
    static QPixmap getThumbnail(const QString &filePath, int maxSize = 48, Backend backend = Auto);

    /**
     * 检查文件是否为支持的图片格式
     * @param filePath 文件路径
     * @return 是否支持
     */
    static bool isImageFile(const QString &filePath);

    /**
     * 获取支持的图片格式列表
     * @return 格式扩展名列表（不含点）
     */
    static QStringList supportedImageFormats();

    /**
     * 设置SageThumbs库路径
     * @param dllPath SageThumbs.dll的完整路径
     * @return 是否加载成功
     */
    static bool setSageThumbsLibraryPath(const QString &dllPath);

private:
    ThumbnailProvider() = delete;
    static void *s_sageThumbsDll;
    typedef int (*SageThumbsGetThumbnailFunc)(const wchar_t *filePath, int maxSize, void **bitmap);
    static SageThumbsGetThumbnailFunc s_sageThumbsGetThumbnail;
};

#endif // THUMBNAILPROVIDER_H