if(NOT IS_DIRECTORY "${HELIOS_WORKER_STAGE}/workers" OR NOT HELIOS_RESOURCE_ROOT MATCHES "/Resources$")
  message(FATAL_ERROR "Worker staging needs the built worker directory and an owned Resources destination.")
endif()
# Remove only obsolete bytecode inside the owned bundle; source scripts remain
# the portable runtime contract across Python versions.
file(GLOB_RECURSE helios_previous_bytecode "${HELIOS_RESOURCE_ROOT}/workers/*.pyc")
if(helios_previous_bytecode)
  file(REMOVE ${helios_previous_bytecode})
endif()
file(COPY "${HELIOS_WORKER_STAGE}/workers" DESTINATION "${HELIOS_RESOURCE_ROOT}"
  PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
