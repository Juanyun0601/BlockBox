/**
 * @file   TranslatePayloadCodec.cpp
 * @brief  列表页整页翻译的「请求 / 响应」格式编解码实现
 */

#include "TranslatePayloadCodec.h"

#include <QRegularExpression>

namespace TranslatePayloadCodec {

QMap<int, QPair<QString, QString>> parse(const QString &text)
{
    QMap<int, QPair<QString, QString>> parsed;

    // 按 [#序号#] 切分，容忍模型把多行合并或加了解释
    QRegularExpression markerReg(
        QStringLiteral("\\[#(\\d+)#\\]([\\s\\S]*?)(?=\\[#\\d+#\\]|$)"));
    QRegularExpressionMatchIterator matchIt = markerReg.globalMatch(text);
    while (matchIt.hasNext()) {
        const QRegularExpressionMatch match = matchIt.next();
        const int marker = match.captured(1).toInt();
        QString body = match.captured(2).trimmed();
        body.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
        body.remove(QRegularExpression(QStringLiteral("```[a-zA-Z]*")));
        body.remove(QRegularExpression(QStringLiteral("```")));

        // 模型偶尔会省掉 ||| 分隔符，退一步按「简介：」标记切分
        QString name;
        QString desc;
        const int sep = body.indexOf(QStringLiteral("|||"));
        if (sep >= 0) {
            name = body.left(sep).trimmed();
            desc = body.mid(sep + 3).trimmed();
        } else {
            const QRegularExpression descLabel(QStringLiteral("(?:简介|描述)\\s*[:：]"));
            const QRegularExpressionMatch descMatch = descLabel.match(body);
            if (descMatch.hasMatch()) {
                name = body.left(descMatch.capturedStart()).trimmed();
                desc = body.mid(descMatch.capturedEnd()).trimmed();
            } else {
                desc = body.trimmed();
            }
        }
        name.remove(QRegularExpression(QStringLiteral("^名称\\s*[:：]\\s*")));
        desc.remove(QRegularExpression(QStringLiteral("^简介\\s*[:：]\\s*")));
        parsed.insert(marker, qMakePair(name, desc));
    }

    return parsed;
}

} // namespace TranslatePayloadCodec
