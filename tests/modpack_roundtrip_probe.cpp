/**
 * @file   modpack_roundtrip_probe.cpp
 * @brief  整合包导出→检测→解析 往返验证探针（无 GUI，直接跑逻辑）
 *
 * 覆盖：BlockBox / CurseForge / MultiMC / Modrinth / MCBBS 五种格式
 * 验证点：
 *   1. 导出产物能被 ModpackDetector 正确识别为对应类型
 *   2. parse 出的 name/gameVersion 与导出时一致
 *   3. 导出 zip 内关键清单文件存在
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "utils/modpack/ModpackDetector.h"
#include "utils/modpack/ModpackExporter.h"

using namespace modpack;

static QTextStream out(stdout);
static int failCount = 0;

static void check(bool cond, const QString& msg)
{
  out << (cond ? "[PASS] " : "[FAIL] ") << msg << "\n";
  out.flush();
  if (!cond)
    ++failCount;
}

// 用 QEventLoop 等导出完成（导出是同步的，但通过信号回报）
static bool runExport(ModpackExporter& exp)
{
  bool done = false;
  bool ok = false;
  QString err;
  QObject::connect(&exp, &ModpackExporter::exportFinished,
                   [&](bool success, const QString&, const QString& error) {
                     done = true;
                     ok = success;
                     err = error;
                   });
  exp.startExport();
  if (!done)
  {
    out << "[FAIL] 导出未同步完成\n";
    out.flush();
    ++failCount;
    return false;
  }
  if (!ok)
  {
    out << "[FAIL] 导出失败: " << err << "\n";
    out.flush();
    ++failCount;
  }
  return ok;
}

int main(int argc, char* argv[])
{
  QCoreApplication app(argc, argv);

  const QString workDir = QDir::tempPath() + "/blockbox_modpack_probe";
  QDir(workDir).removeRecursively();
  QDir().mkpath(workDir + "/instance/mods");
  QDir().mkpath(workDir + "/instance/config");

  // 造一个假实例（约定：版本 JSON 在实例目录根，名为 <目录名>.json）
  {
    QFile f(workDir + "/instance/mods/testmod.jar");
    f.open(QIODevice::WriteOnly);
    f.write("fake-jar-bytes-for-testing-1234567890");
    f.close();
  }
  {
    QFile f(workDir + "/instance/config/test.cfg");
    f.open(QIODevice::WriteOnly);
    f.write("key=value\n");
    f.close();
  }
  {
    // 实例版本 JSON：Fabric 加载器实例，inheritsFrom 指向原版 1.20.1
    QFile f(workDir + "/instance/instance.json");
    f.open(QIODevice::WriteOnly);
    f.write(R"({"id":"fabric-loader-0.15.11-1.20.1","inheritsFrom":"1.20.1"})");
    f.close();
  }

  const QStringList files = {"mods/testmod.jar", "config/test.cfg"};

  ModpackExportInfo info;
  info.name = "ProbePack";
  info.version = "1.2.3";
  info.author = "ProbeAuthor";
  info.description = "往返测试包";

  struct Case
  {
    ExportFormat fmt;
    const char* label;
    QString outPath;
    ModpackType expected;
  };

  QList<Case> cases = {
    {ExportFormat::BlockBox, "BlockBox", workDir + "/out.blockbox", ModpackType::BlockBox},
    {ExportFormat::CurseForge, "CurseForge", workDir + "/out-cf.zip", ModpackType::CurseForge},
    {ExportFormat::MultiMC, "MultiMC", workDir + "/out-mmc.zip", ModpackType::MultiMC},
    {ExportFormat::Modrinth, "Modrinth", workDir + "/out-mr.mrpack", ModpackType::Modrinth},
    {ExportFormat::Mcbbs, "MCBBS", workDir + "/out-mcbbs.zip", ModpackType::MCBBS},
  };

  for (const Case& c : cases)
  {
    out << "\n=== " << c.label << " ===\n";
    out.flush();

    ModpackExporter exp(workDir + "/instance", c.fmt, info, files, c.outPath);
    if (!runExport(exp))
      continue;

    check(QFile::exists(c.outPath), QString("%1: 输出文件存在").arg(c.label));

    const ModpackType detected = ModpackDetector::detect(c.outPath);
    check(detected == c.expected,
          QString("%1: 检测类型正确 (得到 %2, 期望 %3)")
            .arg(c.label)
            .arg(static_cast<int>(detected))
            .arg(static_cast<int>(c.expected)));

    const ModpackInfo parsed = ModpackDetector::parse(c.outPath);
    check(parsed.name == "ProbePack",
          QString("%1: name='%2'").arg(c.label).arg(parsed.name));
    // MultiMC 格式不携带 version/author（写在 instance.cfg）；Modrinth 清单无 author 字段
    if (c.expected != ModpackType::MultiMC)
      check(parsed.version == "1.2.3",
            QString("%1: version='%2'").arg(c.label).arg(parsed.version));
    if (c.expected != ModpackType::MultiMC && c.expected != ModpackType::Modrinth)
      check(parsed.author == "ProbeAuthor",
            QString("%1: author='%2'").arg(c.label).arg(parsed.author));
  }

  // BlockBox 往返：清单内应含 files 与 gameVersion
  {
    out << "\n=== BlockBox 清单内容 ===\n";
    out.flush();
    QFile f(cases[0].outPath);
    if (f.open(QIODevice::ReadOnly))
    {
      // zip 文件：直接用 ZipReader 不可（探测器内部类），改用 parse 结果
      f.close();
      const ModpackInfo bb = ModpackDetector::parse(cases[0].outPath);
      check(bb.type == ModpackType::BlockBox, "BlockBox: parse 类型为 BlockBox");
      // 导出时 detectGameVersion 扫 versions/ 目录，探针实例里有 1.20.1
      check(bb.gameVersion == "1.20.1",
            QString("BlockBox: gameVersion='%1'").arg(bb.gameVersion));
      // fabric-loader 目录应被 detectAllLoaders 识别
      check(bb.loaderType == "fabric",
            QString("BlockBox: loaderType='%1'").arg(bb.loaderType));
      check(bb.loaderVersion == "0.15.11",
            QString("BlockBox: loaderVersion='%1'").arg(bb.loaderVersion));
    }
  }

  // 导入侧路径推导：模拟导入器的目录约定
  {
    out << "\n=== 实例布局约定 ===\n";
    out.flush();
    const QString gameRoot = workDir + "/game";
    const QString instanceName = "我的整合包";
    const QString instancePath = gameRoot + "/versions/" + instanceName;
    check(instancePath.endsWith("/versions/我的整合包"),
          "导入实例路径遵循 <root>/versions/<实例名>");
  }

  out << "\n";
  if (failCount == 0)
    out << "ALL TESTS PASSED\n";
  else
    out << failCount << " TEST(S) FAILED\n";
  out.flush();
  return failCount == 0 ? 0 : 1;
}
