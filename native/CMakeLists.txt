cmake_minimum_required(VERSION 3.25)
# Use the same minimum macOS version as Hailstone, including floating-point to_chars.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS deployment version")
project(hailstone_crest_native LANGUAGES CXX)

include(CTest)
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(HAILSTONE_SDK_DIR "" CACHE PATH "Hailstone public SDK with native nameplate canvas support")
if(NOT EXISTS "${HAILSTONE_SDK_DIR}/hailstone_nameplates.h")
  message(FATAL_ERROR "Set HAILSTONE_SDK_DIR to the Hailstone SDK with native nameplates.")
endif()

function(crest_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

set(crest_lua_dir "${CMAKE_CURRENT_SOURCE_DIR}/../crest")
set(settings_schema "${CMAKE_CURRENT_BINARY_DIR}/settings_schema.inc")
add_custom_command(OUTPUT "${settings_schema}"
  COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/settings_schema.py"
          "${crest_lua_dir}/config.lua" "${settings_schema}"
  DEPENDS tools/settings_schema.py "${crest_lua_dir}/config.lua"
  VERBATIM)

add_library(crest_core STATIC src/stacking.cpp src/server_indicators.cpp src/resource_observations.cpp
  src/settings.cpp src/legacy_settings.cpp src/gameplay.cpp src/presentation.cpp
  src/presentation_geometry.cpp "${settings_schema}")
target_include_directories(crest_core PUBLIC src "${HAILSTONE_SDK_DIR}" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(crest_core PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
crest_warnings(crest_core)

add_library(crest SHARED src/plugin.cpp)
target_link_libraries(crest PRIVATE crest_core)
target_include_directories(crest PRIVATE "${HAILSTONE_SDK_DIR}")
set_target_properties(crest PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
crest_warnings(crest)
install(TARGETS crest RUNTIME DESTINATION crest LIBRARY DESTINATION crest)
foreach(pack IN ITEMS modern gamepad)
  install(FILES ui/crest.lua DESTINATION "crest/ui/${pack}")
  install(FILES "${crest_lua_dir}/config.lua" "${crest_lua_dir}/colors.lua"
    "${crest_lua_dir}/panel.lua" "${crest_lua_dir}/personal.lua" DESTINATION "crest/ui/${pack}/crest")
endforeach()

if(BUILD_TESTING)
  foreach(component IN ITEMS stacking server_indicators resource_observations settings gameplay presentation)
    add_executable(crest_${component}_test tests/${component}_test.cpp)
    target_link_libraries(crest_${component}_test PRIVATE crest_core)
    crest_warnings(crest_${component}_test)
    add_test(NAME crest_${component} COMMAND crest_${component}_test)
  endforeach()
  add_executable(crest_plugin_test tests/plugin_test.cpp src/plugin.cpp)
  target_link_libraries(crest_plugin_test PRIVATE crest_core)
  target_include_directories(crest_plugin_test PRIVATE "${HAILSTONE_SDK_DIR}")
  crest_warnings(crest_plugin_test)
  add_test(NAME crest_plugin COMMAND crest_plugin_test)
endif()
