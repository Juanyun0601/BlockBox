#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QTextStream>
#include <zlib.h>

int main(int argc, char* argv[])
{
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);

  const QString path = QDir::tempPath() + "/blockbox_modpack_probe/out-cf.zip";
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
  {
    out << "open failed\n";
    return 1;
  }
  const qint64 size = f.size();
  out << "size=" << size << "\n";

  // 读尾部
  const qint64 searchStart = qMax(qint64(0), size - 65557);
  f.seek(searchStart);
  const QByteArray tail = f.read(size - searchStart);
  out << "tail size=" << tail.size() << "\n";

  // 找 EOCD
  bool found = false;
  qint64 centralDirOffset = 0;
  quint16 totalEntries = 0;
  for (int i = tail.size() - 22; i >= 0; --i)
  {
    if (static_cast<quint8>(tail[i]) == 0x50 && static_cast<quint8>(tail[i + 1]) == 0x4b
        && static_cast<quint8>(tail[i + 2]) == 0x05 && static_cast<quint8>(tail[i + 3]) == 0x06)
    {
      out << "EOCD found at tail offset " << i << "\n";
      QDataStream ds(tail.mid(i, 22));
      ds.setByteOrder(QDataStream::LittleEndian);
      ds.skipRawData(10);
      quint16 entryCount = 0;
      quint32 cdSize = 0;
      quint32 cdOffset = 0;
      ds >> entryCount >> cdSize >> cdOffset;
      out << "entryCount=" << entryCount << " cdSize=" << cdSize << " cdOffset=" << cdOffset << "\n";
      centralDirOffset = cdOffset;
      totalEntries = entryCount;
      found = true;
      break;
    }
  }
  if (!found)
  {
    out << "EOCD NOT found\n";
    return 1;
  }

  // 读中央目录
  if (!f.seek(centralDirOffset))
  {
    out << "seek to CD failed\n";
    return 1;
  }
  const int cdEntryBaseSize = 46;
  for (quint16 i = 0; i < totalEntries; ++i)
  {
    const QByteArray header = f.read(cdEntryBaseSize);
    out << "entry " << i << " header size=" << header.size() << "\n";
    if (header.size() < cdEntryBaseSize)
    {
      out << "  truncated\n";
      break;
    }
    QDataStream ds(header);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 signature = 0;
    ds >> signature;
    out << "  sig=0x" << QString::number(signature, 16) << "\n";
    if (signature != 0x02014b50)
    {
      out << "  BAD signature\n";
      break;
    }
    // 跳到 fileNameLength（偏移 28）
    ds.skipRawData(24);
    quint16 fileNameLength = 0, extraFieldLength = 0, fileCommentLength = 0;
    ds >> fileNameLength >> extraFieldLength >> fileCommentLength;
    const QByteArray fileNameBytes = f.read(fileNameLength);
    out << "  name=" << fileNameBytes << "\n";
    f.read(extraFieldLength + fileCommentLength);
  }
  out << "DONE\n";
  return 0;
}
