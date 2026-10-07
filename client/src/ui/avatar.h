#pragma once
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QString>

namespace im {

// 首字圆形头像（微信风格默认头像）：未上传头像时按昵称首字生成，同人同色
inline QPixmap avatar_pixmap(const QString& name, int size = 36) {
  static const char* kPalette[] = {
      "#6B9FCA", "#67B279", "#C9A86A", "#B08CC0", "#D98C8C", "#7FB3B0", "#8FA3C8", "#C48A98",
  };
  const int n = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
  const QColor bg(kPalette[qHash(name) % n]);

  QPixmap pm(size, size);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(bg);
  p.drawEllipse(0, 0, size, size);
  p.setPen(QColor("#FFFFFF"));
  QFont f;
  f.setPixelSize(size * 9 / 20);
  f.setBold(true);
  p.setFont(f);
  p.drawText(pm.rect(), Qt::AlignCenter,
             name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper());
  return pm;
}

}  // namespace im
