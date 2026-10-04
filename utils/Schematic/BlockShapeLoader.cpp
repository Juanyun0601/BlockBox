/**
 * @file   BlockShapeLoader.cpp
 * @brief  从实例客户端 jar 解析方块真实形状（blockstate + model）
 * @author BlockBox Team
 * @date   2026-08-18
 */

#include "BlockShapeLoader.h"

#include "utils/JarUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtMath>

namespace {

const char kBS[]   = "assets/minecraft/blockstates/";
const char kModelB[] = "assets/minecraft/models/block/";
const char kModelI[] = "assets/minecraft/models/item/";

QJsonObject parseJsonObject(const QByteArray &data)
{
    if (data.isEmpty())
        return QJsonObject();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

// 把模型引用规范化为查找键，如 "minecraft:block/cube_all#angry" -> "block/cube_all"
QString normalizeModelKey(const QString &key)
{
    QString k = key.trimmed();
    const int hash = k.indexOf(QLatin1Char('#'));
    if (hash >= 0)
        k = k.left(hash);
    if (k.startsWith(QLatin1String("minecraft:")))
        k = k.mid(10);
    while (k.endsWith(QLatin1String(".json")))
        k.chop(5);
    return k;
}

} // namespace

// ============================================================================
// 解析一个模型（含 parent 链）——纹理与元素都逐层处理
// ============================================================================

// 沿纹理链解析纹理变量（#var -> 直接 key），成功返回 true
static bool resolveTexture(QString var,
                           const QMap<QString, QString> &chain,
                           QString &out)
{
    if (var.startsWith(QLatin1Char('#')))
        var = var.mid(1);
    QString cur = var;
    int guard = 0;
    while (guard++ < 32)
    {
        auto it = chain.constFind(cur);
        if (it == chain.constEnd())
            return false;               // 无法解析
        QString val = it.value();
        if (val.startsWith(QLatin1Char('#')))
        {
            cur = val.mid(1);
            continue;
        }
        out = normalizeModelKey(val);
        return true;
    }
    return false;
}

// blockstate 级模型引用：模型名 + 在 blockstate 中给定的 x/y 旋转（0/90/180/270）
struct ModelRef
{
    QString model;
    int     rotX = 0;
    int     rotY = 0;
};

// 读取单个变体对象 / multipart apply 对象的 model + x/y 旋转
static ModelRef refFromObject(const QJsonObject &obj, const QString &fallbackModel)
{
    ModelRef r;
    r.model = obj.value(QStringLiteral("model")).toString();
    if (r.model.isEmpty())
        r.model = fallbackModel;
    r.rotX = obj.value(QStringLiteral("x")).toInt(0);
    r.rotY = obj.value(QStringLiteral("y")).toInt(0);
    return r;
}

// 解析 model 引用值（字符串 或 array[obj] 或 obj）为模型引用列表
static void appendModelRefs(const QJsonValue &val,
                            const QString &fallbackModel,
                            QList<ModelRef> &refs)
{
    if (val.isArray())
    {
        for (const QJsonValue &v : val.toArray())
        {
            if (v.isObject())
                refs.append(refFromObject(v.toObject(), fallbackModel));
            else if (v.isString())
            {
                ModelRef r; r.model = v.toString();
                refs.append(r);
            }
        }
    }
    else if (val.isObject())
    {
        refs.append(refFromObject(val.toObject(), fallbackModel));
    }
    else if (val.isString())
    {
        ModelRef r; r.model = val.toString();
        refs.append(r);
    }
}

// ============================================================================
// 构造 / 加载
// ============================================================================

void BlockShapeLoader::addModelEntry(const QString &name, const QJsonObject &obj)
{
    ModelJson m;
    m.parent = obj.value(QStringLiteral("parent")).toString();
    const QJsonObject tex = obj.value(QStringLiteral("textures")).toObject();
    for (auto it = tex.constBegin(); it != tex.constEnd(); ++it)
        m.textures.insert(it.key(), it.value().toString());
    m.hasElements = obj.contains(QStringLiteral("elements"))
        && obj.value(QStringLiteral("elements")).isArray();
    if (m.hasElements)
        m.elemObj = obj;
    m_models.insert(name, m);
}

// 绕 pivot 旋转一点（右手系，正轴逆时针；角度为度）
static void rotateBoxPoint(float &x, float &y, float &z,
                           float px, float py, float pz, float angDeg, int axis)
{
    if (qFuzzyIsNull(angDeg))
        return;
    const float a = qDegreesToRadians(angDeg);
    const float c = qFastCos(a), s = qFastSin(a);
    const float dx = x - px, dy = y - py, dz = z - pz;
    if (axis == 0)             // 绕 X
    {
        y = py + dy * c - dz * s;
        z = pz + dy * s + dz * c;
    }
    else if (axis == 2)        // 绕 Z
    {
        x = px + dx * c - dy * s;
        y = py + dx * s + dy * c;
    }
    else                       // 绕 Y
    {
        x = px + dx * c - dz * s;
        z = pz + dx * s + dz * c;
    }
}

// 把 blockstate 级的 x/y 旋转施加到 boxes[start..] 上：
// 按 8 角点（含元素自身旋转）旋转后重新求轴向包围盒。
// MC 的 blockstate 旋转恒为 90° 整数倍，对轴向盒旋转结果仍是精确轴向盒。
static void applyBlockRotation(QList<ShapeBox> &boxes, int start, float rotX, float rotY)
{
    if (qFuzzyIsNull(rotX) && qFuzzyIsNull(rotY))
        return;
    const float cx = 0.5f, cy = 0.5f, cz = 0.5f;
    for (int i = start; i < boxes.size(); ++i)
    {
        ShapeBox &b = boxes[i];
        float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
        for (int bit = 0; bit < 8; ++bit)
        {
            float px = (bit & 1) ? b.to[0] : b.from[0];
            float py = (bit & 2) ? b.to[1] : b.from[1];
            float pz = (bit & 4) ? b.to[2] : b.from[2];
            if (b.rotated)
                rotateBoxPoint(px, py, pz, b.rotOrigin[0], b.rotOrigin[1], b.rotOrigin[2],
                               b.rotAngle, b.rotAxis);
            rotateBoxPoint(px, py, pz, cx, cy, cz, rotX, 0);
            rotateBoxPoint(px, py, pz, cx, cy, cz, rotY, 1);
            mn[0] = qMin(mn[0], px); mn[1] = qMin(mn[1], py); mn[2] = qMin(mn[2], pz);
            mx[0] = qMax(mx[0], px); mx[1] = qMax(mx[1], py); mx[2] = qMax(mx[2], pz);
        }
        for (int a = 0; a < 3; ++a)
        {
            b.from[a] = mn[a];
            b.to[a]   = mx[a];
        }
        b.rotated = false;      // 已合入轴向包围盒
        b.rotAngle = 0.0f;
    }
}

bool BlockShapeLoader::load(const QString &instancePath, QString *errorOut)
{
    m_loaded = false;
    m_models.clear();
    m_blockstates.clear();

    QDir dir(instancePath);
    if (!dir.exists())
    {
        if (errorOut) *errorOut = QStringLiteral("实例目录不存在：%1").arg(instancePath);
        return false;
    }

    // 定位客户端 jar（与 InstanceTextureLoader 相同规则）
    QString jarPath;
    const QString base = QFileInfo(instancePath).fileName();
    QString bestJar;
    qint64 bestSize = -1;
    const QFileInfoList jars = dir.entryInfoList(QStringList() << "*.jar", QDir::Files, QDir::Name);
    for (const QFileInfo &fi : jars)
    {
        if (fi.completeBaseName() == base) { jarPath = fi.absoluteFilePath(); break; }
        if (fi.size() > bestSize) { bestSize = fi.size(); bestJar = fi.absoluteFilePath(); }
    }
    if (jarPath.isEmpty() && !bestJar.isEmpty())
        jarPath = bestJar;
    if (jarPath.isEmpty())
    {
        if (errorOut) *errorOut = QStringLiteral("未在实例目录找到客户端 .jar：%1").arg(instancePath);
        return false;
    }

    // blockstates
    const QStringList bs = JarUtils::listEntriesInJar(jarPath, kBS);
    for (const QString &e : bs)
    {
        if (!e.endsWith(QStringLiteral(".json"))) continue;
        QByteArray data;
        if (!JarUtils::extractFromJarToMemory(jarPath, e, data)) continue;
        m_blockstates.insert(QFileInfo(e).completeBaseName(), parseJsonObject(data));
    }

    // block models
    const QStringList mb = JarUtils::listEntriesInJar(jarPath, kModelB);
    for (const QString &e : mb)
    {
        if (!e.endsWith(QStringLiteral(".json"))) continue;
        QByteArray data;
        if (!JarUtils::extractFromJarToMemory(jarPath, e, data)) continue;
        addModelEntry(QStringLiteral("block/") + QFileInfo(e).completeBaseName(),
                      parseJsonObject(data));
    }

    // item models（部分 blockstate 引用）
    const QStringList mi = JarUtils::listEntriesInJar(jarPath, kModelI);
    for (const QString &e : mi)
    {
        if (!e.endsWith(QStringLiteral(".json"))) continue;
        QByteArray data;
        if (!JarUtils::extractFromJarToMemory(jarPath, e, data)) continue;
        addModelEntry(QStringLiteral("item/") + QFileInfo(e).completeBaseName(),
                      parseJsonObject(data));
    }

    if (m_blockstates.isEmpty() && m_models.isEmpty())
    {
        if (errorOut) *errorOut = QStringLiteral("实例 jar 中没有可用的方块模型数据：%1").arg(jarPath);
        return false;
    }

    m_loaded = true;
    return true;
}

// ============================================================================
// 收集模型元素（递归 parent 链）
// ============================================================================

void BlockShapeLoader::collectElements(const QString &modelKey,
                                       const QMap<QString, QString> *texOverride,
                                       QList<ShapeBox> &boxes,
                                       QStringList &visited) const
{
    const QString norm = normalizeModelKey(modelKey);
    if (norm.isEmpty() || visited.contains(norm))
        return;
    visited.append(norm);

    auto it = m_models.constFind(norm);
    if (it == m_models.constEnd())
        return;
    const ModelJson &model = it.value();

    // ---- 合并纹理链：子模型纹理优先级最高，覆盖父模型同名变量 ----
    QMap<QString, QString> mergedTex;
    if (texOverride)
        for (auto oit = texOverride->constBegin(); oit != texOverride->constEnd(); ++oit)
            mergedTex.insert(oit.key(), oit.value());

    if (!model.parent.isEmpty())
    {
        // 先合并父链纹理（父为基底，子覆盖）
        const QString pn = normalizeModelKey(model.parent);
        auto pit = m_models.constFind(pn);
        if (pit != m_models.constEnd())
        {
            for (auto mit = pit.value().textures.constBegin();
                 mit != pit.value().textures.constEnd(); ++mit)
                if (!mergedTex.contains(mit.key()))
                    mergedTex.insert(mit.key(), mit.value());
        }
    }
    for (auto mit = model.textures.constBegin(); mit != model.textures.constEnd(); ++mit)
        mergedTex.insert(mit.key(), mit.value());

    // ---- 元素：当前模型定义了 elements 即整体替换父链元素（递归停止） ----
    if (model.hasElements)
    {
        const QJsonArray elems = model.elemObj.value(QStringLiteral("elements")).toArray();
        for (const QJsonValue &ev : elems)
        {
            const QJsonObject eo = ev.toObject();
            ShapeBox sb;
            auto readVec = [&](const char *field, float *out, float dflt) {
                out[0]=out[1]=out[2]=dflt;
                const QJsonArray a = eo.value(QLatin1String(field)).toArray();
                if (a.size() >= 3) {
                    out[0]=float(a.at(0).toDouble()/16.0);
                    out[1]=float(a.at(1).toDouble()/16.0);
                    out[2]=float(a.at(2).toDouble()/16.0);
                }
            };
            readVec("from", sb.from, 0.0f);
            readVec("to",   sb.to,   1.0f);

            const QJsonObject rot = eo.value(QStringLiteral("rotation")).toObject();
            if (!rot.isEmpty() && !rot.value(QStringLiteral("angle")).isUndefined())
            {
                sb.rotated = true;
                const QString axis = rot.value(QStringLiteral("axis")).toString().toLower();
                if (axis == QLatin1String("x")) sb.rotAxis = 0;
                else if (axis == QLatin1String("z")) sb.rotAxis = 2;
                else sb.rotAxis = 1;
                sb.rotAngle = rot.value(QStringLiteral("angle")).toDouble();
                sb.rotOrigin[0]=0.5f; sb.rotOrigin[1]=0.5f; sb.rotOrigin[2]=0.5f;
                const QJsonArray org = rot.value(QStringLiteral("origin")).toArray();
                if (org.size() >= 3) {
                    sb.rotOrigin[0]=float(org.at(0).toDouble()/16.0);
                    sb.rotOrigin[1]=float(org.at(1).toDouble()/16.0);
                    sb.rotOrigin[2]=float(org.at(2).toDouble()/16.0);
                }
            }

            const QJsonObject faces = eo.value(QStringLiteral("faces")).toObject();
            for (auto fit = faces.constBegin(); fit != faces.constEnd(); ++fit)
            {
                const QJsonObject fo = fit.value().toObject();
                ShapeFace sf;
                sf.dir = fit.key();
                const QString texVar = fo.value(QStringLiteral("texture")).toString();
                QString resolved;
                if (!texVar.isEmpty() && resolveTexture(texVar, mergedTex, resolved))
                    sf.texture = resolved;
                const QJsonValue uvVal = fo.value(QStringLiteral("uv"));
                if (uvVal.isArray())
                {
                    const QJsonArray uva = uvVal.toArray();
                    if (uva.size() >= 4)
                    {
                        for (int i = 0; i < 4; ++i)
                            sf.uv[i] = float(uva.at(i).toDouble());
                        sf.hasUv = true;
                    }
                }
                sb.faces.append(sf);
            }
            if (!sb.isEmpty())
                boxes.append(sb);
        }
        return;
    }

    // ---- 当前模型无元素：向父链继续收集 ----
    if (!model.parent.isEmpty())
        collectElements(model.parent, &mergedTex, boxes, visited);
}

// ============================================================================
// blockstate -> 形状
// ============================================================================

bool BlockShapeLoader::shapeFor(const QString &blockStateName, BlockShapes &out) const
{
    out = BlockShapes();
    if (!m_loaded)
        return false;

    QString bs = blockStateName.trimmed();
    const int lb = bs.indexOf(QLatin1Char('['));
    QString type = lb >= 0 ? bs.left(lb).trimmed() : bs.trimmed();
    if (type.startsWith(QLatin1String("minecraft:")))
        type = type.mid(10);

    QMap<QString, QString> props;
    if (lb >= 0)
    {
        const int rb = bs.lastIndexOf(QLatin1Char(']'));
        const QString inner = bs.mid(lb + 1, rb >= 0 ? rb - lb - 1 : bs.size());
        const QStringList pairs = inner.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &p : pairs)
        {
            const int eq = p.indexOf(QLatin1Char('='));
            if (eq > 0)
                props.insert(p.left(eq).trimmed(), p.mid(eq + 1).trimmed());
        }
    }

