#pragma once
#include "GodEyeSpatial.h"
#include <QJsonObject>
#include <QMatrix4x4>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <optional>

namespace godeye {
struct Coordinate {
  double latitude = 0, longitude = 0;
  bool valid() const;
  GEVector3 ecef(double height = 0) const;
  QJsonObject json() const;
  static std::optional<Coordinate> read(const QJsonObject &object);
};
struct Bounds {
  double west = 0, south = 0, east = 0, north = 0;
  bool valid() const;
  bool validArea() const;
  Coordinate center() const;
  QJsonObject json() const;
  static std::optional<Bounds> read(const QJsonValue &value);
};
struct TileID {
  int x = 0, y = 0, zoom = 0;
  auto operator<=>(const TileID &) const = default;
  QString key() const;
  Coordinate coordinate(double u, double v) const;
  static TileID containing(Coordinate coordinate, int zoom);
  static std::optional<TileID> make(int x, int y, int zoom);
};
struct Camera {
  Coordinate center{45.4641, 9.1919};
  double range = 1500000, heading = 0, tilt = 0;
  bool valid() const;
  GECameraBasis basis(double targetHeight = 0) const;
  void pan(double dx, double dy);
  void zoom(double steps);
  void focus(Bounds bounds);
  QJsonObject json() const;
  static std::optional<Camera> read(const QJsonObject &object);
};
QVector<TileID> visibleTiles(const Camera &camera, double aspect);
QVector<QRectF> tileCutouts(TileID parent, const QVector<TileID> &loaded);
GEVector3 add(GEVector3 a, GEVector3 b);
GEVector3 subtract(GEVector3 a, GEVector3 b);
GEVector3 scale(GEVector3 a, double s);
double dot(GEVector3 a, GEVector3 b);
QMatrix4x4 viewProjection(const Camera &camera, double height, double aspect);
std::optional<Coordinate> ellipsoidPick(const Camera &camera, double x, double y, double aspect);
} // namespace godeye
