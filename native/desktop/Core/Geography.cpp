#include "Geography.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace godeye {
namespace {
constexpr double pi = std::numbers::pi;
constexpr double radius = 6378137.0;
} // namespace
bool Coordinate::valid() const {
  return std::isfinite(latitude) && std::isfinite(longitude) && std::abs(latitude) <= 90 &&
         std::abs(longitude) <= 180;
}
GEVector3 Coordinate::ecef(double height) const {
  GEVector3 p{};
  if (!ge_wgs84_to_ecef(latitude, longitude, height, &p))
    return {NAN, NAN, NAN};
  return p;
}
QJsonObject Coordinate::json() const {
  return {{"latitude", latitude}, {"longitude", longitude}};
}
std::optional<Coordinate> Coordinate::read(const QJsonObject &o) {
  if (!o["latitude"].isDouble() || !o["longitude"].isDouble())
    return {};
  Coordinate p{o["latitude"].toDouble(), o["longitude"].toDouble()};
  return p.valid() ? std::optional(p) : std::nullopt;
}
bool Bounds::valid() const {
  return Coordinate{south, west}.valid() && Coordinate{north, east}.valid() && west < east &&
         south < north && south >= -85 && north <= 85 && east - west <= 180;
}
bool Bounds::validArea() const {
  return valid() && east - west <= 10 && north - south <= 10;
}
Coordinate Bounds::center() const {
  return {(south + north) / 2, (west + east) / 2};
}
QJsonObject Bounds::json() const {
  return {{"west", west}, {"south", south}, {"east", east}, {"north", north}};
}
std::optional<Bounds> Bounds::read(const QJsonValue &value) {
  Bounds b;
  if (value.isArray()) {
    const auto a = value.toArray();
    if (a.size() != 4 ||
        !std::all_of(a.begin(), a.end(), [](const auto &v) { return v.isDouble(); }))
      return {};
    b = {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
  } else if (value.isObject()) {
    const auto o = value.toObject();
    for (const auto &key : {"west", "south", "east", "north"})
      if (!o[key].isDouble())
        return {};
    b = {o["west"].toDouble(), o["south"].toDouble(), o["east"].toDouble(), o["north"].toDouble()};
  } else
    return {};
  return b.valid() ? std::optional(b) : std::nullopt;
}
QString TileID::key() const {
  return QString("%1/%2/%3").arg(zoom).arg(x).arg(y);
}
std::optional<TileID> TileID::make(int x, int y, int zoom) {
  if (zoom < 0 || zoom > 19 || y < 0 || y >= (1 << zoom))
    return {};
  const int n = 1 << zoom;
  return TileID{((x % n) + n) % n, y, zoom};
}
TileID TileID::containing(Coordinate p, int zoom) {
  zoom = std::clamp(zoom, 0, 19);
  const double n = 1 << zoom;
  const double latitude = std::clamp(p.latitude, -85.05112878, 85.05112878) * pi / 180;
  return *make(
      int(std::floor((p.longitude + 180) / 360 * n)),
      std::clamp(int(std::floor((1 - std::asinh(std::tan(latitude)) / pi) / 2 * n)), 0, int(n) - 1),
      zoom);
}
Coordinate TileID::coordinate(double u, double v) const {
  const double n = 1 << zoom;
  return {std::atan(std::sinh(pi * (1 - 2 * (y + v) / n))) * 180 / pi, (x + u) / n * 360 - 180};
}
bool Camera::valid() const {
  return center.valid() && std::isfinite(range) && range >= 500 && range <= 60000000 &&
         std::isfinite(heading) && heading >= 0 && heading < 360 && std::isfinite(tilt) &&
         tilt >= 0 && tilt <= 80;
}
GECameraBasis Camera::basis(double height) const {
  GECameraBasis b{};
  ge_camera_basis(center.latitude, center.longitude, range, heading, tilt, height, &b);
  return b;
}
void Camera::pan(double dx, double dy) {
  if (!std::isfinite(dx) || !std::isfinite(dy) || std::abs(dx) > 1e6 || std::abs(dy) > 1e6)
    return;
  const double s = std::min(0.25, range / radius / 8), angle = heading * pi / 180;
  center.latitude =
      std::clamp(center.latitude + (-dy * std::cos(angle) + dx * std::sin(angle)) * s, -85.0, 85.0);
  center.longitude =
      std::fmod(
          std::fmod(center.longitude + (-dx * std::cos(angle) - dy * std::sin(angle)) * s + 180,
                    360) +
              360,
          360) -
      180;
}
void Camera::zoom(double steps) {
  if (std::isfinite(steps))
    range = std::clamp(range * std::exp(-steps * 0.2), 500.0, 60000000.0);
}
void Camera::focus(Bounds b) {
  if (!b.valid())
    return;
  center = b.center();
  range = std::clamp(std::max((b.north - b.south) * 111320,
                              (b.east - b.west) * 111320 * std::cos(center.latitude * pi / 180)) *
                         1.7,
                     500.0, 60000000.0);
}
QJsonObject Camera::json() const {
  return {{"center", center.json()}, {"altitude", range}, {"heading", heading}, {"tilt", tilt}};
}
std::optional<Camera> Camera::read(const QJsonObject &o) {
  const auto center = Coordinate::read(o["center"].toObject());
  if (!center || !o["altitude"].isDouble() || (o.contains("heading") && !o["heading"].isDouble()) ||
      (o.contains("tilt") && !o["tilt"].isDouble()))
    return {};
  Camera c{*center, o["altitude"].toDouble(), o["heading"].toDouble(), o["tilt"].toDouble()};
  return c.valid() ? std::optional(c) : std::nullopt;
}
GEVector3 add(GEVector3 a, GEVector3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
GEVector3 subtract(GEVector3 a, GEVector3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
GEVector3 scale(GEVector3 a, double s) {
  return {a.x * s, a.y * s, a.z * s};
}
double dot(GEVector3 a, GEVector3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
std::optional<Coordinate> ellipsoidPick(const Camera &camera, double x, double y, double aspect) {
  GERay ray{};
  double distance = 0;
  GEGeographicPoint p{};
  if (!ge_camera_ray(camera.basis(), x, y, aspect, &ray) ||
      !ge_ray_ellipsoid_distance(ray, &distance) ||
      !ge_wgs84_from_ecef(add(ray.origin, scale(ray.direction, distance)), &p))
    return {};
  return Coordinate{p.latitude, p.longitude};
}
QVector<TileID> visibleTiles(const Camera &camera, double aspect) {
  QVector<Coordinate> coordinates{camera.center};
  for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x)
      if (auto p = ellipsoidPick(camera, x * 0.5, y * 0.5, aspect))
        coordinates.append(*p);
  GEGeographicPoint eye{};
  if (ge_wgs84_from_ecef(camera.basis().eye, &eye))
    coordinates.append({eye.latitude, eye.longitude});
  const int initial =
      std::clamp(int(std::floor(std::log2(
                     40075016 * std::max(0.12, std::cos(camera.center.latitude * pi / 180)) /
                     std::max(75.0, camera.range * 0.15)))),
                 1, 18);
  for (int z = initial; z >= 1; --z) {
    const auto center = TileID::containing(camera.center, z);
    const int n = 1 << z;
    int minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (auto p : coordinates) {
      auto tile = TileID::containing(p, z);
      int dx = tile.x - center.x;
      if (dx > n / 2)
        dx -= n;
      if (dx < -n / 2)
        dx += n;
      minX = std::min(minX, dx);
      maxX = std::max(maxX, dx);
      minY = std::min(minY, tile.y - center.y);
      maxY = std::max(maxY, tile.y - center.y);
    }
    if ((maxX - minX <= 4 && maxY - minY <= 4) || z == 1) {
      QVector<TileID> result;
      QSet<QString> seen;
      auto append = [&](std::optional<TileID> tile) {
        if (tile && !seen.contains(tile->key())) {
          seen.insert(tile->key());
          result.append(*tile);
        }
      };
      for (int y = minY - 1; y <= maxY + 1; ++y)
        for (int x = minX - 1; x <= maxX + 1; ++x)
          append(TileID::make(center.x + x, center.y + y, z));
      if (z < initial)
        for (auto p : QVector<Coordinate>{camera.center, {eye.latitude, eye.longitude}}) {
          auto near = TileID::containing(p, initial);
          for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
              append(TileID::make(near.x + x, near.y + y, initial));
        }
      return result;
    }
  }
  return {};
}
QVector<QRectF> tileCutouts(TileID parent, const QVector<TileID> &loaded) {
  QVector<QRectF> cuts;
  for (auto child : loaded) {
    const int levels = child.zoom - parent.zoom;
    if (levels <= 0 || levels > 19)
      continue;
    const int scale = 1 << levels;
    if (child.x / scale != parent.x || child.y / scale != parent.y)
      continue;
    const double x = double(child.x - parent.x * scale) / scale;
    const double y = double(child.y - parent.y * scale) / scale;
    const QRectF rectangle(x, y, 1.0 / scale, 1.0 / scale);
    if (!cuts.contains(rectangle))
      cuts.append(rectangle);
  }
  // Coalesce only rectangles whose union is itself a rectangle. Never fill
  // holes corresponding to children that have not actually loaded yet.
  bool merged = true;
  while (merged) {
    merged = false;
    for (qsizetype i = 0; i < cuts.size() && !merged; ++i) {
      for (qsizetype j = i + 1; j < cuts.size(); ++j) {
        const auto a = cuts[i], b = cuts[j];
        const bool horizontal = a.top() == b.top() && a.bottom() == b.bottom() &&
                                a.left() <= b.right() && b.left() <= a.right();
        const bool vertical = a.left() == b.left() && a.right() == b.right() &&
                              a.top() <= b.bottom() && b.top() <= a.bottom();
        if (a.contains(b) || b.contains(a) || horizontal || vertical) {
          cuts[i] = a.united(b);
          cuts.removeAt(j);
          merged = true;
          break;
        }
      }
    }
  }
  return cuts;
}
QMatrix4x4 viewProjection(const Camera &camera, double height, double aspect) {
  const auto b = camera.basis(height);
  const auto eye = scale(subtract(b.eye, b.target), 1 / radius);
  const auto back = scale(b.forward, -1);
  QMatrix4x4 view(float(b.right.x), float(b.right.y), float(b.right.z), float(-dot(b.right, eye)),
                  float(b.up.x), float(b.up.y), float(b.up.z), float(-dot(b.up, eye)),
                  float(back.x), float(back.y), float(back.z), float(-dot(back, eye)), 0, 0, 0, 1);
  const double y = 1 / std::tan(pi / 8), x = y / std::max(0.01, aspect);
  // Canonical OpenGL [-1,1] reverse-Z. QRhi's clip correction converts this to
  // Metal/Vulkan [0,1] and the backend's framebuffer orientation exactly once.
  QMatrix4x4 projection(float(x), 0, 0, 0, 0, float(y), 0, 0, 0, 0, 1, float(1.0 / radius), 0, 0,
                        -1, 0);
  return projection * view;
}
} // namespace godeye
