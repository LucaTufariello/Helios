#include "GodEyeSpatial.h"

// Compile and link the public header as C, not only as C++. Swift uses this ABI.
int ge_test_c_header(void) {
  GEVector3 output = {0, 0, 0};
  return ge_wgs84_to_ecef(0, 0, 0, &output) && output.x == 6378137.0;
}
