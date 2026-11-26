/**
 * @file   CoverImageCache.h
 * @brief  个人主页封面图的本机缓存
 * @author BlockBox Team
 * @date   2026-11-20
 *
 * 站内封面地址（/uploads/cover-…）每上传一次就是一个新文件，内容不会再变，
 * 所以第一张下回来就存进本机缓存：之后重开软件、来回切页面、甚至断网时
 * 都能立刻把封面画出来，不用每次都等网络重新下一遍。
 */

#pragma once

#include <QImage>
#include <QString>

class CoverImageCache
{
public:
    /**
     * @brief 读本机缓存的封面
     * @param absoluteUrl 封面完整地址
     * @return 缓存的封面；没有缓存或文件读不出来时返回空图
     */
    static QImage load(const QString &absoluteUrl);

    /** 把刚下载到的封面存进本机缓存（最多留 40 张，更旧的自动清掉） */
    static void save(const QString &absoluteUrl, const QImage &image);

    /** 删掉某张封面的缓存（换封面、删封面后调用） */
    static void remove(const QString &absoluteUrl);

    /** 清空全部封面缓存（退出登录、清理缓存时调用） */
    static void clear();

private:
    static QString cacheDirectory();
    static QString cacheFilePath(const QString &absoluteUrl);
};
