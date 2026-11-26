/**
 * @file   LogSanitizer.cpp
 * @brief  日志脱敏实现（纯正则替换，不依赖界面 / 网络）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 替换顺序是有讲究的：
 *   1) 令牌类（JWT、Bearer、token=…）先换，避免长串随机字符被后面的
 *      IP / 手机号规则误伤，也避免它们残留在路径规则里；
 *   2) 邮箱、手机号、IP 这些"定点"隐私再换；
 *   3) 最后才换用户目录：路径规则会把整段用户名吞掉，
 *      放在后面才不会破坏前面已经替换好的占位符。
 */
#include "utils/trust/LogSanitizer.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

namespace {

// 占位符统一用尖括号包起来，既不影响阅读，也不会再被后面的规则二次命中
constexpr const char *kUserMark = "<用户>";
constexpr const char *kEmailMark = "<邮箱>";
constexpr const char *kPhoneMark = "<手机号>";
constexpr const char *kIpMark = "<IP>";
constexpr const char *kIpv6Mark = "<IPv6>";
constexpr const char *kTokenMark = "<令牌>";

/**
 * @brief 所有规则编译一次，反复使用
 *
 * 用函数内 static 而不是全局对象：QRegularExpression 的构造依赖 Qt 的
 * 全局状态，函数内 static 是线程安全的惰性初始化，也能避免静态初始化顺序问题。
 */
struct Rules
{
    QRegularExpression jwt;
    QRegularExpression bearer;
    QRegularExpression tokenPair;
    QRegularExpression email;
    QRegularExpression phone;
    QRegularExpression ipv4;
    QRegularExpression ipv6Full;
    QRegularExpression ipv6Compressed;
    QRegularExpression winUserDirWithSep;   // 用户名带空格（后面紧跟路径分隔符）
    QRegularExpression winUserDir;          // 普通用户名
    QRegularExpression unixUserDirWithSep;
    QRegularExpression unixUserDir;
};

const Rules &rules()
{
    static const Rules value = []() {
        Rules r;

        // JWT：eyJ 开头的三段 base64url
        r.jwt = QRegularExpression(QStringLiteral(
            "(?<![A-Za-z0-9_-])eyJ[A-Za-z0-9_-]{4,}\\.[A-Za-z0-9_-]{4,}\\.[A-Za-z0-9_-]{4,}"));

        // Authorization: Bearer xxx（也兼容只写 Bearer xxx 的日志）
        r.bearer = QRegularExpression(QStringLiteral("(\\bbearer\\s+)([A-Za-z0-9._~+/=-]{8,})"),
                                      QRegularExpression::CaseInsensitiveOption);

        // accessToken / access_token / client_token / clientToken /
        // refreshToken / sessionId 后面的值（= 或 : 都认；JSON 里键名外面那对引号
        // 单独抓出来再原样拼回去，值本身连引号一起换掉）
        r.tokenPair = QRegularExpression(
            QStringLiteral("\\b(access[_-]?token|client[_-]?token|refresh[_-]?token|session[_-]?id)\\b"
                           "([\"']?)(\\s*[=:]\\s*)"
                           "(\"[^\"]{0,4096}\"|'[^']{0,4096}'|[^\\s,;&\"'<>)}]{1,4096})"),
            QRegularExpression::CaseInsensitiveOption);

        // 邮箱
        r.email = QRegularExpression(QStringLiteral(
            "[A-Za-z0-9._%+\\-]+@[A-Za-z0-9.\\-]+\\.[A-Za-z]{2,}"));

        // 11 位手机号（前后不能再接数字，避免把长哈希截一段）
        r.phone = QRegularExpression(QStringLiteral("(?<!\\d)1[3-9]\\d{9}(?!\\d)"));

        // IPv4：先换掉，后面 IPv6 规则就不会把 ::ffff:1.2.3.4 拆坏
        r.ipv4 = QRegularExpression(QStringLiteral(
            "(?<![\\d.])(?:\\d{1,3}\\.){3}\\d{1,3}(?![\\d.])"));

        // IPv6 完整写法（8 组）
        r.ipv6Full = QRegularExpression(QStringLiteral(
            "(?<![0-9A-Za-z:])(?:[0-9A-Fa-f]{1,4}:){7}[0-9A-Fa-f]{1,4}(?![0-9A-Za-z:])"));

        // IPv6 压缩写法（含 ::）。要求至少有一个十六进制字符，
        // 这样 C++ 里的 std::vector、日志里的 12:34:56 时间戳都不会被误伤
        r.ipv6Compressed = QRegularExpression(QStringLiteral(
            "(?<![0-9A-Za-z:])(?=[0-9A-Fa-f:]*[0-9A-Fa-f])"
            "(?:[0-9A-Fa-f]{1,4}:){0,6}:[0-9A-Fa-f]{0,4}(?::[0-9A-Fa-f]{1,4}){0,6}"
            "(?![0-9A-Za-z:])"));

        // Windows 用户目录：C:\Users\<名> / C:\Documents and Settings\<名>
        // 用户名可能带空格（英文用户名），这种只在后面紧跟分隔符时整体脱敏
        r.winUserDirWithSep = QRegularExpression(
            QStringLiteral("([A-Za-z]:[\\\\/](?:Documents and Settings|Users)[\\\\/])"
                           "([^\\\\/\\r\\n:\"'<>|*?]+?)"
                           "(?=[\\\\/])"),
            QRegularExpression::CaseInsensitiveOption);
        r.winUserDir = QRegularExpression(
            QStringLiteral("([A-Za-z]:[\\\\/](?:Documents and Settings|Users)[\\\\/])"
                           "([^\\\\/\\r\\n\\s:\"'<>|*?，。、；：！？（）]+)"),
            QRegularExpression::CaseInsensitiveOption);

        // Linux / macOS 用户目录：/home/<名> / /Users/<名>
        r.unixUserDirWithSep = QRegularExpression(
            QStringLiteral("(/(?:home|Users)/)([^/\\r\\n:\"'<>|*?]+?)(?=/)"),
            QRegularExpression::CaseInsensitiveOption);
        r.unixUserDir = QRegularExpression(
            QStringLiteral("(/(?:home|Users)/)([^/\\r\\n\\s:\"'<>|*?，。、；：！？（）]+)"),
            QRegularExpression::CaseInsensitiveOption);

        return r;
    }();
    return value;
}

} // namespace

