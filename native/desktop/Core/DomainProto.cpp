#include "DomainProto.h"
#include "DomainFeeds.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QSet>
#include <QTimeZone>
#include <QtEndian>
#include <bit>
#include <cmath>
#include <numbers>
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
DomainSnapshot traffic(const QByteArray &bytes, TileID tile, QDateTime now) {
  const auto root = DomainProto::read(bytes);
  if (!root)
    return badProto();
  DomainSnapshot out;
  out.source = "© TomTom";
  out.retrievedAt = now;
  QSet<QString> identifiers;
  out.notices = {"Relative provider road-flow estimates; acquisition time is unknown. Green ≥85%, "
                 "amber ≥55%, red below 55% or closure."};
  for (const auto &layerField : *root)
    if (layerField.number == 3) {
      auto layer = DomainProto::read(layerField.payload);
      if (!layer)
        return badProto();
      if (payload(*layer, 1) != "Traffic flow")
        continue;
      auto version = DomainProto::find(*layer, 15);
      if (!version || version->integer != 2)
        return badProto();
      auto extentField = DomainProto::find(*layer, 5);
      const double extent =
          extentField && extentField->integer ? double(*extentField->integer) : 4096;
      if (extent < 256 || extent > 65536)
        return badProto();
      QVector<QString> keys;
      QVector<QJsonValue> values;
      for (const auto &f : *layer) {
        if (f.number == 3)
          keys.append(QString::fromUtf8(f.payload));
        if (f.number == 4) {
          auto fields = DomainProto::read(f.payload);
          if (!fields)
            return badProto();
          QJsonValue value;
          if (auto s = DomainProto::find(*fields, 1))
            value = QString::fromUtf8(s->payload);
          else if (auto n = DomainProto::find(*fields, 3); n && n->fixed64) {
            const double d = std::bit_cast<double>(*n->fixed64);
            if (std::isfinite(d))
              value = d;
          } else if (auto n = DomainProto::find(*fields, 2); n && n->fixed) {
            const double d = std::bit_cast<float>(*n->fixed);
            if (std::isfinite(d))
              value = d;
          } else if (auto b = DomainProto::find(*fields, 7); b && b->integer)
            value = *b->integer == 1;
          values.append(value);
        }
      }
      for (const auto &feature : *layer)
        if (feature.number == 2) {
          auto fields = DomainProto::read(feature.payload);
          if (!fields) {
            ++out.rejected;
            continue;
          }
          auto type = DomainProto::find(*fields, 3);
          if (!type || type->integer != 2)
            continue;
          auto tags = DomainProto::packed(payload(*fields, 2));
          if (!tags || tags->size() % 2) {
            ++out.rejected;
            continue;
          }
          QJsonObject properties;
          bool valid = true;
          for (int i = 0; i < tags->size(); i += 2) {
            if ((*tags)[i] >= quint64(keys.size()) || (*tags)[i + 1] >= quint64(values.size())) {
              valid = false;
              break;
            }
            properties[keys[int((*tags)[i])]] = values[int((*tags)[i + 1])];
          }
          if (!valid) {
            ++out.rejected;
            continue;
          }
          const bool closed =
              properties["road_closure"].toBool() || properties["road_closure"] == "true";
          auto ratio = number(properties["traffic_level"], false);
          if (ratio && (*ratio < 0 || *ratio > 1))
            ratio.reset();
          if (!ratio && !closed) {
            ++out.rejected;
            continue;
          }
          const auto geometry = payload(*fields, 4);
          auto commands = DomainProto::packed(geometry);
          if (!commands) {
            ++out.rejected;
            continue;
          }
          QVector<QVector<Coordinate>> paths;
          QVector<Coordinate> path;
          qint64 x = 0, y = 0;
          qsizetype cursor = 0;
          while (cursor < commands->size() && valid) {
            const auto command = (*commands)[cursor++], kind = command & 7, count = command >> 3;
            if ((kind != 1 && kind != 2) || !count || count > 50000 ||
                count * 2 > quint64(commands->size() - cursor)) {
              valid = false;
              break;
            }
            for (quint64 i = 0; i < count; ++i) {
              if (kind == 1) {
                if (path.size() > 1)
                  paths.append(path);
                path.clear();
              }
              const auto a = (*commands)[cursor++], b = (*commands)[cursor++];
              if (a > UINT32_MAX || b > UINT32_MAX) {
                valid = false;
                break;
              }
              x += qint64(a >> 1) ^ -qint64(a & 1);
              y += qint64(b >> 1) ^ -qint64(b & 1);
              if (std::abs(x) > extent * 2 || std::abs(y) > extent * 2) {
                valid = false;
                break;
              }
              const double n = double(1 << tile.zoom);
              Coordinate p{
                  std::atan(std::sinh(std::numbers::pi * (1 - 2 * (tile.y + y / extent) / n))) *
                      180 / std::numbers::pi,
                  (tile.x + x / extent) / n * 360 - 180};
              if (!p.valid()) {
                valid = false;
                break;
              }
              path.append(p);
            }
          }
          if (path.size() > 1)
            paths.append(path);
          if (!valid || paths.isEmpty()) {
            ++out.rejected;
            continue;
          }
          const auto id =
              QString("traffic:%1:%2:%3:%4:")
                  .arg(tile.zoom)
                  .arg(tile.x)
                  .arg(tile.y)
                  .arg(int(extent)) +
              QString::fromLatin1(
                  QCryptographicHash::hash(geometry, QCryptographicHash::Sha256).toHex());
          if (identifiers.contains(id)) {
            ++out.rejected;
            continue;
          }
          identifiers.insert(id);
          DomainRecord r;
          r.id = id;
          r.feed = "traffic";
          r.label = clean(properties["road_type"]);
          if (r.label.isEmpty())
            r.label = "Road segment";
          r.coordinate = paths[0][0];
          r.paths = paths;
          r.source = "© TomTom";
          r.raw = properties;
          r.details = {{"Road closed", closed ? "Yes" : "Not reported"},
                       {"Position basis", "TomTom reported road geometry"},
                       {"Source URL", "https://www.tomtom.com/"},
                       {"Acquisition time", "Unknown; retrieval time is separate"}};
          if (ratio) {
            r.details["Relative flow"] = *ratio;
            r.details["Flow description"] =
                QString::number(*ratio * 100, 'f', 0) + "% of free-flow speed";
          }
          out.records.append(r);
        }
    }
  return out;
}
} // namespace DomainParsers
} // namespace helios
