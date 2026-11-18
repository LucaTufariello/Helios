#include "HeliosSpatial.h"

#include <cmath>
#include <numbers>

namespace {
constexpr double equatorialRadius = 6378137.0;
constexpr double polarRadius = 6356752.314245;
constexpr double eccentricitySquared = 0.0066943799901413165;
constexpr double radians = std::numbers::pi / 180.0;

bool finite(GEVector3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool coordinate(double latitude, double longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) &&
         std::abs(latitude) <= 90.0 && std::abs(longitude) <= 180.0;
}
GEVector3 add(GEVector3 a, GEVector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
GEVector3 subtract(GEVector3 a, GEVector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
GEVector3 scale(GEVector3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(GEVector3 a, GEVector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
GEVector3 cross(GEVector3 a, GEVector3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool normalized(GEVector3 input, GEVector3 &output) {
  if (!finite(input)) return false;
  const double length = std::hypot(input.x, input.y, input.z);
  if (!std::isfinite(length) || length <= 1e-12) return false;
  output = scale(input, 1.0 / length);
  return finite(output);
}
} // namespace

extern "C" int ge_wgs84_to_ecef(double latitude, double longitude, double height,
                                GEVector3 *output) {
  if (!output || !coordinate(latitude, longitude) || !std::isfinite(height)) return 0;
  const double lat = latitude * radians;
  const double lon = longitude * radians;
  const double sine = std::sin(lat);
  const double n = equatorialRadius / std::sqrt(1.0 - eccentricitySquared * sine * sine);
  const GEVector3 result{(n + height) * std::cos(lat) * std::cos(lon),
                         (n + height) * std::cos(lat) * std::sin(lon),
                         (n * (1.0 - eccentricitySquared) + height) * sine};
  if (!finite(result)) return 0;
  *output = result;
  return 1;
}

extern "C" int ge_wgs84_from_ecef(GEVector3 position, GEGeographicPoint *output) {
  if (!output || !finite(position) ||
      std::hypot(position.x, position.y, position.z) <= 1.0) return 0;
  const double horizontal = std::hypot(position.x, position.y);
  double latitude = std::atan2(position.z, horizontal * (1.0 - eccentricitySquared));
  for (int i = 0; i < 10; ++i) {
    const double sine = std::sin(latitude);
    const double n = equatorialRadius / std::sqrt(1.0 - eccentricitySquared * sine * sine);
    const double next = std::atan2(position.z + eccentricitySquared * n * sine, horizontal);
    if (std::abs(next - latitude) < 1e-13) break;
    latitude = next;
  }
  const double sine = std::sin(latitude);
  const double n = equatorialRadius / std::sqrt(1.0 - eccentricitySquared * sine * sine);
  const double height = horizontal > 1e-6
      ? horizontal / std::cos(latitude) - n
      : std::abs(position.z) - equatorialRadius * std::sqrt(1.0 - eccentricitySquared);
  const GEGeographicPoint result{latitude / radians,
                                 std::atan2(position.y, position.x) / radians, height};
  if (!coordinate(result.latitude, result.longitude) || !std::isfinite(height)) return 0;
  *output = result;
  return 1;
}

extern "C" int ge_ray_make(GEVector3 origin, GEVector3 direction, GERay *output) {
  GEVector3 unit{};
  if (!output || !finite(origin) || !normalized(direction, unit)) return 0;
  *output = {origin, unit};
  return 1;
}

extern "C" int ge_ray_ellipsoid_distance(GERay ray, double *output) {
  if (!output || !ge_ray_make(ray.origin, ray.direction, &ray)) return 0;
  const GEVector3 origin{ray.origin.x / equatorialRadius, ray.origin.y / equatorialRadius,
                         ray.origin.z / polarRadius};
  const GEVector3 direction{ray.direction.x / equatorialRadius,
                            ray.direction.y / equatorialRadius, ray.direction.z / polarRadius};
  const double a = dot(direction, direction);
  const double b = 2.0 * dot(origin, direction);
  const double c = dot(origin, origin) - 1.0;
  const double discriminant = b * b - 4.0 * a * c;
  if (!std::isfinite(discriminant) || discriminant < 0.0 || a <= 0.0) return 0;
  // Stable quadratic roots preserve close-to-surface distances at orbital scale.
  const double q = -0.5 * (b + std::copysign(std::sqrt(discriminant), b));
  const double first = q / a;
  const double second = q == 0.0 ? first : c / q;
  const double near = std::fmin(first, second);
  const double far = std::fmax(first, second);
  const double distance = near >= 0.0 ? near : far;
  if (!std::isfinite(distance) || distance < 0.0) return 0;
  *output = distance;
  return 1;
}

extern "C" int ge_ray_triangle_distance(GERay ray, GEVector3 a, GEVector3 b, GEVector3 c,
                                        double *output) {
  if (!output || !finite(a) || !finite(b) || !finite(c) ||
      !ge_ray_make(ray.origin, ray.direction, &ray)) return 0;
  const GEVector3 edge1 = subtract(b, a);
  const GEVector3 edge2 = subtract(c, a);
  const GEVector3 p = cross(ray.direction, edge2);
  const double determinant = dot(edge1, p);
  if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-10) return 0;
  const double inverse = 1.0 / determinant;
  const GEVector3 t = subtract(ray.origin, a);
  const double u = dot(t, p) * inverse;
  if (!std::isfinite(u) || u < -1e-10 || u > 1.0 + 1e-10) return 0;
  const GEVector3 q = cross(t, edge1);
  const double v = dot(ray.direction, q) * inverse;
  if (!std::isfinite(v) || v < -1e-10 || u + v > 1.0 + 1e-10) return 0;
  const double distance = dot(edge2, q) * inverse;
  if (!std::isfinite(distance) || distance < 0.0) return 0;
  *output = distance;
  return 1;
}

extern "C" int ge_camera_basis(double latitude, double longitude, double range, double heading,
                                double tilt, double target_height, GECameraBasis *output) {
  GEVector3 target{};
  if (!output || !std::isfinite(range) || range < 500.0 || range > 60000000.0 ||
      !std::isfinite(heading) || heading < 0.0 || heading >= 360.0 ||
      !std::isfinite(tilt) || tilt < 0.0 || tilt > 80.0 ||
      !ge_wgs84_to_ecef(latitude, longitude, target_height, &target)) return 0;
  const double lon = longitude * radians;
  const double lat = latitude * radians;
  const GEVector3 normal{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
  const GEVector3 east{-std::sin(lon), std::cos(lon), 0.0};
  const GEVector3 north = cross(normal, east);
  const GEVector3 horizontal = add(scale(north, std::cos(heading * radians)),
                                    scale(east, std::sin(heading * radians)));
  const GEVector3 back = subtract(scale(normal, std::cos(tilt * radians)),
                                  scale(horizontal, std::sin(tilt * radians)));
  GECameraBasis result{};
  result.target = target;
  result.eye = add(target, scale(back, range));
  result.forward = scale(back, -1.0);
  if (!finite(result.eye) || !normalized(cross(result.forward, horizontal), result.right) ||
      !normalized(cross(result.right, result.forward), result.up)) return 0;
  *output = result;
  return 1;
}

extern "C" int ge_camera_ray(GECameraBasis basis, double x, double y, double aspect, GERay *output) {
  if (!output || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(aspect) || aspect <= 0.0 ||
      !finite(basis.eye) || !finite(basis.forward) || !finite(basis.right) || !finite(basis.up)) return 0;
  const double tangent = std::tan(std::numbers::pi / 8.0);
  const GEVector3 direction = add(basis.forward,
      add(scale(basis.right, x * tangent * aspect), scale(basis.up, y * tangent)));
  return ge_ray_make(basis.eye, direction, output);
}
