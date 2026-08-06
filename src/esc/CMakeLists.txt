cmake_minimum_required(VERSION 3.16)

set(ESC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# ── core library (no OpenROAD dependency) ──────────────────────────
add_library(esc_core STATIC
  src/esc_src/globals.cpp
  src/esc_src/makeInput.cpp
  src/esc_src/parse.cpp
  src/esc_src/interval.cpp
  src/esc_src/timer.cpp
  src/esc_src/escaper_lifecycle.cpp
  src/esc_src/compare.cpp
  src/esc_src/assess.cpp
  src/esc_src/find_successors.cpp
  src/esc_src/group.cpp
  src/esc_src/create_resource_table.cpp
  src/esc_src/backtrack.cpp
  src/esc_src/create_subsolutions.cpp
  src/esc_src/dfs.cpp
  src/esc_src/escaper_operator.cpp
  src/esc_src/baseline_dp.cpp
  src/esc_src/crossing_net.cpp
  src/esc_src/output.cpp
  src/esc_src/route.cpp
  src/esc_src/route_crossing.cpp
)

target_compile_features(esc_core PUBLIC cxx_std_20)
target_include_directories(esc_core PUBLIC
  "${ESC_SOURCE_DIR}/src/esc_src"
)
target_compile_options(esc_core PRIVATE
  -Wall -Wextra -Wpedantic
  -fno-fast-math -ffp-contract=off
)
target_link_libraries(esc_core PUBLIC
  Threads::Threads
  Boost::boost
)
set_target_properties(esc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── OpenROAD adapter (embedded only) ────────────────────────────────
if(TARGET odb AND DEFINED OPENROAD_HOME)
  add_library(esc_or STATIC
    src/EscOpenRoad.cpp
  )
  target_compile_features(esc_or PUBLIC cxx_std_20)
  target_include_directories(esc_or
    PUBLIC "${ESC_SOURCE_DIR}/include"
    PRIVATE
      "${OPENROAD_HOME}/include"
      ${TCL_INCLUDE_PATH}
      "${ESC_SOURCE_DIR}/src/esc_src"
  )
  target_link_libraries(esc_or
    PRIVATE
      esc_core
      odb
      utl_lib
      ${TCL_LIBRARY}
  )
endif()

if(ENABLE_TESTS)
  add_subdirectory(test)
endif()
