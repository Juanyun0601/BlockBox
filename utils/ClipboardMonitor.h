#ifndef CLIPBOARDMONITOR_H
#define CLIPBOARDMONITOR_H

#include <QObject>
#include <QSet>
#include "content/ContentData.h"

class QClipboard;

struct ClipboardLinkInfo
{
    QString source;       // "curseforge" or "modrinth"
    QString slug;         // project slug/id
    ContentType contentType; // content type
    QString displayName;  // display name for dialog
};

class ClipboardMonitor : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardMonitor(QObject *parent = nullptr);
    ~ClipboardMonitor();

    void start();
    void stop();
    void addRejected(const QString &url);

signals:
    void linkDetected(const ClipboardLinkInfo &info);

private slots:
    void onClipboardChanged();

private:
    ClipboardLinkInfo parseUrl(const QString &url) const;
    ContentType curseforgeTypeToContentType(const QString &cfType) const;
    ContentType modrinthTypeToContentType(const QString &mrType) const;

    QClipboard *m_clipboard;
    QSet<QString> m_rejectedLinks; // 已拒绝的链接，避免重复弹窗
};

#endif // CLIPBOARDMONITOR_H