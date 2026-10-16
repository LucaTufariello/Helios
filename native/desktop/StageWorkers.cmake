if(NOT IS_DIRECTORY "${GODEYE_WORKER_STAGE}/workers" OR NOT GODEYE_RESOURCE_ROOT MATCHES "/Resources$")
  message(FATAL_ERROR "Worker staging needs the built worker directory and an owned Resources destination.")
endif()
# Remove only obsolete bytecode inside the owned bundle; source scripts remain
# the portable runtime contract across Python versions.
file(GLOB_RECURSE godeye_previous_bytecode "${GODEYE_RESOURCE_ROOT}/workers/*.pyc")
if(godeye_previous_bytecode)
  file(REMOVE ${godeye_previous_bytecode})
endif()
file(COPY "${GODEYE_WORKER_STAGE}/workers" DESTINATION "${GODEYE_RESOURCE_ROOT}"
  PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
