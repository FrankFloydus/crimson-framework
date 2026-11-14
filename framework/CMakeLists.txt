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
option(CRIMSON_STEAM_PUBLISHER "Build trusted Steam publisher services" OFF)
option(CRIMSON_STEAM_PUBLISHER_TESTS "Expose the isolated publisher fixture transport" OFF)
if(CRIMSON_STEAM_PUBLISHER_TESTS AND NOT CRIMSON_STEAM_PUBLISHER)
    message(FATAL_ERROR "Enable CRIMSON_STEAM_PUBLISHER to build publisher fixture support.")
endif()
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
    "src/modules/scene_manager/*.cpp"
    "src/modules/match_runtime/*.cpp"
    "src/modules/network/*.cpp"
    "src/modules/input/*.cpp"
    "src/modules/input/rebinding/*.cpp"
    "src/modules/mover/*.cpp"
    "src/modules/steam/*.cpp"
    "src/modules/steam/client/*.cpp"
    "src/modules/steam/client/workshop/*.cpp"
    "src/modules/steam/commerce/*.cpp"
    "src/modules/steam/lobbies/*.cpp"
    "src/modules/steam/runtime/*.cpp"
    "src/modules/steam/server/*.cpp"
)
set(integration_sources src/integration/register_integration.cpp)
file(GLOB module_docs CONFIGURE_DEPENDS
    "src/modules/inventory/doc_classes/*.xml"
    "src/modules/save/doc_classes/*.xml"
    "src/modules/session/doc_classes/*.xml"
    "src/modules/steam/doc_classes/*.xml"
    "src/modules/input/doc_classes/*.xml"
    "src/modules/input/rebinding/doc_classes/*.xml"
    "src/modules/mover/doc_classes/*.xml"
    "src/modules/network/doc_classes/*.xml"
)
foreach(area IN ITEMS game world_session adapters online online/steam player_control)
    file(GLOB area_sources CONFIGURE_DEPENDS "src/integration/${area}/*.cpp")
    file(GLOB area_docs CONFIGURE_DEPENDS "src/integration/${area}/doc_classes/*.xml")
    list(APPEND integration_sources ${area_sources})
    list(APPEND module_docs ${area_docs})
endforeach()
add_library(crimson_framework SHARED src/register_types.cpp ${module_sources} ${integration_sources})
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(crimson_check_native_text
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_native_text.py"
    VERBATIM
)
add_dependencies(crimson_framework crimson_check_native_text)
if(CRIMSON_STEAM_PUBLISHER)
    file(GLOB publisher_sources CONFIGURE_DEPENDS "src/modules/steam/publisher/*.cpp")
    file(GLOB publisher_docs CONFIGURE_DEPENDS "src/modules/steam/publisher/doc_classes/*.xml")
    target_sources(crimson_framework PRIVATE ${publisher_sources})
    target_compile_definitions(crimson_framework PRIVATE CRIMSON_WITH_STEAM_PUBLISHER)
    list(APPEND module_docs ${publisher_docs})
endif()
if(CRIMSON_STEAM_PUBLISHER_TESTS)
    target_compile_definitions(crimson_framework PRIVATE CRIMSON_WITH_STEAM_PUBLISHER_TESTS)
endif()
if(GODOTCPP_TARGET STREQUAL "template_debug" AND module_docs)
    set(doc_source "${CMAKE_CURRENT_BINARY_DIR}/gen/doc_source.cpp")
    set(doc_manifest "${CMAKE_CURRENT_BINARY_DIR}/gen/doc_manifest.txt")
    set(doc_generator "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/doc_source_generator.py")
    set(doc_driver "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_extension_docs.py")
    list(JOIN module_docs "\n" doc_manifest_content)
    file(GENERATE OUTPUT "${doc_manifest}" CONTENT "${doc_manifest_content}\n")
    # Use a manifest to keep the Windows custom command below its argument limit.
    add_custom_command(OUTPUT "${doc_source}"
        COMMAND "${Python3_EXECUTABLE}" "${doc_driver}" "${doc_generator}" "${doc_source}" "${doc_manifest}"
        DEPENDS "${doc_driver}" "${doc_generator}" "${doc_manifest}" ${module_docs}
        VERBATIM
    )
    target_sources(crimson_framework PRIVATE "${doc_source}")
    set(mover_help_source "${CMAKE_CURRENT_BINARY_DIR}/gen/mover_property_help.gen.cpp")
    set(mover_help_generator "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_mover_property_help.py")
    set(mover_help_xml "${CMAKE_CURRENT_SOURCE_DIR}/src/modules/mover/doc_classes/MoverComponent.xml")
    add_custom_command(OUTPUT "${mover_help_source}"
        COMMAND "${Python3_EXECUTABLE}" "${mover_help_generator}" "${mover_help_source}" "${mover_help_xml}"
        DEPENDS "${mover_help_generator}" "${mover_help_xml}"
        VERBATIM
    )
    target_sources(crimson_framework PRIVATE "${mover_help_source}")
