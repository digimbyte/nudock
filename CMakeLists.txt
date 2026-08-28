cmake_minimum_required(VERSION 3.28...3.31)

project(nudock VERSION 2.0.0 LANGUAGES CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

add_library(nudock MODULE)

target_sources(
  nudock
  PRIVATE
    src/dock_profile_manager_dialog.cpp
    src/dock_profile_manager_dialog.hpp
    src/dock_profile_store.cpp
    src/dock_profile_store.hpp
    src/dock_state_restorer.cpp
    src/dock_state_restorer.hpp
    src/nudock.cpp
    src/nudock.hpp
    src/pluginInfo.hpp
)

target_link_libraries(
  nudock
  PRIVATE
    OBS::libobs
    OBS::frontend-api
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_compile_features(nudock PRIVATE cxx_std_20)
target_compile_definitions(nudock PRIVATE UNICODE _UNICODE NOMINMAX)

if(OS_WINDOWS)
  set(UI_VERSION "${PROJECT_VERSION}")
  set(UI_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
  set(UI_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
  set(UI_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
  set(MODULE_DESCRIPTION "NuDock dock profile manager")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/bundle/windows/obs-module.rc.in" nudock.rc)
  target_sources(nudock PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/nudock.rc")
endif()

set_target_properties_obs(
  nudock
  PROPERTIES
    FOLDER plugins/nudock
    PREFIX ""
)

install(
  TARGETS nudock
  RUNTIME DESTINATION "obs-plugins/64bit"
  LIBRARY DESTINATION "obs-plugins/64bit"
)

install(DIRECTORY data/ DESTINATION "data/obs-plugins/nudock")

option(NUDOCK_BUILD_TESTS "Build NuDock unit tests" OFF)
if(NUDOCK_BUILD_TESTS)
  enable_testing()

  add_executable(
    nudock_tests
    tests/dock_profile_store_test.cpp
    src/dock_profile_store.cpp
    src/dock_profile_store.hpp
    src/dock_state_restorer.cpp
    src/dock_state_restorer.hpp
  )
  target_include_directories(nudock_tests PRIVATE src)
  target_link_libraries(
    nudock_tests
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
  )
  target_compile_features(nudock_tests PRIVATE cxx_std_20)
  target_compile_definitions(nudock_tests PRIVATE UNICODE _UNICODE NOMINMAX)
  add_test(NAME nudock_tests COMMAND nudock_tests)
  set_tests_properties(nudock_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=minimal")
endif()
