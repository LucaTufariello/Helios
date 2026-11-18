#ifndef HELIOS_SPATIAL_H
#define HELIOS_SPATIAL_H

// Portable double-precision geography shared by the Swift and future Qt clients.
// No Apple, Qt, CUDA, renderer or allocator dependency crosses this boundary.
// Functions return 1 on success, 0 on invalid input/no intersection. An output
// pointer must be non-null and is written only on success. Distances are metres,
// angles degrees, coordinates WGS84; rays are normalized by ge_ray_make.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GEVector3 {
  double x, y, z;
} GEVector3;

typedef struct GEGeographicPoint {
  double latitude, longitude, height;
} GEGeographicPoint;

typedef struct GERay {
  GEVector3 origin, direction;
} GERay;

typedef struct GECameraBasis {
  GEVector3 eye, target, forward, right, up;
} GECameraBasis;

int ge_wgs84_to_ecef(double latitude, double longitude, double height, GEVector3 *output);
int ge_wgs84_from_ecef(GEVector3 position, GEGeographicPoint *output);
int ge_ray_make(GEVector3 origin, GEVector3 direction, GERay *output);
int ge_ray_ellipsoid_distance(GERay ray, double *output);
int ge_ray_triangle_distance(GERay ray, GEVector3 a, GEVector3 b, GEVector3 c, double *output);

// Matches the existing camera: range 500..60,000,000 m, heading [0,360),
// tilt 0..80 degrees. Height describes the rendered target surface, not a DEM
// accuracy guarantee. The basis is shared by display and geographic picking.
int ge_camera_basis(double latitude, double longitude, double range, double heading,
                    double tilt, double target_height, GECameraBasis *output);
int ge_camera_ray(GECameraBasis basis, double x, double y, double aspect, GERay *output);

#ifdef __cplusplus
}
#endif

#endif
