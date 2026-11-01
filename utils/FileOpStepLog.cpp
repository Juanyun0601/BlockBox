/**
 * @file   FileOpStepLog.cpp
 * @brief  运行日志 → 文件操作步骤 解析实现
 * @author BlockBox Team
 * @date   2026-10-25
 */

#include "utils/FileOpStepLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>

#include "utils/LauncherLogger.h"

namespace {

/// 会话分隔头：`==================== 方块盒子启动 2026-10-24 17:13:27 ====================`
const QRegularExpression &sessionHeaderRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(={2,}\s*方块盒子启动\s+(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}))"));
    return re;
}

/// 会话头下一行里的版本：`版本 beta5.1 · Windows ...`
const QRegularExpression &sessionVersionRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(版本\s+(\S+))"));
    return re;
}

/// 标准日志行：
/// `2026-10-24 20:11:33.045 [DEBUG] message`
/// `2026-10-24 20:11:33.045 [DEBUG] [Module] message`
/// 级别标签可能带空格对齐（DEBUG / INFO / WARN / ERROR / FATAL）
const QRegularExpression &logLineRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s+\[(\w+)\s*\](?:\s+\[([^\]]+)\])?\s+(.*)$)"));
    return re;
}

/// 从消息里抠出引号包住的路径/URL
QString quotedOrRest(const QString &msg)
{
    static const QRegularExpression q1(QStringLiteral("\"([^\"]+)\""));
    auto m = q1.match(msg);
    if (m.hasMatch())
        return m.captured(1);

    static const QRegularExpression q2(QStringLiteral("'([^']+)'"));
    m = q2.match(msg);
    if (m.hasMatch())
        return m.captured(1);

    // 末尾裸路径/URL（无引号）
    static const QRegularExpression tail(
        QStringLiteral("(https?://\\S+|[A-Za-z]:[\\\\/][^\\s]+|\\S+\\.(?:jar|json|zip|lzma|log|png|txt|xml|dll|so|pak)\\b)\\s*$"));
    m = tail.match(msg);
    if (m.hasMatch())
        return m.captured(1);

    return msg.trimmed();
}

/// 取 URL / 路径最后一段，便于标题里显示文件名
QString baseNameOf(const QString &pathOrUrl)
{
    QString s = pathOrUrl;
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const int q = s.indexOf(QLatin1Char('?'));
    if (q >= 0)
        s = s.left(q);
    const int slash = s.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0 && slash + 1 < s.size())
        return s.mid(slash + 1);
    return s;
}

bool looksLikeErrorLevel(const QString &level)
{
    return level == QStringLiteral("WARN")
        || level == QStringLiteral("WARNING")
        || level == QStringLiteral("ERROR")
        || level == QStringLiteral("CRITICAL")
        || level == QStringLiteral("FATAL");
}

