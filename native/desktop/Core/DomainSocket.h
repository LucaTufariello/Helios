#pragma once
#include "DomainFeeds.h"
#include <QSslSocket>
#include <QTimer>
namespace godeye {
// Minimal RFC 6455 client for the single fixed AIS endpoint. No compression/extensions.
class DomainSocket : public QObject {
  Q_OBJECT
public:
  explicit DomainSocket(QObject *parent = nullptr);
  void start(QString key);
  void stop();
  bool running() const {
    return active_;
  }
  DomainSnapshot snapshot() const;
  static QByteArray clientFrame(quint8 opcode, const QByteArray &payload);
  // Consumes one bounded server frame. nullopt means incomplete; false is invalid.
  static std::optional<bool> serverFrame(QByteArray &buffer, quint8 &opcode, bool &final,
                                         QByteArray &payload);
signals:
  void changed();

private:
  QSslSocket socket_;
  QTimer expire_, publish_;
  QByteArray buffer_, accept_, fragmented_;
  QString key_, issue_;
  quint8 fragmentedOpcode_ = 0;
  bool handshake_ = false, active_ = false;
  QDateTime started_, lastMessage_;
  QMap<QString, std::pair<QDateTime, DomainRecord>> records_;
  void read();
  void fail(const QString &error);
};
} // namespace godeye
