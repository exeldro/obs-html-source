# --- Detect if the plugin is build out of tree or not ---
if(CMAKE_PROJECT_NAME STREQUAL "obs-studio")
  set(BUILD_OUT_OF_TREE OFF)
else()
  set(BUILD_OUT_OF_TREE ON)
  cmake_minimum_required(VERSION 3.16...3.26)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${PROJECT_NAME} MODULE)

if(BUILD_OUT_OF_TREE)
  find_package(libobs REQUIRED)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)

find_package(Qt6 COMPONENTS Core Gui)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Core Qt6::Gui)

target_compile_options(
${PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                -Wno-comma>)
set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(BUILD_OUT_OF_TREE)
  find_package(CURL REQUIRED)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

if(OS_WINDOWS)
	configure_file(cmake/windows/resources/installer-Windows.iss.in "${CMAKE_CURRENT_BINARY_DIR}/installer-Windows.generated.iss")
endif()

target_sources(${PROJECT_NAME} PRIVATE
	html-source.c
	html-source.cpp
	html-source.h
	version.h)

if(BUILD_OUT_OF_TREE)
	set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
else()
	set_target_properties_obs(${PROJECT_NAME} PROPERTIES FOLDER "plugins/exeldro" PREFIX "")
endif()
