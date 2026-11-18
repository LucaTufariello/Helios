#include "DomainSocket.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QtEndian>
namespace helios {
DomainSocket::DomainSocket(QObject *parent) : QObject(parent) {
  socket_.setReadBufferSize(512 * 1024);
  connect(&socket_, &QSslSocket::encrypted, this, [this] {
    QByteArray nonce(16, Qt::Uninitialized);
    for (auto &c : nonce)
      c = char(QRandomGenerator::system()->generate() & 255);
    const auto key = nonce.toBase64();
    accept_ = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                       QCryptographicHash::Sha1)
                  .toBase64();
    socket_.write("GET /v0/stream HTTP/1.1\r\nHost: stream.aisstream.io\r\nUpgrade: "
                  "websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " +
                  key + "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: HeliosNative/1.0\r\n\r\n");
  });
  connect(&socket_, &QSslSocket::readyRead, this, &DomainSocket::read);
  connect(&socket_, &QSslSocket::errorOccurred, this, [this] {
    if (active_)
      fail("AIS TLS connection is unavailable. Check the key or disable and enable the source to "
           "reconnect.");
  });
  connect(&socket_, &QSslSocket::disconnected, this, [this] {
    if (active_ && issue_.isEmpty())
      fail("AIS connection closed. Retained positions are stale; refresh to reconnect.");
  });
  expire_.setInterval(15000);
  publish_.setSingleShot(true);
  publish_.setInterval(1000);
  connect(&publish_, &QTimer::timeout, this, [this] { emit changed(); });
  connect(&expire_, &QTimer::timeout, this, [this] {
    const auto now = QDateTime::currentDateTimeUtc();
    for (auto i = records_.begin(); i != records_.end();) {
      if (i.value().first.secsTo(now) >= 900)
        i = records_.erase(i);
      else
        ++i;
    }
    if (issue_.isEmpty() && started_.isValid() &&
        (lastMessage_.isValid() ? lastMessage_ : started_).secsTo(now) > 120)
      issue_ = "No AIS reports received for more than two minutes. Retained positions are stale.";
    emit changed();
  });
}
void DomainSocket::start(QString key) {
  stop();
  if (key.isEmpty() || key.size() > 256 || key.contains(QChar(0)) || key.contains('\n') ||
      key.contains('\r')) {
    issue_ = "Add an AISStream API key in Provider Settings to receive real vessel positions.";
    emit changed();
    return;
  }
  key_ = key;
  active_ = true;
  started_ = QDateTime::currentDateTimeUtc();
  records_.clear();
  socket_.connectToHostEncrypted("stream.aisstream.io", 443);
  expire_.start();
  QTimer::singleShot(30000, this, [this, started = started_] {
    if (started_ == started && !handshake_ && !key_.isEmpty())
      fail("AIS connection timed out. TLS verification remains enabled.");
  });
  emit changed();
}
void DomainSocket::stop() {
  active_ = false;
  key_.clear();
  socket_.abort();
  expire_.stop();
  publish_.stop();
  handshake_ = false;
  fragmentedOpcode_ = 0;
  buffer_.clear();
  fragmented_.clear();
  accept_.clear();
  issue_.clear();
  started_ = {};
  lastMessage_ = {};
  records_.clear();
}
void DomainSocket::fail(const QString &error) {
  issue_ = error;
  active_ = false;
  key_.clear();
  socket_.abort();
  emit changed();
}
QByteArray DomainSocket::clientFrame(quint8 opcode, const QByteArray &payload) {
  if (payload.size() > 256 * 1024)
    return {};
  QByteArray frame;
  frame.append(char(0x80 | opcode));
  if (payload.size() < 126)
    frame.append(char(0x80 | payload.size()));
  else if (payload.size() <= 65535) {
    frame.append(char(0xfe));
    char data[2];
    qToBigEndian<quint16>(quint16(payload.size()), data);
    frame.append(data, 2);
  } else {
    frame.append(char(0xff));
    char data[8];
    qToBigEndian<quint64>(quint64(payload.size()), data);
    frame.append(data, 8);
  }
  QByteArray mask(4, Qt::Uninitialized);
  for (auto &c : mask)
    c = char(QRandomGenerator::system()->generate() & 255);
  frame += mask;
  for (qsizetype i = 0; i < payload.size(); ++i)
    frame.append(char(quint8(payload[i]) ^ quint8(mask[i % 4])));
  return frame;
}
std::optional<bool> DomainSocket::serverFrame(QByteArray &buffer, quint8 &opcode, bool &final,
                                              QByteArray &payload) {
  if (buffer.size() < 2)
    return {};
  const auto a = quint8(buffer[0]), b = quint8(buffer[1]);
  opcode = a & 15;
  final = a & 128;
  if ((a & 112) || (b & 128) ||
      !(opcode == 0 || opcode == 1 || opcode == 2 || opcode == 8 || opcode == 9 || opcode == 10))
    return false;
  quint64 length = b & 127;
  qsizetype begin = 2;
  if (length == 126) {
    if (buffer.size() < 4)
      return {};
    length = qFromBigEndian<quint16>(buffer.constData() + 2);
    begin = 4;
    if (length < 126)
      return false;
  } else if (length == 127) {
    if (buffer.size() < 10)
      return {};
    length = qFromBigEndian<quint64>(buffer.constData() + 2);
    begin = 10;
    if (length < 65536)
      return false;
  }
  if (length > 256 * 1024 || (opcode >= 8 && (!final || length > 125)))
    return false;
  if (quint64(buffer.size() - begin) < length)
    return {};
  payload = buffer.mid(begin, qsizetype(length));
  buffer.remove(0, begin + qsizetype(length));
  return true;
}
void DomainSocket::read() {
  buffer_ += socket_.read(128 * 1024);
  if (buffer_.size() > 512 * 1024) {
    fail("AIS transport exceeded its bounded message buffer.");
    return;
  }
  if (!handshake_) {
    const auto end = buffer_.indexOf("\r\n\r\n");
    if (end < 0) {
      if (buffer_.size() > 16384)
        fail("AIS returned an invalid upgrade response.");
      return;
    }
    const auto headers = buffer_.left(end).split('\n');
    QMap<QByteArray, QByteArray> values;
    for (const auto &raw : headers.mid(1)) {
      const auto line = raw.trimmed();
      const auto colon = line.indexOf(':');
      if (colon > 0) {
        const auto name = line.left(colon).toLower();
        if (values.contains(name)) {
          fail("AIS returned duplicate upgrade headers.");
          return;
        }
        values[name] = line.mid(colon + 1).trimmed();
      }
    }
    bool connectionUpgrade = false;
    for (const auto &token : values["connection"].toLower().split(','))
      connectionUpgrade |= token.trimmed() == "upgrade";
    if (headers.isEmpty() || !headers[0].startsWith("HTTP/1.1 101 ") ||
        values["upgrade"].toLower() != "websocket" || !connectionUpgrade ||
        values["sec-websocket-accept"] != accept_ || values.contains("sec-websocket-extensions")) {
      fail("AIS refused the secure WebSocket upgrade.");
      return;
    }
    buffer_.remove(0, end + 4);
    handshake_ = true;
    const QJsonObject subscription{
        {"APIKey", key_},
        {"BoundingBoxes", QJsonArray{QJsonArray{QJsonArray{-90, -180}, QJsonArray{90, 180}}}},
        {"FilterMessageTypes", QJsonArray{"PositionReport", "StandardClassBPositionReport",
                                          "ExtendedClassBPositionReport"}}};
    socket_.write(clientFrame(1, QJsonDocument(subscription).toJson(QJsonDocument::Compact)));
    key_.clear();
  }
  bool changed = false;
  int consumed = 0;
  for (int i = 0; i < 64; ++i) {
    quint8 opcode;
    bool final;
    QByteArray payload;
    auto parsed = serverFrame(buffer_, opcode, final, payload);
    if (!parsed)
      break;
    if (!*parsed) {
      fail("AIS returned an invalid or oversized WebSocket frame.");
      return;
    }
    ++consumed;
    if (opcode == 8) {
      fail("AIS closed the subscription. Refresh to reconnect.");
      return;
    }
    if (opcode == 9) {
      socket_.write(clientFrame(10, payload));
      continue;
    }
    if (opcode == 10)
      continue;
    if (opcode == 0) {
      if (!fragmentedOpcode_) {
        fail("AIS returned an invalid fragmented message.");
        return;
      }
      fragmented_ += payload;
    } else {
      if (fragmentedOpcode_) {
        fail("AIS returned overlapping messages.");
        return;
      }
      fragmentedOpcode_ = opcode;
      fragmented_ = payload;
    }
    if (fragmented_.size() > 256 * 1024) {
      fail("AIS message exceeded its size limit.");
      return;
    }
    if (!final)
      continue;
    QString error;
    auto record = DomainParsers::ais(fragmented_, &error);
    fragmented_.clear();
    fragmentedOpcode_ = 0;
    if (!error.isEmpty()) {
      fail(error);
      return;
    }
    if (record) {
      lastMessage_ = QDateTime::currentDateTimeUtc();
      records_[record->id] = {lastMessage_, *record};
      issue_.clear();
      changed = true;
      if (records_.size() > 5000) {
        auto oldest = records_.begin();
        for (auto entry = records_.begin(); entry != records_.end(); ++entry)
          if (entry->first < oldest->first)
            oldest = entry;
        records_.erase(oldest);
      }
    }
  }
  if (changed && !publish_.isActive())
    publish_.start();
  if (socket_.bytesAvailable() > 0 || (consumed == 64 && !buffer_.isEmpty()))
    QTimer::singleShot(1, this, [this] {
      if (active_)
        read();
    });
}
DomainSnapshot DomainSocket::snapshot() const {
  DomainSnapshot out;
  out.source = "AISStream";
  out.retrievedAt = lastMessage_;
  out.error = issue_;
  out.notices = {lastMessage_.isValid() ? "Actual received fixes retained for up to 15 minutes. "
                                          "Reception gaps are not interpolated."
                                        : "Connecting; awaiting actual AIS reports."};
  for (const auto &value : records_)
    out.records.append(value.second);
  return out;
}
} // namespace helios
