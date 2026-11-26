#ifndef RESOURCESPAGE_H
#define RESOURCESPAGE_H

#include <QColor>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPixmap>
#include <QList>

#include "utils/PageFilter.h"

class ResourcesPage : public QWidget
{
    Q_OBJECT

public:
    explicit ResourcesPage(QWidget *parent = nullptr);
    ~ResourcesPage();

    void setInstancePath(const QString &path);

    /**
     * @brief 切换 Java 版 / 基岩版模式，重建页面卡片
     *
     * 两个版本共用同一套卡片样式（对齐原型 resource-card），
     * 仅卡片内容、分组与 childIndex 语义不同（路由见 MainWindow::onChildNavClicked）：
     *  - 基岩版 0: 安装新实例；1~4: 资源包/材质包/地图/脚本；5: 新建收藏夹；>=6: 收藏夹
     */
    void setBedrockMode(bool bedrock);
    bool isBedrockMode() const { return m_bedrockMode; }

    /**
     * 子导航搜索框分发：按标题/描述过滤资源入口卡片与收藏夹卡片；
     * 分组内全部不匹配时连同分组标题一并隐藏。
     */
    Q_INVOKABLE void applyPageFilter(const QString &query);

signals:
    // 统一导航信号：childIndex 与左侧导航项索引一致
    // Java 版：0~2: 安装新实例 / 下载整合包 / 导入整合包
    //          3~8: 模组 / 数据包 / 资源包 / 光影包 / 世界 / 皮肤
    //          9~10: 投影 / 插件；11: 新建收藏夹；>=12: 各收藏夹（childIndex - 12 为收藏夹索引）
    // 基岩版：0: 安装新实例；1~4: 资源包 / 材质包 / 地图 / 脚本
    //         5:   新建收藏夹；>=6: 各收藏夹（childIndex - 6 为收藏夹索引）
    void navItemClicked(int childIndex);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

public:
    // 从本地文件提取图标（供其他页面复用）
    static QPixmap extractIconFromZip(const QString &zipPath, const QString &entryName);
    static QPixmap extractIconFromDir(const QString &dirPath, const QString &iconName);
    static QPixmap loadImageThumbnail(const QString &imagePath);
    static QPixmap extractModIcon(const QString &jarPath);
    static QPixmap extractResourcePackIcon(const QString &zipPath);
    static QPixmap extractSaveIcon(const QString &saveDirPath);

private:
    void initUI();
    void rebuildPageCards();
    void rebuildFavoriteCards();
    void applyCurrentFilter();

    // 分区标题（渐变竖条 + 标题，右侧可挂附加控件，如“新建收藏夹”按钮）
    QWidget *createSectionHeader(const QString &title, QWidget *extraRight = nullptr);
    // 横向资源卡片（渐变图标块 + 标题/描述 + 右侧箭头，对齐原型 resource-card）
    QWidget *createResourceCard(const QString &title, const QString &desc,
                                const QColor &grad1, const QColor &grad2,
                                const QString &iconPath, int childIndex);
    // 绘制渐变圆角图标块（带柔和底部阴影 + 白色图标）
    static QPixmap makeGradientIconPixmap(const QColor &c1, const QColor &c2,
                                          const QString &iconPath, int size = 52);

    QWidget *m_scrollContent;
    QVBoxLayout *m_mainLayout = nullptr;   // 页面主布局（标题 + 卡片行 + 收藏夹），模式切换时全量重建
    QWidget *m_favoritesContainer;   // 收藏夹卡片容器（动态刷新）
    QVBoxLayout *m_favoritesLayout;

    QString m_instancePath;
    bool m_bedrockMode = false;      // 当前是否为基岩版模式（卡片内容随版本切换）

    // ── 子导航搜索：卡片过滤 + 分组标题联动隐藏 ──
    QString m_pageFilterQuery;
    PageFilter::VisibilityFilter m_pageFilter;
};

#endif // RESOURCESPAGE_H
