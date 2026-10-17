/**
 * @file   SchematicCoverRenderer.h
 * @brief  投影封面等距 3D 渲染器（支持实例/资源包材质）
 * @author BlockBox Team
 * @date 2026-10-16
 *
 * 将 .litematic / .schematic / .schem 投影渲染为等距（isometric）体素缩略图，
 * 供本地投影列表瀑布流封面展示。
 *
 * 实现思路参考（特此致谢，详见 设置→关于→参考开源项目）：
 *   - EngineHub/SchematicWebViewer（MIT）
 *       https://github.com/EngineHub/SchematicWebViewer
 *       · 遮挡剔除：仅当 6 邻均为「实心且遮挡」方块才跳过；
 *         非遮挡方块列表（叶子/玻璃/台阶等）使用其 nonOccluding.json
 *       · 材质来源：客户端 jar + resourcepacks 资源包叠加
 *   - EndingCredits/litematic-viewer（基于 Deepslate）
 *       https://github.com/EndingCredits/litematic-viewer
 *       · .litematic NBT/BlockStates 位打包解析与预览流程
 *   - maruohon/litematica
 *       https://github.com/maruohon/litematica
 *       · 投影文件格式规范
 *
 * 渲染为 2D 等距画家算法（QPainter），非引入第三方 3D 引擎，
 * 保证启动器体积与稳定性。
 */
#pragma once

#include <QAtomicInt>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

/**
 * @brief 封面渲染完成回调（始终在主线程调用）
 * @param filePath 源投影路径
 * @param image    渲染结果（可能为空表示失败/跳过）
 * @param textured true=带材质细节，false=色块轮廓
 */
using SchematicCoverCallback =
    std::function<void(const QString &filePath, const QImage &image, bool textured)>;

class SchematicCoverRenderer : public QObject
{
    Q_OBJECT
public:
    /** 封面渲染结果（跨线程信号参数） */
    struct CoverResult
    {
        QString filePath;
        QImage image;
        bool textured = false;
        bool done = true;   ///< true=本次请求结束（可释放 inflight）；色块中间态为 false
    };

    explicit SchematicCoverRenderer(QObject *parent = nullptr);
    ~SchematicCoverRenderer() override;

    /** 进程内单例（由首次请求创建，挂在 qApp 上） */
    static SchematicCoverRenderer *instance();

    /**
     * @brief 设置材质来源实例路径（投影列表页在 setInstancePath 时调用）
     *
     * 可传实例根或 versions/<ver> 目录；内部会自动查找客户端 jar 与 resourcepacks。
     * 更换实例后缓存 key 变化，封面会按新材质重新渲染。
     */
    void setTextureInstance(const QString &instancePath);
    QString textureInstance() const { return m_textureInstance; }

    /**
     * @brief 异步请求渲染封面
     * @param filePath 投影文件绝对路径
     * @param width    目标位图宽（建议 512）
     * @param height   目标位图高（建议 296，便于瀑布流横幅裁剪）
     * @param callback 主线程回调；null 也允许（仅写缓存）
     *
     * 同一路径的重复请求会合并。
     * 通常先收到 textured=false 的色块版，再（若有材质）收到 textured=true 的细化版。
     */
    void request(const QString &filePath, int width, int height,
                 SchematicCoverCallback callback = nullptr);

    /** 同步渲染（测试/探针用；生产请走 request）。instancePath 可空。 */
    static QImage renderSync(const QString &filePath, int width, int height,
                             const QString &instancePath = QString(),
                             bool textured = true);

    /** 磁盘缓存路径（无缓存返回空）。instancePath 参与 key。 */
    static QString cacheFilePath(const QString &filePath, int width, int height,
                                 const QString &instancePath = QString());

signals:
    /** 工作线程发出，主线程接收后分发 callback */
    void coverReady(const SchematicCoverRenderer::CoverResult &result);

private slots:
    void onCoverReady(const SchematicCoverRenderer::CoverResult &result);

private:
    struct Pending
    {
        SchematicCoverCallback callback;
        int width = 0;
        int height = 0;
    };

    /** 工作线程：加载 + 采样 + 等距绘制 + 写盘 */
    static QImage renderInWorker(const QString &filePath, int width, int height,
                                 const QString &instancePath, bool textured);

    /** 从 .litematic 等文件加载非空气体素并投影绘制 */
    static QImage renderFromDocument(const QString &filePath, int width, int height,
                                     const QString &instancePath, bool textured);

    QHash<QString, QList<Pending>> m_pending;  ///< filePath -> 待回调
    QSet<QString> m_inflight;                   ///< 已在渲染中的路径（合并请求）
    QString m_textureInstance;                  ///< 材质来源实例路径
    QAtomicInt m_jobSeq{0};                     ///< 任务序号：实例变更后丢弃过期结果
};

Q_DECLARE_METATYPE(SchematicCoverRenderer::CoverResult)
