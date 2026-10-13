#include "GodEyeSpatial.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

extern "C" int ge_test_c_header(void);

namespace {
int checks = 0;
void require(bool ok, const char *message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char *message) {
  require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
void coordinates() {
  GEVector3 p{};
  require(ge_test_c_header(), "C header/linkage");
  require(ge_wgs84_to_ecef(0, 0, 0, &p), "equator conversion");
  near(p.x, 6378137.0, 1e-9, "equatorial radius");
  near(p.y, 0, 1e-9, "equator y");
  near(p.z, 0, 1e-9, "equator z");
  require(ge_wgs84_to_ecef(90, 123, 1800, &p), "north pole conversion");
  near(p.z, 6358552.314245, 1e-6, "polar radius and height");
  for (double lat : {-90.0, -89.0, -45.4, 0.0, 45.4, 89.0, 90.0}) {
    for (double lon : {-180.0, -75.0, 0.0, 123.0, 180.0}) {
      for (double height : {-430.0, 0.0, 1800.0, 420000.0, 35786000.0}) {
        require(ge_wgs84_to_ecef(lat, lon, height, &p), "round trip conversion");
        GEGeographicPoint result{};
        require(ge_wgs84_from_ecef(p, &result), "round trip inverse");
        near(result.latitude, lat, 1e-8, "round trip latitude");
        near(result.height, height, 0.001, "round trip height");
        if (std::abs(lat) < 90.0) near(result.longitude, lon, 1e-8, "round trip longitude");
      }
    }
  }
}
void rays() {
  GERay ray{};
  double distance = -1;
  require(ge_ray_make({6379137, 0, 0}, {-4, 0, 0}, &ray), "normalize ray");
  near(ray.direction.x, -1, 1e-15, "unit direction");
  require(ge_ray_ellipsoid_distance(ray, &distance), "surface ray hit");
  near(distance, 1000, 1e-6, "surface ray distance in metres");
  require(ge_ray_make({42164137, 0, 0}, {-1, 0, 0}, &ray), "orbital ray");
  require(ge_ray_ellipsoid_distance(ray, &distance), "orbital hit");
  near(distance, 35786000, 1e-6, "orbital hit distance");
  require(ge_ray_make({6378137.5, 0, 0}, {-1, 0, 0}, &ray), "close surface ray");
  require(ge_ray_ellipsoid_distance(ray, &distance), "close surface hit");
  near(distance, 0.5, 1e-8, "close surface numerical precision");
  require(ge_ray_make({6379137, 0, 0}, {1, 0, 0}, &ray), "outward ray");
  distance = -1;
  require(!ge_ray_ellipsoid_distance(ray, &distance) && distance == -1, "outward ray misses");
  require(ge_ray_make({0, 0, 0}, {1, 0, 0}, &ray), "inside ray");
  require(ge_ray_ellipsoid_distance(ray, &distance), "inside exit");
  near(distance, 6378137, 1e-6, "inside exit distance");
  require(ge_ray_make({0.25, 0.25, 3}, {0, 0, -10}, &ray), "triangle ray");
  require(ge_ray_triangle_distance(ray, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, &distance), "triangle hit");
  near(distance, 3, 1e-12, "triangle distance");
  require(ge_ray_triangle_distance(ray, {0, 1, 0}, {1, 0, 0}, {0, 0, 0}, &distance), "two-sided hit");
  require(!ge_ray_triangle_distance(ray, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, &distance), "degenerate triangle");
  ray.direction = {0, 0, 1};
  require(!ge_ray_triangle_distance(ray, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, &distance), "triangle behind ray");
}
void cameras() {
  for (double latitude : {-89.0, 0.0, 45.0, 90.0}) {
    for (double longitude : {-179.99, 0.0, 123.0}) {
      for (double heading : {0.0, 90.0, 123.0, 270.0}) {
        for (double tilt : {0.0, 55.0, 80.0}) {
          GECameraBasis basis{};
          require(ge_camera_basis(latitude, longitude, 800, heading, tilt, 2100, &basis), "camera basis");
          GERay ray{};
          require(ge_camera_ray(basis, 0, 0, 1.8, &ray), "center ray");
          const GEVector3 target{ray.origin.x + ray.direction.x * 800,
                                 ray.origin.y + ray.direction.y * 800,
                                 ray.origin.z + ray.direction.z * 800};
          near(target.x, basis.target.x, 1e-8, "center ray target x");
          near(target.y, basis.target.y, 1e-8, "center ray target y");
          near(target.z, basis.target.z, 1e-8, "center ray target z");
          near(basis.forward.x * basis.right.x + basis.forward.y * basis.right.y +
                   basis.forward.z * basis.right.z, 0, 1e-14, "camera orthogonality");
          require(ge_camera_ray(basis, -0.8, 0.7, 1.8, &ray), "off-center ray");
        }
      }
    }
  }
}
void invalidInputs() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  GEVector3 vector{1, 2, 3};
  GEGeographicPoint point{1, 2, 3};
  GERay ray{};
  GECameraBasis basis{};
  double distance = -1;
  require(!ge_wgs84_to_ecef(91, 0, 0, &vector) && vector.x == 1, "invalid latitude preserves output");
  require(!ge_wgs84_to_ecef(0, 181, 0, &vector), "invalid longitude");
  require(!ge_wgs84_to_ecef(nan, 0, 0, &vector), "NaN latitude");
  require(!ge_wgs84_to_ecef(0, 0, inf, &vector), "infinite height");
  require(!ge_wgs84_to_ecef(0, 0, 0, nullptr), "null coordinate output");
  require(!ge_wgs84_from_ecef({0, 0, 0}, &point) && point.latitude == 1, "undefined origin preserves output");
  require(!ge_wgs84_from_ecef({inf, 0, 0}, &point), "invalid inverse input");
  require(!ge_wgs84_from_ecef({1, 2, 3}, nullptr), "null inverse output");
  require(!ge_ray_make({nan, 0, 0}, {1, 0, 0}, &ray), "invalid ray origin");
  require(!ge_ray_make({0, 0, 0}, {0, 0, 0}, &ray), "zero direction");
  require(!ge_ray_make({0, 0, 0}, {1e-14, 0, 0}, &ray), "near-zero direction");
  require(!ge_ray_make({0, 0, 0}, {1, 0, 0}, nullptr), "null ray output");
  require(!ge_ray_ellipsoid_distance(ray, &distance) && distance == -1, "invalid hit preserves output");
  require(!ge_ray_ellipsoid_distance(ray, nullptr), "null distance output");
  require(!ge_ray_triangle_distance(ray, {nan, 0, 0}, {}, {}, &distance), "invalid triangle vertex");
  require(!ge_camera_basis(0, 0, 499, 0, 0, 0, &basis), "invalid range");
  require(!ge_camera_basis(0, 0, 500, 360, 0, 0, &basis), "invalid heading");
  require(!ge_camera_basis(0, 0, 500, 0, 81, 0, &basis), "invalid tilt");
  require(!ge_camera_basis(0, 0, 500, 0, 0, nan, &basis), "invalid target height");
  require(!ge_camera_basis(0, 0, 500, 0, 0, 0, nullptr), "null basis output");
  require(ge_camera_basis(0, 0, 500, 0, 0, 0, &basis), "valid basis");
  require(!ge_camera_ray(basis, nan, 0, 1, &ray), "invalid screen coordinate");
  require(!ge_camera_ray(basis, 0, 0, 0, &ray), "zero aspect");
  require(!ge_camera_ray(basis, 0, 0, -1, &ray), "negative aspect");
}
} // namespace

int main() {
  try {
    coordinates();
    rays();
    cameras();
    invalidInputs();
    std::cout << checks << " spatial checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Spatial check " << checks << " failed: " << error.what() << '\n';
    return 1;
  }
}
