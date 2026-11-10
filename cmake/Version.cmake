# Stamps the git revision into the build so every result can say which build made it.
# sub0squad_add_version(<target>) adds a build-time step that writes <build>/generated/sub0squad/build_info.hpp (only when its content changes,
# so an unchanged revision does not recompile) and makes <target> depend on it. The header defines SUB0SQUAD_GIT_SHA, SUB0SQUAD_GIT_DIRTY ("0"/"1")
# and SUB0SQUAD_VERSION. A tree with uncommitted changes is "dirty": results from it are not reproducible from the sha alone.
include_guard(GLOBAL)
set(_SUB0SQUAD_VERSION_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/VersionStamp.cmake")

function(sub0squad_add_version target)
  set(dir "${CMAKE_BINARY_DIR}/generated/sub0squad")
  file(MAKE_DIRECTORY "${dir}")
  add_custom_target(${target}_version
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -DOUT=${dir}/build_info.hpp -DVERSION=${PROJECT_VERSION}
            -P ${_SUB0SQUAD_VERSION_SCRIPT}
    BYPRODUCTS ${dir}/build_info.hpp
    COMMENT "Stamping the git revision"
    VERBATIM)
  add_dependencies(${target} ${target}_version)
  target_include_directories(${target} PUBLIC "${CMAKE_BINARY_DIR}/generated")
endfunction()
