cmake_minimum_required(VERSION 3.24)

project(figtools
  VERSION 1.3.0
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
option(FIGTOOLS_BUILD_GUI "Build the Qt desktop shell" OFF)
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
  src/core/pyramid.cpp
  src/core/sample_scene.cpp
)
target_include_directories(ft_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ft_core PRIVATE figtools::warnings)
add_library(figtools::core ALIAS ft_core)

# ---------------------------------------------------------------------------
# ft_io -- PNG / BMP / PPM / JPEG / WebP / TGA codecs
# ---------------------------------------------------------------------------
add_library(ft_io STATIC
  src/io/png.cpp
  src/io/bmp.cpp
  src/io/ppm.cpp
  src/io/stb_codec.cpp
  src/io/image_io.cpp
)
target_include_directories(ft_io PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
  $<INSTALL_INTERFACE:include>
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
# ft_json -- dependency-free JSON reader/writer
# ---------------------------------------------------------------------------
add_library(ft_json STATIC
  src/json/json.cpp
)
target_include_directories(ft_json PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ft_json PRIVATE figtools::warnings)
add_library(figtools::json ALIAS ft_json)

# ---------------------------------------------------------------------------
# ft_annotate -- shapes, label schema, COCO/YOLO export
# ---------------------------------------------------------------------------
add_library(ft_annotate STATIC
  src/annotate/annotation.cpp
)
target_include_directories(ft_annotate PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ft_annotate PUBLIC ft_core ft_json PRIVATE figtools::warnings)
add_library(figtools::annotate ALIAS ft_annotate)

# ---------------------------------------------------------------------------
# ft_dataset -- scanning, deterministic splits, snapshot export
# ---------------------------------------------------------------------------
add_library(ft_dataset STATIC
  src/dataset/dataset.cpp
)
target_include_directories(ft_dataset PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ft_dataset PUBLIC ft_core ft_io ft_annotate ft_json PRIVATE figtools::warnings)
add_library(figtools::dataset ALIAS ft_dataset)

# ---------------------------------------------------------------------------
# ft_pipeline -- recipes and executor
# ---------------------------------------------------------------------------
add_library(ft_pipeline STATIC
  src/pipeline/recipe.cpp
  src/pipeline/executor.cpp
  src/pipeline/history.cpp
)
target_link_libraries(ft_pipeline PUBLIC ft_core ft_io ft_ops ft_json PRIVATE figtools::warnings)
add_library(figtools::pipeline ALIAS ft_pipeline)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(figtools src/cli/main.cpp src/cli/commands.cpp)
target_link_libraries(figtools PRIVATE ft_pipeline figtools::warnings)
target_compile_definitions(figtools PRIVATE FIGTOOLS_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# ft_batch -- run a recipe over a directory
# ---------------------------------------------------------------------------
add_library(ft_batch STATIC
  src/batch/batch.cpp
)
target_include_directories(ft_batch PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ft_batch PUBLIC ft_core ft_io ft_pipeline PRIVATE figtools::warnings)
add_library(figtools::batch ALIAS ft_batch)

# ---------------------------------------------------------------------------
# Desktop shell (optional)
# ---------------------------------------------------------------------------
if(FIGTOOLS_BUILD_GUI)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
  add_executable(figtools_gui WIN32
    gui/main.cpp
    gui/main_window.cpp
    gui/main_window.hpp
    gui/image_canvas.cpp
    gui/image_canvas.hpp
    gui/histogram_widget.cpp
    gui/histogram_widget.hpp
    gui/theme.cpp
    gui/theme.hpp
    gui/widgets.cpp
    gui/widgets.hpp
    gui/batch_dialog.cpp
    gui/batch_dialog.hpp
    gui/batch_infer_dialog.cpp
    gui/batch_infer_dialog.hpp
    gui/train_dialog.cpp
    gui/train_dialog.hpp
    gui/python_env.cpp
    gui/python_env.hpp
    gui/inference_client.cpp
    gui/inference_client.hpp
  )
  target_include_directories(figtools_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
  target_compile_definitions(figtools_gui PRIVATE FIGTOOLS_VERSION="${PROJECT_VERSION}")
  target_link_libraries(figtools_gui PRIVATE ft_pipeline ft_annotate ft_dataset ft_batch Qt6::Widgets figtools::warnings)
  set_target_properties(figtools_gui PROPERTIES AUTOMOC ON)
  # The trainer and the inference script ship next to the executable so the
  # app can start them without knowing where the source tree lives.
  add_custom_command(TARGET figtools_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:figtools_gui>/python"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/python" "$<TARGET_FILE_DIR:figtools_gui>/python"
  )
  install(TARGETS figtools_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

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
    tests/test_annotate.cpp
    tests/test_dataset.cpp
    tests/test_batch.cpp
    tests/test_history.cpp
  )
  target_link_libraries(ft_tests PRIVATE ft_pipeline ft_annotate ft_dataset ft_batch figtools::warnings)
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