QString LogSanitizer::sanitize(const QString &text)
{
    if (text.isEmpty())
        return text;

    const Rules &r = rules();
    QString out = text;

    // ── 1) 令牌类 ──
    if (out.contains(QLatin1String("eyJ")))
        out.replace(r.jwt, QString::fromUtf8(kTokenMark));

    // 大小写不敏感的关键字先各自查一次，没有就不跑正则（日志动辄几 MB）
    const QString lowered = out.toLower();
    if (lowered.contains(QLatin1String("bearer")))
        out.replace(r.bearer, QStringLiteral("\\1") + QString::fromUtf8(kTokenMark));
    if (lowered.contains(QLatin1String("token")) || lowered.contains(QLatin1String("session")))
    {
        out.replace(r.tokenPair, QStringLiteral("\\1\\2\\3") + QString::fromUtf8(kTokenMark));
    }

    // ── 2) 定点隐私：邮箱 / 手机号 / IP ──
    if (out.contains(QLatin1Char('@')))
        out.replace(r.email, QString::fromUtf8(kEmailMark));
    out.replace(r.phone, QString::fromUtf8(kPhoneMark));
    out.replace(r.ipv4, QString::fromUtf8(kIpMark));
    out.replace(r.ipv6Full, QString::fromUtf8(kIpv6Mark));
    out.replace(r.ipv6Compressed, QString::fromUtf8(kIpv6Mark));

    // ── 3) 最后处理用户目录：只换用户名那一段，盘符与后续层级都保留 ──
    // 带空格的用户名先试（要求后面紧跟分隔符），再跑普通用户名规则
    out.replace(r.winUserDirWithSep, QStringLiteral("\\1") + QString::fromUtf8(kUserMark));
    out.replace(r.winUserDir, QStringLiteral("\\1") + QString::fromUtf8(kUserMark));
    out.replace(r.unixUserDirWithSep, QStringLiteral("\\1") + QString::fromUtf8(kUserMark));
    out.replace(r.unixUserDir, QStringLiteral("\\1") + QString::fromUtf8(kUserMark));

    return out;
}

QByteArray LogSanitizer::sanitizeBytes(const QByteArray &data)
{
    if (data.isEmpty())
        return data;

    const QString text = QString::fromUtf8(data);
    const QString clean = sanitize(text);
    if (clean == text)
        return data; // 没有任何隐私片段：原样返回，避免重新编码改动字节
    return clean.toUtf8();
}

bool LogSanitizer::sanitizeFile(const QString &filePath)
{
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = input.readAll();
    input.close();

    const QByteArray clean = sanitizeBytes(raw);
    if (clean == raw)
        return true;

    // 原子写回：写到一半出错时原文件不会被截断
    QSaveFile output(filePath);
    if (!output.open(QIODevice::WriteOnly))
        return false;
    if (output.write(clean) != clean.size())
    {
        output.cancelWriting();
        return false;
    }
    return output.commit();
}
