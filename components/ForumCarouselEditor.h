/**
 * @file   ForumCarouselEditor.h
 * @brief  帖子轮播图编辑器：添加图床图片 / B 站视频，排序、删除一条龙
 * @author BlockBox Team
 * @date   2026-11-21
 *
 * 发布页（资源 / 导航模式）和帖子详情页的「编辑轮播」对话框共用这个控件：
 *   - 图片：多选本地图片，逐张走图床上传（自动压缩），条目上就地显示进度；
 *   - B 站视频：粘贴 bilibili.com/video/BV… 链接，提取 BV 号规范化保存；
 *   - 每条可以左右调序、删除；发帖时把就绪条目按当前顺序交给 ForumContentService。
 */

#pragma once

#include <QVector>
#include <QWidget>

#include "utils/ForumContentService.h"

class QLabel;
class QVBoxLayout;

/** 轮播编辑器里的一条待发条目（key 是界面上的身份证，跨重建保持稳定） */
struct ForumCarouselEntry
{
    int key = 0;
    ForumCarouselItem::Type type = ForumCarouselItem::Image;
    QString filePath;    ///< image：本地文件路径（图床上传前有值，外链为空）
    int uploadId = 0;    ///< image：上传回执的图床 id（> 0 即上传完成，外链为 0）
    QString url;         ///< image：图床路径或外链完整地址；bilibili：规范化 BV 链接
    QString thumb;       ///< image：图床缩略图路径（可能为空）；bilibili：空
    bool uploading = false;

    bool ready() const
    {
        return type == ForumCarouselItem::Bilibili ? !url.isEmpty()
                                                   : (uploadId > 0 || !url.isEmpty());
    }
};

class ForumCarouselEditor : public QWidget
{
    Q_OBJECT

public:
    /// 与服务端 MAX_CAROUSEL_ITEMS 一致：一帖轮播最多几条
    static const int kMaxItems = 8;

    explicit ForumCarouselEditor(QWidget *parent = nullptr);

    /** 就绪条目按当前顺序转成发帖用的 ForumCarouselItem（上传中 / 未就绪的不带上） */
    QVector<ForumCarouselItem> items() const;
    /** 还有图片正在上传（宿主在提交前用它拦一下） */
    bool hasPendingUploads() const;
    /** 一条就绪条目都没有 */
    bool isEmpty() const;
    /** 条目总数（含上传中的） */
    int count() const { return m_entries.size(); }
    /** 清空全部条目（重置表单时用） */
    void clear();
    /** 用已就绪的条目填充（编辑已有帖子的轮播时用，图片给 uploadId/url/thumb 即可） */
    void setItems(const QVector<ForumCarouselItem> &items);

signals:
    /** 条目增删 / 调序 / 上传状态变化后发出，宿主可以刷新提交按钮的可用性 */
    void changed();

private:
    void addImageFiles();
    void addExternalImage();
    void addBilibiliVideo();
    void uploadEntry(int key);
    void removeEntry(int key);
    void moveEntry(int key, int delta);
    void rebuildRows();
    void applyThumbnail(QLabel *label, const ForumCarouselEntry &entry);

    QVBoxLayout *m_rowsLayout = nullptr;
    QLabel *m_hintLabel = nullptr;
    QVector<ForumCarouselEntry> m_entries;
    int m_keySeed = 0;
};
