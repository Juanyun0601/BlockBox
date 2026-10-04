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

class ResourcesPage : public QWidget
{
    Q_OBJECT

public:
    explicit ResourcesPage(QWidget *parent = nullptr);
    ~ResourcesPage();

    void setInstancePath(const QString &path);

signals:
    // 统一导航信号：childIndex 与左侧导航项索引一致
    // 0~2: 安装新实例 / 下载整合包 / 导入整合包
    // 3~7: 模组 / 数据包 / 资源包 / 光影包 / 世界
    // 8:   新建收藏夹
    // >=9: 各收藏夹（childIndex - 9 为收藏夹索引）
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
    void rebuildFavoriteCards();

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
    QWidget *m_favoritesContainer;   // 收藏夹卡片容器（动态刷新）
    QVBoxLayout *m_favoritesLayout;

    QString m_instancePath;
};

#endif // RESOURCESPAGE_H
