#include "DomainOrbit.h"
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <cmath>
#include <numbers>
namespace godeye {
namespace {
constexpr double pi = std::numbers::pi;
bool validLine(const QByteArray &line, char kind) {
  if (line.size() != 69 || line[0] != kind || line[1] != ' ')
    return false;
  int checksum = 0;
  for (int i = 0; i < 68; ++i) {
    const char c = line[i];
    if (c >= '0' && c <= '9')
      checksum += c - '0';
    else if (c == '-')
      ++checksum;
    else if (c != ' ' && c != '+' && c != '.' && !(c >= 'A' && c <= 'Z'))
      return false;
  }
  return line[68] >= '0' && line[68] <= '9' && checksum % 10 == line[68] - '0';
}
QString norad(QByteArray text) {
  text = text.trimmed();
  bool ok = false;
  uint number = text.toUInt(&ok);
  if (ok)
    return QString::number(number);
  const QByteArray alphabet("ABCDEFGHJKLMNPQRSTUVWXYZ");
  if (text.size() != 5 || !alphabet.contains(text[0]))
    return {};
  number = text.mid(1).toUInt(&ok);
  return ok ? QString::number((alphabet.indexOf(text[0]) + 10) * 10000 + number) : QString{};
}
std::optional<double> field(const QByteArray &line, int begin, int count) {
  bool ok = false;
  const double n = line.mid(begin, count).trimmed().toDouble(&ok);
  if (!ok || !std::isfinite(n))
    return {};
  return n;
}
} // namespace
std::optional<std::array<double, 3>> DomainSatellite::position(double minutes) const {
  std::array<double, 3> p{}, v{};
  if (!std::isfinite(minutes) || ge_sgp4(&elements, minutes, p.data(), v.data()) != 1)
    return {};
  for (double n : p)
    if (!std::isfinite(n))
      return {};
  return p;
}
std::optional<std::pair<Coordinate, double>> DomainSatellite::geographic(QDateTime time) const {
  if (!time.isValid())
    return {};
  auto point = position(epoch.msecsTo(time) / 60000.0);
  if (!point)
    return {};
  const auto p = *point;
  const double jd = time.toMSecsSinceEpoch() / 86400000.0 + 2440587.5;
  const double centuries = (jd - 2451545.0) / 36525.0;
  const double seconds = 67310.54841 + (876600.0 * 3600 + 8640184.812866) * centuries +
                         .093104 * centuries * centuries -
                         6.2e-6 * centuries * centuries * centuries;
  const double theta = std::fmod(seconds * pi / 43200, 2 * pi);
  const double horizontal = std::hypot(p[0], p[1]), e2 = .0066943799901413165, a = 6378.137;
  double lat = std::atan2(p[2], horizontal);
  for (int i = 0; i < 20; ++i) {
    const double n = a / std::sqrt(1 - e2 * std::pow(std::sin(lat), 2));
    lat = std::atan2(p[2] + n * e2 * std::sin(lat), horizontal);
  }
  const double n = a / std::sqrt(1 - e2 * std::pow(std::sin(lat), 2));
  const double altitude =
      horizontal > 1e-6 ? horizontal / std::cos(lat) - n : std::abs(p[2]) - a * std::sqrt(1 - e2);
  double lon = (std::atan2(p[1], p[0]) - theta) * 180 / pi;
  lon = std::fmod(std::fmod(lon + 180, 360) + 360, 360) - 180;
  Coordinate coordinate{lat * 180 / pi, lon};
  if (!coordinate.valid() || !std::isfinite(altitude) || altitude <= 0 || altitude >= 100000)
    return {};
  return std::pair(coordinate, altitude * 1000);
}
std::optional<DomainOrbit> DomainOrbit::parse(const QByteArray &bytes, QDateTime retrieved) {
  if (bytes.isEmpty() || bytes.size() > 8 * 1024 * 1024 || bytes.contains('\0'))
    return {};
  QList<QByteArray> lines;
  for (auto line : bytes.split('\n')) {
    if (line.endsWith('\r'))
      line.chop(1);
    if (!line.trimmed().isEmpty())
      lines.append(line);
  }
  DomainOrbit result;
  result.retrievedAt = retrieved;
  QSet<QString> seen;
  for (qsizetype index = 0; index < lines.size();) {
    const bool named = !lines[index].startsWith("1 ");
    const qsizetype first = index + (named ? 1 : 0);
    if (first + 1 >= lines.size()) {
      ++result.rejected;
      break;
    }
    const auto one = lines[first], two = lines[first + 1];
    index = first + 2;
    if (!validLine(one, '1') || !validLine(two, '2')) {
      ++result.rejected;
      continue;
    }
    const QString id = norad(one.mid(2, 5));
    const auto year = field(one, 18, 2), day = field(one, 20, 12), inc = field(two, 8, 8),
               node = field(two, 17, 8), perigee = field(two, 34, 8), anomaly = field(two, 43, 8),
               motion = field(two, 52, 11), exponent = field(one, 59, 2);
    bool eccentricOK = false, coefficientOK = false, elementOK = false, revolutionOK = false;
    const double ecc = ("0." + two.mid(26, 7).replace(' ', '0')).toDouble(&eccentricOK);
    const double coefficient = ("0." + one.mid(54, 5).replace(' ', '0')).toDouble(&coefficientOK);
    one.mid(64, 4).trimmed().toUInt(&elementOK);
    two.mid(63, 5).trimmed().toUInt(&revolutionOK);
    const int fullYear = year ? (*year < 57 ? 2000 : 1900) + int(*year) : 0;
    const QDate jan1(fullYear, 1, 1);
    if (id.isEmpty() || norad(two.mid(2, 5)) != id || seen.contains(id) || !year || !day || !inc ||
        !node || !perigee || !anomaly || !motion || !exponent || !eccentricOK || !coefficientOK ||
        !elementOK || !revolutionOK || !jan1.isValid() || *day < 1 ||
        *day >= jan1.daysInYear() + 1 || *inc < 0 || *inc > 180 || *node < 0 || *node >= 360 ||
        ecc < 0 || ecc >= 1 || *perigee < 0 || *perigee >= 360 || *anomaly < 0 || *anomaly >= 360 ||
        *motion <= 0 || *motion >= 20) {
      ++result.rejected;
      continue;
    }
    seen.insert(id);
    DomainSatellite satellite;
    satellite.norad = id;
    satellite.name =
        named ? DomainParsers::clean(QString::fromUtf8(lines[first - 1]), 160) : "NORAD " + id;
    if (satellite.name.isEmpty())
      satellite.name = "NORAD " + id;
    satellite.epoch =
        QDateTime(jan1, QTime(0, 0), QTimeZone::UTC).addMSecs(qint64((*day - 1) * 86400000));
    const double radians = pi / 180;
    satellite.elements = {satellite.epoch.toMSecsSinceEpoch() / 86400000.0 + 7306,
                          (one[53] == '-' ? -1 : 1) * coefficient * std::pow(10, *exponent),
                          ecc,
                          *perigee * radians,
                          *inc * radians,
                          *anomaly * radians,
                          *motion * 2 * pi / 1440,
                          *node * radians};
    result.satellites.append(satellite);
  }
  if (result.satellites.isEmpty())
    return {};
  return result;
}
DomainSnapshot DomainOrbit::snapshot(QDateTime at) const {
  DomainSnapshot result;
  result.source = "CelesTrak";
  result.retrievedAt = retrievedAt;
  result.rejected = rejected;
  result.notices = {"Positions are SGP4/SDP4 estimates propagated from TLE epochs. Accuracy "
                    "degrades with age; these are not measured positions."};
  for (const auto &s : satellites) {
    const auto p = s.geographic(at);
    if (!p) {
      ++result.rejected;
      continue;
    }
    DomainRecord r;
    r.id = "norad:" + s.norad;
    r.feed = "satellites";
    r.label = s.name;
    r.source = "CelesTrak / Vallado SGP4";
    r.coordinate = p->first;
    r.altitude = p->second;
    r.observedAt = s.epoch;
    r.details = {{"NORAD", s.norad},
                 {"Position basis", "SGP4/SDP4 propagated estimate, not a live observation"},
                 {"Propagation UTC", at.toUTC().toString(Qt::ISODateWithMs)},
                 {"TLE epoch UTC", s.epoch.toString(Qt::ISODateWithMs)},
                 {"TLE age", QString::number(s.epoch.secsTo(at) / 86400.0, 'f', 1) + " days"},
                 {"Source URL", "https://celestrak.org/satcat/records.php?CATNR=" + s.norad}};
    result.records.append(r);
  }
  return result;
}
bool DomainOrbit::orbit(const QString &id, QVector<Coordinate> &points, QVector<double> &altitudes,
                        QDateTime at) const {
  points.clear();
  altitudes.clear();
  for (const auto &s : satellites)
    if (id == "norad:" + s.norad) {
      const double period = 2 * pi / s.elements.motion * 60;
      for (int i = 0; i <= 96; ++i) {
        const auto p = s.geographic(at.addMSecs(qint64(period * i / 96 * 1000)));
        if (!p) {
          points.clear();
          altitudes.clear();
          return false;
        }
        points.append(p->first);
        altitudes.append(p->second);
      }
      return true;
    }
  return false;
}
} // namespace godeye