/// 单条日志行 → 步骤；不需要则返回空 title 的空步骤（title.isEmpty() 表示跳过）
FileOpStep classifyLine(const QString &level, const QString &module, const QString &msg)
{
    FileOpStep step;
    step.level = level;
    step.module = module;
    step.detail = msg;
    step.success = !looksLikeErrorLevel(level);

    const bool isErr = looksLikeErrorLevel(level);
    const QString m = msg;

    // ── 下载 ──（英文模式兼容旧日志，中文模式对应现行日志）
    if (m.contains(QStringLiteral("Downloading"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Download cache hit"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Download succeeded"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Download completed"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Downloaded:"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Download failed"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Download aborted"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("下载失败"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("下载超时"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("安装器下载失败"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("正在下载"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("开始下载"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("下载完成"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("下载成功"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("下载已取消"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("下载校验失败"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("缓存命中"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("命中缓存"), Qt::CaseInsensitive)
        || (m.contains(QStringLiteral("写入下载数据"), Qt::CaseInsensitive)
            && !m.contains(QStringLiteral("无法"), Qt::CaseInsensitive))
        || (module.contains(QStringLiteral("Download"), Qt::CaseInsensitive)
            && (m.contains(QStringLiteral("Failed"), Qt::CaseInsensitive)
                || m.contains(QStringLiteral("失败"), Qt::CaseInsensitive)
                || m.contains(QStringLiteral("Writing extra"), Qt::CaseInsensitive)))) {
        step.kind = QStringLiteral("下载");
        const QString target = quotedOrRest(m);

        if (m.contains(QStringLiteral("Download cache hit"), Qt::CaseInsensitive)
            || m.contains(QStringLiteral("缓存命中"), Qt::CaseInsensitive)
            || m.contains(QStringLiteral("命中缓存"), Qt::CaseInsensitive)) {
            step.title = QStringLiteral("命中缓存：%1").arg(baseNameOf(target));
            step.success = true;
        } else if (m.contains(QStringLiteral("Download succeeded"), Qt::CaseInsensitive)
                   || m.contains(QStringLiteral("Download completed"), Qt::CaseInsensitive)
                   || m.contains(QStringLiteral("Downloaded:"), Qt::CaseInsensitive)
                   || m.contains(QStringLiteral("下载完成"), Qt::CaseInsensitive)
                   || m.contains(QStringLiteral("下载成功"), Qt::CaseInsensitive)) {
            step.title = QStringLiteral("下载完成：%1").arg(baseNameOf(target));
            step.success = true;
        } else if ((m.contains(QStringLiteral("Downloading"), Qt::CaseInsensitive)
                    || m.contains(QStringLiteral("正在下载"), Qt::CaseInsensitive)
                    || m.contains(QStringLiteral("开始下载"), Qt::CaseInsensitive))
                   && !m.contains(QStringLiteral("failed"), Qt::CaseInsensitive)
                   && !m.contains(QStringLiteral("失败"), Qt::CaseInsensitive)) {
            step.title = QStringLiteral("开始下载：%1").arg(baseNameOf(target));
            step.success = true;
        } else if ((m.contains(QStringLiteral("Writing extra"), Qt::CaseInsensitive)
                    || m.contains(QStringLiteral("写入下载数据"), Qt::CaseInsensitive))
                   && !m.contains(QStringLiteral("无法"), Qt::CaseInsensitive)) {
            step.title = QStringLiteral("写入下载数据：%1").arg(baseNameOf(target));
        } else {
            step.title = QStringLiteral("下载失败：%1").arg(baseNameOf(target));
            step.success = false;
        }
        step.detail = target;
        return step;
    }

    // ── 复制 ──
    if (m.contains(QStringLiteral("Failed to copy"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Copied"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("copy library"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("复制"), Qt::CaseInsensitive)) {
        step.kind = QStringLiteral("复制");
        const QString target = quotedOrRest(m);
        if (m.contains(QStringLiteral("Failed to copy"), Qt::CaseInsensitive)
            || (isErr && m.contains(QStringLiteral("copy"), Qt::CaseInsensitive))
            || (isErr && m.contains(QStringLiteral("复制"), Qt::CaseInsensitive))) {
            step.title = QStringLiteral("复制失败：%1").arg(baseNameOf(target));
            step.success = false;
        } else if (m.contains(QStringLiteral("Copied"), Qt::CaseInsensitive)
                   && m.contains(QStringLiteral("libraries"), Qt::CaseInsensitive)) {
            step.title = QStringLiteral("复制：%1").arg(m.trimmed());
        } else if (m.contains(QStringLiteral("已复制"), Qt::CaseInsensitive)) {
            step.title = m.trimmed();
        } else {
            step.title = QStringLiteral("复制文件：%1").arg(baseNameOf(target));
            step.success = true;
        }
        step.detail = target;
        return step;
    }

    // ── 解压 ──
    if (m.contains(QStringLiteral("extract"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("解压"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Unsafe entry path rejected"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Deflate decompression"), Qt::CaseInsensitive)) {
        step.kind = QStringLiteral("解压");
        const QString target = quotedOrRest(m);
        if (isErr || m.contains(QStringLiteral("Failed"), Qt::CaseInsensitive)
            || m.contains(QStringLiteral("rejected"), Qt::CaseInsensitive)
            || m.contains(QStringLiteral("failed"), Qt::CaseInsensitive)) {
            step.title = QStringLiteral("解压失败：%1").arg(baseNameOf(target));
            step.success = false;
        } else {
            step.title = QStringLiteral("解压：%1").arg(baseNameOf(target));
        }
        step.detail = target;
        return step;
    }

    // ── 安装 / 处理器 ──
    if (m.contains(QStringLiteral("Executing processor"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Processor failed"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("正在执行安装处理器"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("安装"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Reused"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Skipping processor"), Qt::CaseInsensitive)) {
        step.kind = QStringLiteral("安装");
        const QString target = quotedOrRest(m);
        if (m.contains(QStringLiteral("Processor failed"), Qt::CaseInsensitive)
            || (isErr && m.contains(QStringLiteral("安装"), Qt::CaseInsensitive))) {
            step.title = QStringLiteral("安装处理失败：%1").arg(baseNameOf(target));
            step.success = false;
        } else if (m.contains(QStringLiteral("Reused"), Qt::CaseInsensitive)) {
            step.title = QStringLiteral("复用已有文件：%1").arg(m.trimmed());
        } else if (m.contains(QStringLiteral("Skipping processor"), Qt::CaseInsensitive)) {
            step.title = QStringLiteral("跳过已存在的处理结果：%1").arg(baseNameOf(target));
        } else if (m.contains(QStringLiteral("Executing processor"), Qt::CaseInsensitive)
                   || m.contains(QStringLiteral("正在执行安装处理器"), Qt::CaseInsensitive)) {
            step.title = QStringLiteral("执行安装处理器：%1").arg(baseNameOf(target));
        } else {
            step.title = QStringLiteral("安装：%1").arg(m.trimmed());
        }
        step.detail = target;
        return step;
    }

    // ── 删除 ──
    if (m.contains(QStringLiteral("删除"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("清理"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Cache cleanup"), Qt::CaseInsensitive)) {
        // 缓存清理噪音较大，仅错误级别或带明确「删除」时收录
        if (!isErr && !m.contains(QStringLiteral("删除"), Qt::CaseInsensitive))
            return {};
        step.kind = QStringLiteral("删除");
        step.title = QStringLiteral("删除：%1").arg(baseNameOf(quotedOrRest(m)));
        step.detail = quotedOrRest(m);
        return step;
    }

    // ── 写入 ──
    if (m.contains(QStringLiteral("无法写入"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("写入合并"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("写入失败"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("无法打开"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("Failed to write"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("cannot open"), Qt::CaseInsensitive)
        || m.contains(QStringLiteral("无法创建"), Qt::CaseInsensitive)) {
        step.kind = QStringLiteral("写入");
        const QString target = quotedOrRest(m);
        step.title = QStringLiteral("写入失败：%1").arg(baseNameOf(target));
        step.success = false;
        step.detail = target;
        return step;
    }

    // ── 其它明确带文件路径的失败 ──
    if (isErr
        && (m.contains(QStringLiteral("File"), Qt::CaseInsensitive)
            || m.contains(QStringLiteral("文件"), Qt::CaseInsensitive)
            || m.contains(QStringLiteral("路径"), Qt::CaseInsensitive)
            || m.contains(QStringLiteral("directory"), Qt::CaseInsensitive)
            || m.contains(QStringLiteral("目录"), Qt::CaseInsensitive))) {
        step.kind = QStringLiteral("其他文件操作");
        step.title = QStringLiteral("文件相关问题：%1").arg(m.trimmed());
        step.success = false;
        return step;
    }

    return {};
}

/// 同一会话内，把「开始下载」与随后的「下载完成 / 失败」按目标配对，减少重复步骤
void mergeDownloadPairs(QList<FileOpStep> &steps)
{
    QHash<QString, int> pendingStart; // target -> index in steps
    QList<int> toRemove;

    for (int i = 0; i < steps.size(); ++i) {
        FileOpStep &s = steps[i];
        if (s.kind != QStringLiteral("下载"))
            continue;

        const QString key = s.detail.trimmed();
        if (key.isEmpty())
            continue;

        if (s.title.startsWith(QStringLiteral("开始下载"))) {
            if (pendingStart.contains(key)) {
                // 连续两条开始下载同一目标：去掉前一条，保留最新
                toRemove.append(pendingStart.value(key));
            }
            pendingStart.insert(key, i);
            continue;
        }

        // 完成 / 失败 / 缓存：并入对应「开始下载」
        if (pendingStart.contains(key)) {
            const int startIdx = pendingStart.take(key);
            FileOpStep &start = steps[startIdx];
            // 失败信息优先展示；成功则合并为一步「下载 …」
            if (!s.success) {
                start.title = s.title;
                start.success = false;
                start.level = s.level;
                start.rawLine = s.rawLine;
                if (start.timestamp.isNull() && !s.timestamp.isNull())
                    start.timestamp = s.timestamp;
            } else {
                // 成功：标题改为「下载 xxx」，去掉「开始」字样
                start.title = QStringLiteral("下载：%1").arg(baseNameOf(key));
                start.success = true;
            }
            toRemove.append(i);
        }
    }

    for (int i = toRemove.size() - 1; i >= 0; --i)
        steps.removeAt(toRemove.at(i));
}

} // namespace

namespace FileOpStepLog {

QList<FileOpStep> parseLogText(const QString &text)
{
    QList<FileOpStep> steps;
    const QStringList lines = text.split(QLatin1Char('\n'));

    int sessionIndex = -1;
    QString pendingVersion;
    bool nextLineMaybeVersion = false;

    // 先扫一遍：会话分隔、标准行解析；原始行进临时缓冲再配对
    struct RawHit
    {
        FileOpStep step;
        int        session = -1;
    };
    QList<RawHit> hits;

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        auto hm = sessionHeaderRe().match(line);
        if (hm.hasMatch()) {
            ++sessionIndex;
            pendingVersion.clear();
            nextLineMaybeVersion = true;

            FileOpStep marker;
            marker.kind = QStringLiteral("__session__");
            marker.title = QStringLiteral("启动会话");
            marker.detail = hm.captured(1);
            marker.sessionIndex = sessionIndex;
            marker.timestamp = QDateTime::fromString(hm.captured(1),
                QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            hits.append({marker, sessionIndex});
            continue;
        }

        if (nextLineMaybeVersion) {
            nextLineMaybeVersion = false;
            auto vm = sessionVersionRe().match(line);
            if (vm.hasMatch()) {
                pendingVersion = vm.captured(1);
                if (!hits.isEmpty() && hits.last().step.kind == QStringLiteral("__session__"))
                    hits.last().step.module = pendingVersion;
                continue;
            }
        }

        auto lm = logLineRe().match(line);
        if (!lm.hasMatch())
            continue;

        FileOpStep step = classifyLine(lm.captured(2).trimmed(),
                                       lm.captured(3),
                                       lm.captured(4));
        if (step.title.isEmpty())
            continue;

        step.timestamp = QDateTime::fromString(lm.captured(1),
            QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        step.rawLine = line;
        // 非标准分隔下的孤立日志也归到会话 -1（显示为「未标记会话」）
        step.sessionIndex = sessionIndex;
        hits.append({step, sessionIndex});
    }

    // 过滤出真实操作步骤，按会话分组合并下载
    QList<FileOpStep> filtered;
    filtered.reserve(hits.size());
    for (const RawHit &h : hits) {
        if (h.step.kind == QStringLiteral("__session__"))
            continue;
        filtered.append(h.step);
    }

    // 按会话分组配对下载（避免跨会话误合并），再按会话序号顺序拼回
    QHash<int, QList<int>> groups;
    for (int i = 0; i < filtered.size(); ++i)
        groups[filtered[i].sessionIndex].append(i);

    QList<int> sessionKeys = groups.keys();
    std::sort(sessionKeys.begin(), sessionKeys.end());
    for (int key : sessionKeys) {
        QList<FileOpStep> group;
        const QList<int> &idxs = groups.value(key);
        group.reserve(idxs.size());
        for (int idx : idxs)
            group.append(filtered.at(idx));
        mergeDownloadPairs(group);
        for (const FileOpStep &s : group)
            steps.append(s);
    }

    // 同一会话内按时间戳排序（stable，保持同一毫秒内的原始先后）
    std::stable_sort(steps.begin(), steps.end(), [](const FileOpStep &a, const FileOpStep &b) {
        if (a.sessionIndex != b.sessionIndex)
            return a.sessionIndex < b.sessionIndex;
        if (a.timestamp.isValid() && b.timestamp.isValid() && a.timestamp != b.timestamp)
            return a.timestamp < b.timestamp;
        return false;
    });

    // 重新编号会话：若日志从中间开始（没有头），sessionIndex 可能全是 -1
    bool anySession = false;
    for (const FileOpStep &s : steps) {
        if (s.sessionIndex >= 0) {
            anySession = true;
            break;
        }
    }
    if (!anySession) {
        for (FileOpStep &s : steps)
            s.sessionIndex = 0;
    }

    return steps;
}

QList<FileOpStep> parseLogFiles(const QStringList &paths)
{
    QString combined;
    for (const QString &path : paths) {
        if (path.isEmpty() || !QFileInfo::exists(path))
            continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        // 文件之间插一个空行，避免最后一行与下一文件首行粘连
        combined += QString::fromUtf8(f.readAll());
        if (!combined.endsWith(QLatin1Char('\n')))
            combined += QLatin1Char('\n');
        combined += QLatin1Char('\n');
        f.close();
    }
    if (combined.isEmpty())
        return {};
    return parseLogText(combined);
}

QStringList defaultLogFiles()
{
    const QString dir = LauncherLogger::logDirectory();
    // 从旧到新：.2 → .1 → 当前，和轮转顺序一致
    QStringList files;
    files << dir + QStringLiteral("/launcher.log.2");
    files << dir + QStringLiteral("/launcher.log.1");
    files << dir + QStringLiteral("/launcher.log");
    QStringList exist;
    for (const QString &f : files) {
        if (QFileInfo::exists(f))
            exist << f;
    }
    return exist;
}

QList<FileOpLogSession> collectSessions(const QList<FileOpStep> &steps)
{
    // sessionIndex 来自原始文本扫描，步骤本身不带 version。
    // 这里只做序号去重列表，弹窗侧再根据步骤数量过滤空会话。
    QHash<int, FileOpLogSession> map;
    QList<int> order;
    for (const FileOpStep &s : steps) {
        if (!map.contains(s.sessionIndex)) {
            FileOpLogSession sess;
            sess.index = s.sessionIndex;
            map.insert(s.sessionIndex, sess);
            order.append(s.sessionIndex);
        }
    }
    QList<FileOpLogSession> out;
    std::sort(order.begin(), order.end());
    for (int idx : order)
        out.append(map.value(idx));
    return out;
}

} // namespace FileOpStepLog
