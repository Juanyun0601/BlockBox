/**
 * @file   CardListTranslator.h
 * @brief  卡片列表页「整页翻译」控制器（名称 + 简介）
 *
 * 资源包 / 光影包 / 模组等列表页共用：把当前这一页所有卡片的名称与简介
 * 按 `[#n#] 名称 ||| 简介` 的格式一次性交给 AI，回来后按序号落位。
 *
 * 与界面解耦：
 * - 页面负责 EN /「中」悬浮卡片的显示与 rebuildCards()；
 * - 本类负责原文快照、请求、解析、以及「中文态数据」的生成。
 *
 * 典型用法：
 * @code
 *   m_translator = new CardListTranslator(this);
 *   connect(m_translator, &CardListTranslator::busyChanged, m_translateSwitch, &TranslateSwitchCard::setBusy);
 *   connect(m_translator, &CardListTranslator::finished, this, &Page::applyListTranslation);
 *   connect(m_translator, &CardListTranslator::failed, this, [this](const QString &err) { ... });
 *   // placeCards() 末尾
 *   if (!m_translator->syncBatch(m_cardInfos)) m_translateSwitch->setChineseMode(false);
 * @endcode
 */

#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QString>

#include "../utils/mod/ModData.h"

class TranslationService;

class CardListTranslator : public QObject
{
    Q_OBJECT

public:
    explicit CardListTranslator(QObject *parent = nullptr);
    ~CardListTranslator() override;

    /**
     * @brief 核对当前这一批卡片
     *
     * 翻页 / 重新搜索 / 追加加载后卡片集合会变，此时翻译结果作废并回到英文态。
     * 只比较数量与 id（翻译只改名称与简介，不改 id）。
     * @return false 表示换了批次（内部已取消请求并清空缓存）
     */
    bool syncBatch(const QList<ModInfo> &infos);

    /** @brief 开始翻译这一批卡片；已有缓存时不会重复请求 */
    void start(const QList<ModInfo> &infos);

    /** @brief 取消进行中的请求（不发送任何信号，busy 状态由 busyChanged 通知） */
    void cancel();

    bool isBusy() const { return m_busy; }
    /** @brief 是否已有可用的翻译结果 */
    bool hasCache() const { return m_ready; }
    bool isApplied() const { return m_applied; }
    /** @brief 由页面在 rebuildCards() 后标记当前是否处于中文态 */
    void setApplied(bool applied) { m_applied = applied; }

    /** @brief 英文原文快照（切回 EN 时据此还原） */
    const QList<ModInfo> &originals() const { return m_originals; }

    /** @brief 依据原文快照 + 翻译结果生成中文态数据（无结果时返回原文） */
    QList<ModInfo> translated() const;

    /**
     * @brief 解析模型返回的 `[#n#] 名称 ||| 简介`
     *
     * 容忍 ``` 代码块包裹、「简介：」标记、条目缺失等情况。
     */
    static QMap<int, QPair<QString, QString>> parseResponse(const QString &text);

signals:
    /** @brief 请求进行中（页面据此显示「AI 翻译中…」并禁用「中」） */
    void busyChanged(bool busy);
    /** @brief 翻译结果就绪，可调用 translated() 取数据 */
    void finished();
    /** @brief 翻译失败（网络 / 配置 / 返回格式异常） */
    void failed(const QString &error);

private:
    struct Item {
        int cardIndex = -1;
        int marker = -1;
        bool wantName = false;
        bool wantDesc = false;
    };

    TranslationService *service();
    void setBusy(bool busy);
    void onResponse(const QString &text);
    void onError(const QString &error);

    static bool sameIds(const QList<ModInfo> &a, const QList<ModInfo> &b);
    static bool containsHanText(const QString &text);

    TranslationService *m_service = nullptr;
    QList<ModInfo> m_batch;      ///< 最近一次同步到的卡片集合（只用来判断换批）
    QList<ModInfo> m_originals;  ///< 发起翻译时的英文原文快照
    QList<Item> m_items;         ///< 每个条目要翻哪些字段
    QList<QPair<QString, QString>> m_translated; ///< 与 marker 对齐的译名 / 译简介
    bool m_busy = false;
    bool m_ready = false;
    bool m_applied = false;
};
