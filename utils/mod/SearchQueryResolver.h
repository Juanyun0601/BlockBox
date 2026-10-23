#ifndef SEARCHQUERYRESOLVER_H
#define SEARCHQUERYRESOLVER_H

#include <QObject>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QJsonObject;

/**
 * @brief 把中文搜索词解析成 CurseForge / Modrinth 能识别的英文名称
 *
 * CurseForge 与 Modrinth 的检索接口只认英文，用户输入「机械动力」时只能拿到空结果。
 * 本类负责把中文关键词换成对应的英文项目名，解析顺序为：
 *   1. 内置词表（resources 里的 mcmod_names.json，英文名 → 中文名，此处反向使用）
 *   2. MC 百科在线搜索（mcmod-api），把中文名解析成英文项目名
 *   3. 中英混输时直接取其中的英文部分
 * 解析结果会写入内存缓存与本地磁盘缓存，重复搜索同一关键词无需联网。
 */
class SearchQueryResolver : public QObject
{
    Q_OBJECT

public:
    explicit SearchQueryResolver(QObject *parent = nullptr);
    ~SearchQueryResolver() override;

    void setBaseUrl(const QString &url);

    /** @brief 文本中是否包含中日韩统一表意文字 */
    static bool containsChinese(const QString &text);

    /**
     * @brief 解析搜索关键词
     * @param requestId 请求序号，回调时原样带回，便于丢弃过期请求
     * @param query     用户输入（中文、英文或中英混输）
     * @param category  内容类型标记：mod / modpack / resourcepack / shader /
     *                  datapack / world / bedrock，用于识别「光影」这类分类词；为空表示不限
     */
    void resolve(quint64 requestId, const QString &query, const QString &category = QString());

signals:
    /**
     * @brief 解析完成
     * @param requestId   原样带回的请求序号
     * @param original    用户原始输入
     * @param keyword     实际用于搜索的关键词：空表示浏览全部，等于 original 表示未能翻译
     * @param matchedName 命中的中文名（未翻译、分类词时为空或等于 original）
     */
    void resolved(quint64 requestId, const QString &original,
                  const QString &keyword, const QString &matchedName);

private slots:
    void onReplyFinished();

private:
    struct Entry {
        QString chinese;
        QString english;
    };

    struct Pending {
        quint64 requestId = 0;
        QString original;
        QString normalized;
        QString category;
    };

    struct CacheEntry {
        QString keyword;      // 命中的英文关键词（ok 为 false 时为空）
        bool ok = false;      // 是否成功翻译
        qint64 savedAt = 0;   // 写入时间（秒）
    };

    void loadBundledNames();
    void loadDiskCache();
    void saveDiskCache();
    void scheduleSave();

    QString cachedKeyword(const QString &normalized, bool *found) const;

    /** 依次用内置词表与运行时缓存做本地翻译，成功时返回英文名并把中文名写入 matchedName */
    QString translateLocally(const QString &normalized, QString *matchedName) const;

    /** 关键词是否恰好是当前内容分类的分类词（如资源包页的「光影」），是则返回 true */
    static bool isCategoryWord(const QString &normalized, const QString &category);

    /** 中英混输时提取其中的英文部分（要求至少两个字母），否则返回空 */
    static QString asciiPart(const QString &text);

    static QString normalize(const QString &text);
    static int scoreMatch(const QString &query, const QString &chinese);
    /** 从 MC 百科搜索结果里取出英文项目名 */
    static QString englishNameOf(const QJsonObject &entry);
    /** 英文名是否像是一个可用作检索词的项目名 */
    static bool isUsableEnglishName(const QString &name);

    QNetworkAccessManager *m_network;
    QTimer *m_saveTimer;
    QString m_baseUrl;
    QVector<Entry> m_entries;
    QHash<QString, CacheEntry> m_cache;
    QMap<QNetworkReply *, Pending> m_pending;
    bool m_dirty = false;
};

#endif // SEARCHQUERYRESOLVER_H
