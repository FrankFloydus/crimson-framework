# Copyright Francesco Di Blasi 2026 - All Rights Reserved

cmake_minimum_required(VERSION 3.20)
project(crimson-framework VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Crimson Framework targets Windows x86-64.")
endif()

set(GODOTCPP_API_VERSION "4.7")
set(GODOTCPP_PRECISION "single")
set(GODOTCPP_BUILD_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/build_profile.json")
set(GODOTCPP_DISABLE_EXCEPTIONS OFF)
set(GODOTCPP_TARGET "template_debug" CACHE STRING "Godot library target")
set_property(CACHE GODOTCPP_TARGET PROPERTY STRINGS "template_debug" "template_release")
if(NOT GODOTCPP_TARGET MATCHES "^(template_debug|template_release)$")
    message(FATAL_ERROR "Set GODOTCPP_TARGET to template_debug or template_release.")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/SConstruct")
    message(FATAL_ERROR "Initialize godot-cpp with git submodule update --init.")
endif()

add_subdirectory(godot-cpp)
get_target_property(binding_arch godot::cpp GODOTCPP_ARCH)
if(NOT binding_arch STREQUAL "x86_64")
    message(FATAL_ERROR "Select a Windows x86-64 toolchain.")
endif()
get_target_property(binding_suffix godot::cpp GODOTCPP_SUFFIX)

file(GLOB module_sources CONFIGURE_DEPENDS
    "src/modules/inventory/*.cpp"
    "src/modules/save/*.cpp"
    "src/modules/session/*.cpp"
    "src/modules/scene_load/*.cpp"
    "src/modules/match_runtime/*.cpp"
    "src/modules/replication/*.cpp"
)
set(integration_sources src/integration/register_integration.cpp)
file(GLOB module_docs CONFIGURE_DEPENDS "src/modules/inventory/doc_classes/*.xml")
foreach(area IN ITEMS game world_session adapters online online/steam online/eos)
    file(GLOB area_sources CONFIGURE_DEPENDS "src/integration/${area}/*.cpp")
    file(GLOB area_docs CONFIGURE_DEPENDS "src/integration/${area}/doc_classes/*.xml")
    list(APPEND integration_sources ${area_sources})
    list(APPEND module_docs ${area_docs})
endforeach()
add_library(crimson_framework SHARED src/register_types.cpp ${module_sources} ${integration_sources})
if(GODOTCPP_TARGET STREQUAL "template_debug" AND module_docs)
    include("${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/cmake/GodotCPPModule.cmake")
    target_doc_sources(crimson_framework ${module_docs})
endif()
target_compile_features(crimson_framework PRIVATE cxx_std_17)
target_include_directories(crimson_framework PRIVATE src src/modules/save/third_party)
target_link_libraries(crimson_framework PRIVATE godot::cpp bcrypt)
set(eos_default "${CMAKE_CURRENT_SOURCE_DIR}/vendor/eos/SDK")
if(DEFINED ENV{CRIMSON_EOS_SDK} AND NOT "$ENV{CRIMSON_EOS_SDK}" STREQUAL "")
    set(eos_default "$ENV{CRIMSON_EOS_SDK}")
endif()
set(CRIMSON_EOS_SDK "${eos_default}" CACHE STRING "EOS C SDK's SDK directory, or OFF to disable EOS")
get_filename_component(eos_sdk "${CRIMSON_EOS_SDK}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(CRIMSON_EOS_SDK AND (IS_DIRECTORY "${eos_sdk}" OR
        NOT CRIMSON_EOS_SDK STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/vendor/eos/SDK"))
    set(eos_include "${eos_sdk}/Include")
    set(eos_dll "${eos_sdk}/Bin/EOSSDK-Win64-Shipping.dll")
    if(NOT EXISTS "${eos_include}/eos_sdk.h" OR NOT EXISTS "${eos_dll}")
        message(FATAL_ERROR "Set CRIMSON_EOS_SDK to the extracted EOS C SDK's SDK directory, or to OFF.")
    endif()
    target_include_directories(crimson_framework PRIVATE "${eos_include}")
    target_compile_definitions(crimson_framework PRIVATE CRIMSON_WITH_EOS)
endif()
set(steam_full "${CMAKE_CURRENT_SOURCE_DIR}/vendor/steamworks/sdk")
set(steam_headers "${CMAKE_CURRENT_SOURCE_DIR}/vendor/steamworks/proton-1.65")
set(steam_default "${steam_full}")
if(NOT IS_DIRECTORY "${steam_full}" AND IS_DIRECTORY "${steam_headers}")
    set(steam_default "${steam_headers}")
endif()
if(DEFINED ENV{CRIMSON_STEAMWORKS_SDK} AND NOT "$ENV{CRIMSON_STEAMWORKS_SDK}" STREQUAL "")
    set(steam_default "$ENV{CRIMSON_STEAMWORKS_SDK}")
endif()
set(CRIMSON_STEAMWORKS_SDK "${steam_default}" CACHE STRING "Steamworks SDK root, or OFF to disable Steamworks")
get_filename_component(steam_sdk "${CRIMSON_STEAMWORKS_SDK}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(CRIMSON_STEAMWORKS_SDK AND (IS_DIRECTORY "${steam_sdk}" OR NOT CRIMSON_STEAMWORKS_SDK STREQUAL "${steam_full}"))
    set(steam_include "${steam_sdk}/public/steam")
    if(NOT EXISTS "${steam_include}/steam_api_flat.h")
        message(FATAL_ERROR "Set CRIMSON_STEAMWORKS_SDK to a Steamworks SDK root containing public/steam, or to OFF.")
    endif()
    target_include_directories(crimson_framework PRIVATE "${steam_sdk}/public" "${steam_include}")
    target_compile_definitions(crimson_framework PRIVATE CRIMSON_WITH_STEAM)
    set(steam_dll "${steam_sdk}/redistributable_bin/win64/steam_api64.dll")
endif()
set_target_properties(crimson_framework PROPERTIES
    PREFIX ""
    OUTPUT_NAME "crimson_framework${binding_suffix}"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/bin>"
)
set(addon_bin "${CMAKE_CURRENT_SOURCE_DIR}/project/addons/crimson-framework/bin")
add_custom_command(TARGET crimson_framework POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${addon_bin}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:crimson_framework>" "${addon_bin}/"
    VERBATIM
)
if(eos_dll)
    add_custom_command(TARGET crimson_framework POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${eos_dll}" "${addon_bin}/"
        VERBATIM
    )
endif()
if(EXISTS "${steam_dll}")
    add_custom_command(TARGET crimson_framework POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${steam_dll}" "${addon_bin}/"
        VERBATIM
    )
endif()
