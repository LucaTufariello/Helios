#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct GEScene GEScene;
typedef struct {
  double x, y, z;
} GEPoint;
typedef struct {
  float x, y, z, u, v;
} GEVertex;
typedef struct {
  uint64_t id, textureID;
  GEPoint origin;
  const GEVertex *vertices;
  uint32_t vertexCount;
  const uint32_t *indices;
  uint32_t indexCount;
  const uint8_t *pixels;
  uint32_t width, height;
  float red, green, blue, alpha, alphaCutoff;
  uint8_t doubleSided, blend;
  int32_t wrapS, wrapT, magFilter, minFilter;
} GEMesh;
GEScene *GE_scene_create(const char *rootURL);
void GE_scene_destroy(GEScene *scene);
uint32_t GE_scene_update(GEScene *scene, GEPoint eye, GEPoint forward, GEPoint up, double width,
                         double height);
int GE_scene_mesh(GEScene *scene, uint32_t index, GEMesh *mesh);
void GE_scene_rendered_meshes(GEScene *scene, const uint64_t *ids, uint32_t count);
int GE_scene_pick(GEScene *scene, GEPoint origin, GEPoint direction, GEPoint *hit);
float GE_scene_progress(GEScene *scene);
uint32_t GE_scene_failures(GEScene *scene);
const char *GE_scene_credits(GEScene *scene);
int GE_scene_center(GEScene *scene, GEPoint *center, double *radius);
#ifdef __cplusplus
}
#endif
