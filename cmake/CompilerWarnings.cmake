# Compiler warning presets shared by every target in the project.
#
# Usage:
#   target_link_libraries(my_target PRIVATE figtools::warnings)

add_library(figtools_warnings INTERFACE)
add_library(figtools::warnings ALIAS figtools_warnings)

if(MSVC)
  target_compile_options(figtools_warnings INTERFACE
    /W4
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /utf-8
    /EHsc
  )
  if(FIGTOOLS_WARNINGS_AS_ERRORS)
    target_compile_options(figtools_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(figtools_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
  )
  if(FIGTOOLS_WARNINGS_AS_ERRORS)
    target_compile_options(figtools_warnings INTERFACE -Werror)
  endif()
endif()
