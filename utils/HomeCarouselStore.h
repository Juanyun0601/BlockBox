#ifndef HOMECAROUSELSTORE_H
#define HOMECAROUSELSTORE_H

#include <QCoreApplication>
#include <QString>
#include <QStringList>

/**
 * @file   HomeCarouselStore.h
 * @brief  首页轮播图自定义图片仓库
 *
 * 首页（展示）与设置页（管理）共用的存储层：
 * - 自定义列表持久化在设置项 home/carouselImages
 * - 图片统一导入到应用数据目录下的 home_carousel/（缩放 + 重新编码，原图不动）
 */
class HomeCarouselStore
{
    Q_DECLARE_TR_FUNCTIONS(HomeCarouselStore)

public:
    /** 自定义图片存放目录（不存在时按需创建） */
    static QString imageDir();

    /** 文件选择框用的图片过滤器 */
    static QString imageFileFilter();

    /** 读取已保存的自定义列表（不校验文件是否存在） */
    static QStringList savedImages();

    /** 写入自定义列表 */
    static void saveImages(const QStringList &paths);

    /** 过滤掉已删除 / 无法解码的条目，保持原有顺序 */
    static QStringList existingImages(const QStringList &paths);

    /**
     * @brief 把本地图片导入到轮播目录，返回导入后的路径
     *
     * 文件名取源文件内容哈希：重复导入同一张图会复用同一个文件。
     * 长边超过 2560 会先缩小；带透明通道存 PNG，其余存 JPEG。
     * 读取失败或格式不受支持时返回空字符串。
     */
    static QString importImage(const QString &srcPath);

    /** 删除由本仓库管理的图片副本（不在管理目录内的文件不会被删除） */
    static void removeImageFile(const QString &path);

    /** 清空自定义列表并删除全部图片副本 */
    static void clearCustom();

private:
    static bool isManagedFile(const QString &path);
};

#endif // HOMECAROUSELSTORE_H