endif()
target_compile_features(crimson_framework PRIVATE cxx_std_17)
target_include_directories(crimson_framework PRIVATE src src/modules/save/third_party)
target_link_libraries(crimson_framework PRIVATE godot::cpp bcrypt)
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
set(sdr_default "${CMAKE_CURRENT_SOURCE_DIR}/vendor/steamworks/sdr")
if(DEFINED ENV{CRIMSON_STEAM_SDR_SDK} AND NOT "$ENV{CRIMSON_STEAM_SDR_SDK}" STREQUAL "")
    set(sdr_default "$ENV{CRIMSON_STEAM_SDR_SDK}")
endif()
set(CRIMSON_STEAM_SDR_SDK "${sdr_default}" CACHE STRING "Steam Game Coordinator SDK root, or OFF")
get_filename_component(steam_sdk "${CRIMSON_STEAMWORKS_SDK}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(CRIMSON_STEAMWORKS_SDK AND (IS_DIRECTORY "${steam_sdk}" OR NOT CRIMSON_STEAMWORKS_SDK STREQUAL "${steam_full}"))
    set(steam_include "${steam_sdk}/public/steam")
    if(NOT EXISTS "${steam_include}/steam_api_flat.h")
        message(FATAL_ERROR "Set CRIMSON_STEAMWORKS_SDK to a Steamworks SDK root containing public/steam, or to OFF.")
    endif()
    target_include_directories(crimson_framework PRIVATE "${steam_sdk}/public" "${steam_include}")
    target_compile_definitions(crimson_framework PRIVATE CRIMSON_WITH_STEAM)
    set(steam_dll "${steam_sdk}/redistributable_bin/win64/steam_api64.dll")
    set(encrypted_ticket_dll "${steam_include}/lib/win64/sdkencryptedappticket64.dll")
    if(CRIMSON_STEAM_SDR_SDK)
        get_filename_component(sdr_sdk "${CRIMSON_STEAM_SDR_SDK}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(IS_DIRECTORY "${sdr_sdk}" OR NOT CRIMSON_STEAM_SDR_SDK STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/vendor/steamworks/sdr")
            set(sdr_include "${sdr_sdk}/include")
            if(NOT EXISTS "${sdr_include}/steam/steamdatagram_tickets.h")
                message(FATAL_ERROR "Set CRIMSON_STEAM_SDR_SDK to the Steam Game Coordinator SDK root, or to OFF.")
            endif()
            target_include_directories(crimson_framework PRIVATE "${sdr_include}" "${sdr_include}/steam")
            target_compile_definitions(crimson_framework PRIVATE CRIMSON_WITH_STEAM_SDR)
        endif()
    endif()
endif()
set_target_properties(crimson_framework PROPERTIES
    PREFIX ""
    OUTPUT_NAME "crimson_framework${binding_suffix}"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/bin>"
)
if(CRIMSON_STEAM_PUBLISHER)
    set(addon_bin "$<TARGET_FILE_DIR:crimson_framework>")
else()
    set(addon_bin "${CMAKE_CURRENT_SOURCE_DIR}/project/addons/crimson-framework/bin")
    add_custom_command(TARGET crimson_framework POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${addon_bin}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:crimson_framework>" "${addon_bin}/"
        VERBATIM
    )
endif()
if(EXISTS "${steam_dll}")
    add_custom_command(TARGET crimson_framework POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${steam_dll}" "${addon_bin}/"
        VERBATIM
    )
endif()
if(CRIMSON_STEAM_PUBLISHER AND EXISTS "${encrypted_ticket_dll}")
    add_custom_command(TARGET crimson_framework POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${encrypted_ticket_dll}" "${addon_bin}/"
        VERBATIM
    )
endif()
