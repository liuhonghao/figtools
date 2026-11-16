cmake_minimum_required(VERSION 3.24)

project(figtools
  VERSION 0.1.0
  DESCRIPTION "Image processing workbench: deterministic pipelines, operator registry, CLI"
  HOMEPAGE_URL "https://github.com/liuhonghao/figtools"
  LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FIGTOOLS_BUILD_TESTS "Build unit tests" ON)
option(FIGTOOLS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

find_package(ZLIB REQUIRED)

# ---------------------------------------------------------------------------
# ft_core -- pixels, geometry, colour, hashing, error handling
# ---------------------------------------------------------------------------
add_library(ft_core STATIC
  src/core/image.cpp
  src/core/geometry.cpp
  src/core/color.cpp
  src/core/hash.cpp
  src/core/metrics.cpp
)
target_include_directories(ft_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ft_core PRIVATE figtools::warnings)
add_library(figtools::core ALIAS ft_core)

# ---------------------------------------------------------------------------
# ft_io -- PNG / BMP / PPM codecs
# ---------------------------------------------------------------------------
add_library(ft_io STATIC
  src/io/png.cpp
  src/io/bmp.cpp
  src/io/ppm.cpp
  src/io/image_io.cpp
)
target_link_libraries(ft_io PUBLIC ft_core PRIVATE ZLIB::ZLIB figtools::warnings)
add_library(figtools::io ALIAS ft_io)

# ---------------------------------------------------------------------------
# ft_ops -- operator registry and implementations
# ---------------------------------------------------------------------------
add_library(ft_ops STATIC
  src/ops/geometry_ops.cpp
  src/ops/color_ops.cpp
  src/ops/filter_ops.cpp
  src/ops/registry.cpp
)
target_link_libraries(ft_ops PUBLIC ft_core PRIVATE figtools::warnings)
add_library(figtools::ops ALIAS ft_ops)

# ---------------------------------------------------------------------------
# ft_pipeline -- JSON, recipes, executor
# ---------------------------------------------------------------------------
add_library(ft_pipeline STATIC
  src/pipeline/json.cpp
  src/pipeline/recipe.cpp
  src/pipeline/executor.cpp
)
target_link_libraries(ft_pipeline PUBLIC ft_core ft_io ft_ops PRIVATE figtools::warnings)
add_library(figtools::pipeline ALIAS ft_pipeline)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(figtools src/cli/main.cpp src/cli/commands.cpp)
target_link_libraries(figtools PRIVATE ft_pipeline figtools::warnings)
target_compile_definitions(figtools PRIVATE FIGTOOLS_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FIGTOOLS_BUILD_TESTS)
  enable_testing()
  add_executable(ft_tests
    tests/test_main.cpp
    tests/test_core.cpp
    tests/test_ops.cpp
    tests/test_io.cpp
    tests/test_pipeline.cpp
  )
  target_link_libraries(ft_tests PRIVATE ft_pipeline figtools::warnings)
  target_include_directories(ft_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME unit COMMAND ft_tests)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS figtools RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES examples/recipes/surface_defect.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/figtools/recipes)

set(CPACK_PACKAGE_NAME "figtools")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "figtools")
include(CPack)
