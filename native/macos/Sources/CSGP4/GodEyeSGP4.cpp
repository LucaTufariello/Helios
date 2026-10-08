#include "GodEyeSGP4.h"
#include "SGP4.h"
#include <cmath>
int ge_sgp4(const GEElements *e, double minutes, double r[3], double v[3]) {
    if (!e || !std::isfinite(minutes) || !std::isfinite(e->epoch) ||
        !std::isfinite(e->bstar) || !(e->eccentricity >= 0 && e->eccentricity < 1) ||
        !(e->motion > 0 && e->motion < 1) || !std::isfinite(e->perigee) ||
        !std::isfinite(e->inclination) || !std::isfinite(e->anomaly) || !std::isfinite(e->node)) return 0;
    elsetrec record{};
    if (!SGP4Funcs::sgp4init(wgs72, 'i', "00000", e->epoch, e->bstar, 0, 0,
        e->eccentricity, e->perigee, e->inclination, e->anomaly, e->motion, e->node, record)) return 0;
    if (!SGP4Funcs::sgp4(record, minutes, r, v) || record.error) return 0;
    for (int i = 0; i < 3; ++i) if (!std::isfinite(r[i]) || !std::isfinite(v[i])) return 0;
    return 1;
}
