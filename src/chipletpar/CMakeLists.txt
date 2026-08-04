cmake_minimum_required(VERSION 3.16)

set(CHIPLETPAR_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ChipletPart VERSION 1.0 LANGUAGES C CXX)
  set(CHIPLETPAR_STANDALONE ON)
endif()

set(CHIPLETPAR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(CHIPLETPAR_BUILD_MAIN_APP_DEFAULT ${CHIPLETPAR_STANDALONE})
if(DEFINED BUILD_MAIN_APP)
  set(CHIPLETPAR_BUILD_MAIN_APP_DEFAULT ${BUILD_MAIN_APP})
endif()
option(CHIPLETPAR_BUILD_MAIN_APP "Build the standalone ChipletPart application"
       ${CHIPLETPAR_BUILD_MAIN_APP_DEFAULT})

set(CHIPLETPAR_ENABLE_OPENMP_DEFAULT ON)
if(DEFINED ENABLE_OPENMP)
  set(CHIPLETPAR_ENABLE_OPENMP_DEFAULT ${ENABLE_OPENMP})
endif()
option(CHIPLETPAR_ENABLE_OPENMP "Enable OpenMP in the standalone application"
       ${CHIPLETPAR_ENABLE_OPENMP_DEFAULT})

find_package(yaml-cpp REQUIRED)
find_package(Boost 1.71.0 CONFIG REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(OpenMP)

# Build private copies of GKlib and METIS.  Keeping their generic upstream
# targets out of OpenROAD avoids target-name and cache-option collisions.
file(GLOB CHIPLETPAR_GKLIB_SOURCES CONFIGURE_DEPENDS
  "${CHIPLETPAR_SOURCE_DIR}/GKlib/src/*.c"
)
add_library(chipletpar_gklib STATIC ${CHIPLETPAR_GKLIB_SOURCES})
target_include_directories(chipletpar_gklib
  PUBLIC "${CHIPLETPAR_SOURCE_DIR}/GKlib/include"
)
target_compile_definitions(chipletpar_gklib
  PRIVATE
    $<$<PLATFORM_ID:Linux>:LINUX>
    $<$<PLATFORM_ID:Linux>:_FILE_OFFSET_BITS=64>
    NDEBUG
    NDEBUG2
)
target_link_libraries(chipletpar_gklib
  PUBLIC $<$<NOT:$<BOOL:${WIN32}>>:m>
)
set_target_properties(chipletpar_gklib PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB CHIPLETPAR_METIS_SOURCES CONFIGURE_DEPENDS
  "${CHIPLETPAR_SOURCE_DIR}/METIS/libmetis/*.c"
)
add_library(chipletpar_metis STATIC ${CHIPLETPAR_METIS_SOURCES})
target_include_directories(chipletpar_metis
  PUBLIC "${CHIPLETPAR_SOURCE_DIR}/METIS/include"
  PRIVATE
    "${CHIPLETPAR_SOURCE_DIR}/METIS/libmetis"
    "${CHIPLETPAR_SOURCE_DIR}/GKlib/include"
)
target_compile_definitions(chipletpar_metis
  PUBLIC
    IDXTYPEWIDTH=64
    REALTYPEWIDTH=64
  PRIVATE
    $<$<PLATFORM_ID:Linux>:LINUX>
    $<$<PLATFORM_ID:Linux>:_FILE_OFFSET_BITS=64>
)
target_link_libraries(chipletpar_metis PRIVATE chipletpar_gklib)
set_target_properties(chipletpar_metis PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(chipletpar_core STATIC
  src/ChipletPar.cpp
  src/ChipletPart3DBloxReader.cpp
)
target_compile_features(chipletpar_core PUBLIC cxx_std_20)
target_include_directories(chipletpar_core
  PUBLIC
    "${CHIPLETPAR_SOURCE_DIR}/include"
    "${CHIPLETPAR_SOURCE_DIR}/src"
)
target_link_libraries(chipletpar_core
  PUBLIC chipletpar_metis yaml-cpp
)
set_target_properties(chipletpar_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The OpenROAD adapter uses the original ChipletPart search, cost model and
# refiners. Keep them in a reusable library so embedded and standalone builds
# execute the same algorithm instead of maintaining a reduced approximation.
add_library(chipletpar_pugixml STATIC src/pugixml.cpp)
target_include_directories(chipletpar_pugixml
  PUBLIC "${CHIPLETPAR_SOURCE_DIR}/src"
)
set_target_properties(chipletpar_pugixml PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(cost_model)

add_library(chipletpar_legacy STATIC
  src/Hypergraph.cpp
  src/Utilities.cpp
  src/ChipletPart.cpp
  src/FMRefiner.cpp
  src/PriorityQueue.cpp
  src/GeneticTechPartitioner.cpp
  src/floorplan.cpp
  src/KLRefiner.cpp
  src/CanonicalGA.cpp
)
target_compile_features(chipletpar_legacy PUBLIC cxx_std_20)
target_include_directories(chipletpar_legacy
  PUBLIC
    "${CHIPLETPAR_SOURCE_DIR}"
    "${CHIPLETPAR_SOURCE_DIR}/src"
    "${CHIPLETPAR_SOURCE_DIR}/include"
    "${CHIPLETPAR_SOURCE_DIR}/include/eigen3"
    "${CHIPLETPAR_SOURCE_DIR}/cost_model"
    "${CHIPLETPAR_SOURCE_DIR}/cost_model/include"
)
target_link_libraries(chipletpar_legacy
  PUBLIC
    chipletpar_core
    cost_model
    chipletpar_pugixml
    Boost::boost
    Eigen3::Eigen
)
if(CHIPLETPAR_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
  target_compile_definitions(chipletpar_legacy PUBLIC HAVE_OPENMP=1)
  target_link_libraries(chipletpar_legacy PUBLIC OpenMP::OpenMP_CXX)
else()
  target_compile_definitions(chipletpar_legacy PUBLIC HAVE_OPENMP=0)
endif()
set_target_properties(chipletpar_legacy PROPERTIES POSITION_INDEPENDENT_CODE ON)

# OpenROAD provides these targets before adding src/chipletpar.  The adapter is
# deliberately kept out of the standalone core library.
if(TARGET odb AND DEFINED OPENROAD_HOME)
  add_library(chipletpar STATIC
    src/ChipletParOpenDb.cpp
    src/ChipletParOpenRoad.cpp
  )
  target_compile_features(chipletpar PUBLIC cxx_std_20)
  target_include_directories(chipletpar
    PUBLIC "${CHIPLETPAR_SOURCE_DIR}/include"
    PRIVATE
      "${OPENROAD_HOME}/include"
      ${TCL_INCLUDE_PATH}
  )
  target_link_libraries(chipletpar
    PRIVATE
      chipletpar_core
      chipletpar_legacy
      dbSta
      odb
      utl_lib
      ${TCL_LIBRARY}
  )
endif()

if(CHIPLETPAR_BUILD_MAIN_APP)
  add_executable(chipletPart
    src/Main.cpp
  )
  target_compile_features(chipletPart PRIVATE cxx_std_20)
  target_include_directories(chipletPart PRIVATE
    "${CHIPLETPAR_SOURCE_DIR}"
    "${CHIPLETPAR_SOURCE_DIR}/src"
    "${CHIPLETPAR_SOURCE_DIR}/include"
    "${CHIPLETPAR_SOURCE_DIR}/include/eigen3"
    "${CHIPLETPAR_SOURCE_DIR}/cost_model"
    "${CHIPLETPAR_SOURCE_DIR}/cost_model/include"
  )
  target_link_libraries(chipletPart PRIVATE
    chipletpar_core
    chipletpar_legacy
    chipletpar_metis
    chipletpar_gklib
    cost_model
    chipletpar_pugixml
    yaml-cpp
    Boost::boost
    Eigen3::Eigen
  )
  if(CHIPLETPAR_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
    target_compile_definitions(chipletPart PRIVATE HAVE_OPENMP=1)
    target_link_libraries(chipletPart PRIVATE OpenMP::OpenMP_CXX)
  else()
    target_compile_definitions(chipletPart PRIVATE HAVE_OPENMP=0)
  endif()

  set_target_properties(chipletPart PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
endif()
