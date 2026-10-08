#ifndef GODEYE_SGP4_H
#define GODEYE_SGP4_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    double epoch, bstar, eccentricity, perigee, inclination, anomaly, motion, node;
} GEElements;
int ge_sgp4(const GEElements *elements, double minutes, double position[3], double velocity[3]);
#ifdef __cplusplus
}
#endif
#endif
