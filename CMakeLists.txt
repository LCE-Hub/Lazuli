cmake_minimum_required(VERSION 3.24)
project(Lazuli
    VERSION 0.0.0 #neo: not versioned.
    DESCRIPTION "Lazuli UI framework"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(LAZULI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(LAZULI_FETCH_DEPS "Fetch missing dependencies with FetchContent" ON)
include(FetchContent)
function(lazuli_find_package package target)
    if(NOT TARGET ${target})
        find_package(${package} CONFIG QUIET)
        if(NOT ${package}_FOUND AND LAZULI_FETCH_DEPS)
            message(STATUS "Lazuli: fetching ${package}")
            fetchcontent_declare(${package}
                ${ARGN}
                EXCLUDE_FROM_ALL
            )
            fetchcontent_makeavailable(${package})
        endif()
        if(NOT TARGET ${target})
            message(FATAL_ERROR "Lazuli: dependency ${package} not found and LAZULI_FETCH_DEPS is off")
        endif()
    endif()
endfunction()
lazuli_find_package(SDL3 SDL3::SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.30
    GIT_SHALLOW TRUE
)
lazuli_find_package(SDL3_ttf SDL3_ttf::SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG release-3.2.2
    GIT_SHALLOW TRUE
)
lazuli_find_package(pugixml pugixml::pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.15
    GIT_SHALLOW TRUE
)
lazuli_find_package(nlohmann_json nlohmann_json::nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(LAZULI_STB_INCLUDE_DIR "" CACHE PATH "Directory containing stb_image.h")
if(LAZULI_STB_INCLUDE_DIR STREQUAL "" AND EXISTS "/usr/include/stb/stb_image.h")
    set(LAZULI_STB_INCLUDE_DIR "/usr/include/stb")
endif()
if(LAZULI_STB_INCLUDE_DIR STREQUAL "")
    if(NOT stb_SOURCE_DIR)
        message(STATUS "Lazuli: fetching stb")
        fetchcontent_declare(stb
            GIT_REPOSITORY https://github.com/nothings/stb.git
            GIT_TAG master
            GIT_SHALLOW TRUE
            EXCLUDE_FROM_ALL
        )
        fetchcontent_populate(stb)
    endif()
    set(LAZULI_STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
endif()
set(LAZULI_NANOSVG_INCLUDE_DIR "" CACHE PATH "Directory containing nanosvg.h")
if(LAZULI_NANOSVG_INCLUDE_DIR STREQUAL "" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nanosvg/nanosvg.h")
    set(LAZULI_NANOSVG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nanosvg")
endif()
if(LAZULI_NANOSVG_INCLUDE_DIR STREQUAL "")
    if(NOT nanosvg_SOURCE_DIR)
        message(STATUS "Lazuli: fetching nanosvg")
        fetchcontent_declare(nanosvg
            GIT_REPOSITORY https://github.com/memononen/nanosvg.git
            GIT_TAG master
            GIT_SHALLOW TRUE
            EXCLUDE_FROM_ALL
        )
        fetchcontent_populate(nanosvg)
    endif()
    set(LAZULI_NANOSVG_INCLUDE_DIR "${nanosvg_SOURCE_DIR}/src")
    if(NOT EXISTS "${LAZULI_NANOSVG_INCLUDE_DIR}/nanosvg.h")
        set(LAZULI_NANOSVG_INCLUDE_DIR "${nanosvg_SOURCE_DIR}")
    endif()
endif()
set(LAZULI_SOURCES
    src/Base64.cpp
    src/Binding.cpp
    src/Element.cpp
    src/Handlers.cpp
    src/I18n.cpp
    src/Log.cpp
    src/Notifications.cpp
    src/Style.cpp
    src/UiSystem.cpp
    src/Xml.cpp
    src/Render/FontCache.cpp
    src/Render/Panorama.cpp
    src/Render/SdlRenderer.cpp
    src/Render/SvgRaster.cpp
    src/Render/TextureCache.cpp
)

add_library(lazuli STATIC ${LAZULI_SOURCES})
add_library(Lazuli::lazuli ALIAS lazuli)
target_include_directories(lazuli
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(lazuli
    PUBLIC
        SDL3::SDL3
        nlohmann_json::nlohmann_json
    PRIVATE
        SDL3_ttf::SDL3_ttf
        pugixml::pugixml
)
target_include_directories(lazuli
    SYSTEM PRIVATE
        "${LAZULI_STB_INCLUDE_DIR}"
        "${LAZULI_NANOSVG_INCLUDE_DIR}"
)
target_compile_definitions(lazuli PRIVATE STBI_NO_STDIO)
if(LAZULI_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(lazuli PRIVATE /W4 /WX)
    else()
        target_compile_options(lazuli PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
install(TARGETS lazuli
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