    auto bsIt = m_blockstates.constFind(type);
    if (bsIt == m_blockstates.constEnd())
        return false;
    const QJsonObject bsobj = bsIt.value();

    bool gotAny = false;
    QStringList visited;

    // ---- variants ----
    const QJsonValue varsVal = bsobj.value(QStringLiteral("variants"));
    if (varsVal.isObject())
    {
        const QJsonObject vars = varsVal.toObject();
        // 空 key 是默认；否则按属性匹配（按覆盖度取最匹配）
        QString bestKey;
        int bestScore = -1;
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        {
            const QString key = it.key();
            if (key.isEmpty()) { if (bestScore < 0) { bestKey = key; bestScore = 0; } continue; }
            int score = 0;
            bool ok = true;
            const QStringList conds = key.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &c : conds)
            {
                const int eq = c.indexOf(QLatin1Char('='));
                if (eq <= 0) { ok = false; break; }
                const QString pk = c.left(eq).trimmed();
                const QString pv = c.mid(eq + 1).trimmed();
                if (props.contains(pk) && props.value(pk) == pv) score += 1;
                else { ok = false; break; }
            }
            if (!ok) continue;
            if (score > bestScore) { bestScore = score; bestKey = key; }
        }
        if (vars.contains(QStringLiteral("")) && (bestKey.isNull() || bestKey.isEmpty()))
            bestKey = QStringLiteral("");

