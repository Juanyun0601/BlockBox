// 首页皮肤面板空态复现探针：复刻 HomePage 的 HomeSkinPanel + 「添加首个账户」按钮布局，
// 窗口显示后自截图保存，用于离线排查空态内容被裁剪的问题。
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QRect>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

static constexpr qreal kHeroRadius = 16.0;

class HomeSkinBorderOverlay : public QWidget
{
public:
  explicit HomeSkinBorderOverlay(QWidget *parent) : QWidget(parent)
  {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
  }
protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#e8eaed"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), kHeroRadius, kHeroRadius);
  }
};

class HomeSkinPanel : public QWidget
{
public:
  QVBoxLayout *contentLayout = nullptr;
  explicit HomeSkinPanel(QWidget *parent = nullptr) : QWidget(parent)
  {
    contentLayout = new QVBoxLayout(this);
    contentLayout->setContentsMargins(1, 1, 1, 1);
    contentLayout->setSpacing(0);
    m_overlay = new HomeSkinBorderOverlay(this);
  }
  void finalizeOverlay()
  {
    m_overlay->raise();
    m_overlay->setGeometry(rect());
  }
protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect(), kHeroRadius, kHeroRadius);
    p.fillPath(path, QColor("#ffffff"));
  }
  void resizeEvent(QResizeEvent *event) override
  {
    QWidget::resizeEvent(event);
    m_overlay->setGeometry(rect());
    m_overlay->raise();
    m_overlay->update();
  }
private:
  HomeSkinBorderOverlay *m_overlay = nullptr;
};

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);
  app.setStyleSheet(QStringLiteral(
      "QLabel#homeIconChip { background-color: rgba(16,185,129,0.24); border-radius: 8px; }"
      "QLabel#homeAccountName { background-color: transparent; color: #111; font-size: 13px; font-weight: 600; border: none; }"
      "QLabel#homeAccountType { background-color: transparent; color: #888; font-size: 11px; border: none; }"
      "QPushButton#homeSkinAddBtn { background-color: transparent; border: none; border-radius: 16px; }"));

  auto *panel = new HomeSkinPanel();
  panel->setFixedWidth(220);
  panel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  auto *btn = new QPushButton();
  btn->setObjectName("homeSkinAddBtn");
  btn->setFlat(true);
  btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  auto *bl = new QVBoxLayout(btn);
  bl->setContentsMargins(12, 16, 12, 16);
  bl->setSpacing(8);
  bl->setAlignment(Qt::AlignCenter);

  auto *icon = new QLabel(btn);
  icon->setObjectName("homeIconChip");
  icon->setFixedSize(44, 44);
  icon->setAttribute(Qt::WA_StyledBackground, true);
  icon->setAlignment(Qt::AlignCenter);
  icon->setText("+");
  icon->setAlignment(Qt::AlignCenter);
  bl->addWidget(icon, 0, Qt::AlignHCenter);

  auto *t = new QLabel(QStringLiteral("添加首个账户"), btn);
  t->setObjectName("homeAccountName");
  t->setAlignment(Qt::AlignCenter);
  t->setWordWrap(true);
  bl->addWidget(t);

  auto *d = new QLabel(QStringLiteral("添加账户后在这里展示皮肤模型"), btn);
  d->setObjectName("homeAccountType");
  d->setAlignment(Qt::AlignCenter);
  d->setWordWrap(true);
  bl->addWidget(d);

  panel->contentLayout->addWidget(btn, 1);
  panel->finalizeOverlay();

  panel->resize(220, 900);
  panel->show();

  QTimer::singleShot(800, [panel, btn, icon, t, d]() {
    QFile f("D:/BlockBox/.tmp/homeprobe/probe_geo.txt");
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    auto rectStr = [](const QRect &r) {
      return QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    };
    auto line = [&](const QString &s) { f.write(s.toUtf8() + "\n"); };
    line("panel " + rectStr(panel->geometry()));
    line(QString("btn %1 vis=%2").arg(rectStr(btn->geometry())).arg(btn->isVisible()));
    line(QString("icon %1 vis=%2").arg(rectStr(icon->geometry())).arg(icon->isVisible()));
    line(QString("t %1 vis=%2 hint=%3x%4").arg(rectStr(t->geometry()))
             .arg(t->isVisible()).arg(t->sizeHint().width()).arg(t->sizeHint().height()));
    line(QString("d %1 vis=%2 hint=%3x%4").arg(rectStr(d->geometry()))
             .arg(d->isVisible()).arg(d->sizeHint().width()).arg(d->sizeHint().height()));
    f.close();
    panel->grab().save("D:/BlockBox/.tmp/homeprobe/probe_panel.png");
    QApplication::quit();
  });
  return app.exec();
}
