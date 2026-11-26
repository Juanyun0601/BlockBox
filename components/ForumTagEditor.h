/**
 * @file   ForumTagEditor.h
 * @brief  发帖 / 发资源时用的标签编辑器：胶囊标签 + 输入框 + 推荐标签
 * @author BlockBox Team
 * @date   2026-11-21
 *
 * 发帖页（帖子 / 资源 / 教程三种发布类型）、帖子详情页的「编辑标题与标签」、
 * 创作中心的「编辑作品」共用这一个控件，保证各处打标签的手感一致：
 *   - 输入后按回车、空格或逗号就变成一个标签胶囊，粘贴「1.20.4, 优化」会拆成两个；
 *   - 胶囊点一下就移除，输入框空着时按退格也能撤掉最后一个；
 *   - 自动去掉开头的 #、去重（不区分大小写）、每个最长 12 字、最多 4 个；
 *   - 下方推荐标签点一下就加：最近用过的、当前发布类型的常用标签、论坛热门标签，
 *     输入时按内容过滤，已经加过的不会再出现。
 * 标签规则与论坛服务端保持一致，超出上限时只在本地提示，不会白提交一次。
 */
#pragma once

#include <QPair>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QTimer;
class FlowLayout;

class ForumTagEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ForumTagEditor(QWidget *parent = nullptr);

    /** 最多几个标签（默认 4，与服务端一致） */
    void setMaxTags(int max);
    int maxTags() const { return m_maxTags; }

    /** 单个标签最长几个字（默认 12，与服务端一致） */
    void setMaxTagLength(int length);
    int maxTagLength() const { return m_maxTagLength; }

    /** 回显已有标签（编辑帖子 / 作品时用）；同样会去重与截断 */
    void setTags(const QStringList &tags);

    /** 当前已添加的标签；调用前先 commitPending()，输入框里没提交的才会算进来 */
    QStringList tags() const { return m_tags; }

    /** 把输入框里还没按回车的文字也变成标签 */
    void commitPending();

    /** 清空全部标签和输入框（重开发布页时用） */
    void clear();

    /** 当前发布类型的常用标签（帖子 / 插件 / 投影 / 教程），排在热门标签前面 */
    void setRecommendedTags(const QStringList &tags);

    /** 论坛热门标签；second 是已有主题数，只用来做鼠标提示 */
    void setPopularTags(const QVector<QPair<QString, int>> &tags);

    /** 发布成功后记下本次标签，下次打标签时排在推荐最前面 */
    void rememberTags();

signals:
    /** 标签增删（输入框里的文字变化不算，避免每敲一个字都通知） */
    void tagsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void rebuildChips();
    void rebuildSuggestions();
    /** 从一段文字里拆出多个标签（逗号 / 空格 / 顿号 / 分号都算分隔） */
    void addParsedTags(const QString &text);
    void removeTag(int index);
    void updateCounter();
    void updateInputState();
    void refreshHint();
    /** 临时提示（例如「最多 4 个」），过几秒自动退回常规说明 */
    void showTransientHint(const QString &text);
    void setHintWarning(bool warn);
    void applyFrameFocus(bool focused);
    /** 推荐候选：最近用过 → 当前类型常用 → 论坛热门，去掉已添加的 */
    QVector<QPair<QString, int>> suggestionCandidates() const;
    /** 输入框里正在敲的那一段（有逗号时只看最后一段），用来过滤推荐 */
    QString filterText() const;
    bool containsTag(const QString &tag) const;

    int m_maxTags = 4;
    int m_maxTagLength = 12;
    QStringList m_tags;
    QStringList m_recentTags;
    QStringList m_recommendedTags;
    QVector<QPair<QString, int>> m_popularTags;

    QLabel *m_counterLabel = nullptr;
    QFrame *m_field = nullptr;
    QWidget *m_chipsHost = nullptr;
    FlowLayout *m_chipsLayout = nullptr;
    QLineEdit *m_input = nullptr;
    /** 输入框为空时的提示语（标签加满后会被临时替换成上限说明） */
    QString m_placeholder;
    /** 输入法正在选词（有 preedit）时不要抢回车 / 逗号，先让输入法把字上屏 */
    bool m_composing = false;
    QWidget *m_suggestHost = nullptr;
    FlowLayout *m_suggestLayout = nullptr;
    QLabel *m_hintLabel = nullptr;
    QTimer *m_hintTimer = nullptr;
};