        if (!bestKey.isNull() && vars.contains(bestKey))
        {
            QList<ModelRef> refs;
            appendModelRefs(vars.value(bestKey), QString(), refs);
            for (const ModelRef &r : refs)
            {
                if (r.model.isEmpty())
                    continue;
                const int before = out.boxes.size();
                collectElements(normalizeModelKey(r.model), nullptr, out.boxes, visited);
                applyBlockRotation(out.boxes, before,
                                   float(r.rotX), float(r.rotY));
            }
            gotAny = !refs.isEmpty();
        }
    }
    else
    {
        // ---- multipart：组合所有命中的 part ----
        const QJsonValue mpVal = bsobj.value(QStringLiteral("multipart"));
        if (mpVal.isArray())
        {
            const QJsonArray mp = mpVal.toArray();
            for (const QJsonValue &pv : mp)
            {
                const QJsonObject po = pv.toObject();
                bool matched = true;
                const QJsonObject when = po.value(QStringLiteral("when")).toObject();
                // OR 组：任一组全部条件满足即命中
                const QJsonValue orVal = when.value(QStringLiteral("OR"));
                if (orVal.isArray())
                {
                    auto andOk = [&](const QJsonObject &conds) {
                        for (auto wit = conds.constBegin(); wit != conds.constEnd(); ++wit)
                        {
                            const QJsonValue wv = wit.value();
                            if (wv.isArray())
                            {
                                bool hit = false;
                                for (const QJsonValue &ov : wv.toArray())
                                    if (ov.toString() == props.value(wit.key())) { hit = true; break; }
                                if (!hit) return false;
                            }
                            else if (props.value(wit.key()) != wv.toString())
                                return false;
                        }
                        return true;
                    };
                    matched = false;
                    const QJsonArray ors = orVal.toArray();
                    for (const QJsonValue &grp : ors)
                    {
                        if (grp.isObject() && andOk(grp.toObject())) { matched = true; break; }
                    }
                }
                else
                {
                    for (auto wit = when.constBegin(); wit != when.constEnd(); ++wit)
                    {
                        const QString wk = wit.key();
                        const QJsonValue wv = wit.value();
                        if (wv.isArray())
                        {
                            bool hit = false;
                            for (const QJsonValue &ov : wv.toArray())
                                if (ov.toString() == props.value(wk)) { hit = true; break; }
                            if (!hit) { matched = false; break; }
                        }
                        else if (props.value(wk) != wv.toString())
                        {
                            matched = false;
                            break;
                        }
                    }
                }
                if (!matched)
                    continue;

                // multipart part 的模型在 "apply" 键中（对象含 model/x/y）；兼容直接给字符串
                QList<ModelRef> refs;
                appendModelRefs(po.value(QStringLiteral("apply")), QString(), refs);
                for (const ModelRef &r : refs)
                {
                    if (r.model.isEmpty())
                        continue;
                    const int before = out.boxes.size();
                    collectElements(normalizeModelKey(r.model), nullptr, out.boxes, visited);
                    applyBlockRotation(out.boxes, before,
                                       float(r.rotX), float(r.rotY));
                }
                if (!refs.isEmpty())
                    gotAny = true;
            }
        }
    }

    return gotAny && !out.empty();
}
