cmake_minimum_required(VERSION 3.28...3.31)

project(jrdockie VERSION 1.6.2 LANGUAGES CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

add_library(jrdockie MODULE)

target_sources(
  jrdockie
  PRIVATE
    src/jrdockie.cpp
    src/jrdockie.hpp
    src/pluginInfo.hpp
)

target_include_directories(
  jrdockie
  PRIVATE
    "${CMAKE_SOURCE_DIR}/plugins/obs-websocket/lib"
)

target_link_libraries(
  jrdockie
  PRIVATE
    OBS::libobs
    OBS::frontend-api
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_compile_features(jrdockie PRIVATE cxx_std_20)
target_compile_definitions(jrdockie PRIVATE UNICODE _UNICODE NOMINMAX)

if(OS_WINDOWS)
  set(UI_VERSION "${PROJECT_VERSION}")
  set(UI_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
  set(UI_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
  set(UI_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
  set(MODULE_DESCRIPTION "JrDockie dock layout manager")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/bundle/windows/obs-module.rc.in" jrdockie.rc)
  target_sources(jrdockie PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/jrdockie.rc")
endif()

set_target_properties_obs(
  jrdockie
  PROPERTIES
    FOLDER plugins/jr
    PREFIX ""
)

install(
  TARGETS jrdockie
  RUNTIME DESTINATION "obs-plugins/64bit"
  LIBRARY DESTINATION "obs-plugins/64bit"
)

install(DIRECTORY data/ DESTINATION "data/obs-plugins/jrdockie")
