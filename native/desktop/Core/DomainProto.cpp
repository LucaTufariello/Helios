#include "DomainProto.h"
#include "DomainFeeds.h"
#include <QJsonArray>
#include <QSet>
#include <QTimeZone>
#include <QtEndian>
#include <bit>
namespace helios {
namespace DomainProto {
namespace {
std::optional<quint64> varint(const QByteArray &bytes, qsizetype &offset) {
  quint64 value = 0;
  for (int shift = 0; shift <= 63; shift += 7) {
    if (offset >= bytes.size())
      return {};
    auto byte = quint8(bytes[offset++]);
    if (shift == 63 && byte > 1)
      return {};
    value |= quint64(byte & 127) << shift;
    if (!(byte & 128))
      return value;
  }
  return {};
}
} // namespace
std::optional<QVector<DomainProtoField>> read(const QByteArray &bytes) {
  if (bytes.size() > 16 * 1024 * 1024)
    return {};
  qsizetype offset = 0;
  QVector<DomainProtoField> out;
  while (offset < bytes.size()) {
    auto tag = varint(bytes, offset);
    if (!tag || (*tag >> 3) == 0 || (*tag >> 3) > 536870911 || out.size() >= 100000)
      return {};
    DomainProtoField f;
    f.number = *tag >> 3;
    switch (*tag & 7) {
    case 0:
      f.integer = varint(bytes, offset);
      if (!f.integer)
        return {};
      break;
    case 2: {
      auto length = varint(bytes, offset);
      if (!length || *length > quint64(bytes.size() - offset))
        return {};
      f.payload = bytes.mid(offset, qsizetype(*length));
      offset += qsizetype(*length);
      break;
    }
    case 5:
      if (offset > bytes.size() - 4)
        return {};
      f.fixed = qFromLittleEndian<quint32>(bytes.constData() + offset);
      offset += 4;
      break;
    case 1:
      if (offset > bytes.size() - 8)
        return {};
      f.fixed64 = qFromLittleEndian<quint64>(bytes.constData() + offset);
      offset += 8;
      break;
    default:
      return {};
    }
    out.append(f);
  }
  return out;
}
std::optional<QVector<quint64>> packed(const QByteArray &bytes) {
  qsizetype offset = 0;
  QVector<quint64> out;
  while (offset < bytes.size()) {
    if (out.size() >= 100000)
      return {};
    auto n = varint(bytes, offset);
    if (!n)
      return {};
    out.append(*n);
  }
  return out;
}
const DomainProtoField *find(const QVector<DomainProtoField> &fields, quint64 number) {
  for (const auto &f : fields)
    if (f.number == number)
      return &f;
  return nullptr;
}
} // namespace DomainProto
namespace DomainParsers {
namespace {
DomainSnapshot badProto() {
  DomainSnapshot s;
  s.error = "Source returned malformed protobuf. Previous records are retained.";
  return s;
}
QByteArray payload(const QVector<DomainProtoField> &fields, quint64 n) {
  auto f = DomainProto::find(fields, n);
  return f ? f->payload : QByteArray{};
}
QString text(const QVector<DomainProtoField> &fields, quint64 n) {
  return clean(QString::fromUtf8(payload(fields, n)), 160);
}
std::optional<double> floatValue(const QVector<DomainProtoField> &fields, quint64 n) {
  auto f = DomainProto::find(fields, n);
  if (!f || !f->fixed)
    return {};
  double value = std::bit_cast<float>(*f->fixed);
  if (!std::isfinite(value))
    return {};
  return value;
}
} // namespace
DomainSnapshot transit(const QByteArray &bytes, const TransitProvider &provider, QDateTime now) {
  const auto root = DomainProto::read(bytes);
  if (!root)
    return badProto();
  auto header = DomainProto::read(payload(*root, 1));
  if (!header || payload(*header, 1).isEmpty())
    return badProto();
  DomainSnapshot out;
  out.source = provider.attribution;
  out.retrievedAt = now;
  out.notices = {
      "Vehicle fixes older than 3 minutes are excluded. Missing observation times stay unknown."};
  QSet<QString> seen;
  for (const auto &entry : *root)
    if (entry.number == 2) {
      auto entity = DomainProto::read(entry.payload);
      if (!entity)
        return badProto();
      const auto deleted = DomainProto::find(*entity, 2);
      if (deleted && deleted->integer == 1)
        continue;
      auto vehicle = DomainProto::find(*entity, 4);
      if (!vehicle)
        continue;
      auto fields = DomainProto::read(vehicle->payload);
      if (!fields)
        return badProto();
      auto pos = DomainProto::read(payload(*fields, 2));
      if (!pos)
        return badProto();
      auto lat = floatValue(*pos, 1), lon = floatValue(*pos, 2);
      if (!lat || !lon || !Coordinate{*lat, *lon}.valid()) {
        ++out.rejected;
        continue;
      }
      auto descriptor = DomainProto::read(payload(*fields, 8)),
           trip = DomainProto::read(payload(*fields, 1));
      if (!descriptor || !trip)
        return badProto();
      auto raw = text(*descriptor, 1);
      if (raw.isEmpty())
        raw = text(*entity, 1);
      const auto id = "transit:" + provider.id + ":" + raw;
      if (raw.isEmpty() || seen.contains(id)) {
        ++out.rejected;
        continue;
      }
      seen.insert(id);
      QDateTime at;
      auto timestamp = DomainProto::find(*fields, 5);
      if (timestamp && timestamp->integer) {
        if (*timestamp->integer > 8640000000000ULL) {
          ++out.rejected;
          continue;
        }
        at = QDateTime::fromSecsSinceEpoch(qint64(*timestamp->integer), QTimeZone::UTC);
        if (at.secsTo(now) > 180 || now.secsTo(at) > 120) {
          ++out.rejected;
          continue;
        }
      }
      DomainRecord r;
      r.id = id;
      r.feed = "transit";
      r.coordinate = {*lat, *lon};
      r.source = provider.attribution;
      r.observedAt = at;
      auto label = text(*descriptor, 2);
      r.label = provider.name + " · " + (label.isEmpty() ? raw : label);
      r.details = {{"Vehicle ID", raw},
                   {"Operator", provider.name},
                   {"Route", text(*trip, 5)},
                   {"Data license", provider.license},
                   {"Source URL", provider.licenseUrl},
                   {"Position basis", "Operator-reported GTFS-realtime fix"}};
      if (auto speed = floatValue(*pos, 5); speed && *speed >= 0)
        r.details["Speed"] = QString::number(*speed * 3.6, 'f', 1) + " km/h";
      r.raw = {{"protobufVehicleBase64", QString::fromLatin1(vehicle->payload.toBase64())}};
      out.records.append(r);
    }
  return out;
}
} // namespace DomainParsers
} // namespace helios
