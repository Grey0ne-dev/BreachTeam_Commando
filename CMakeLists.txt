cmake_minimum_required(VERSION 3.20)

project(breach_team_p2p_clone VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build")
endif()

add_library(breach_team_core
    src/game_session.cpp
    src/terminal_game.cpp
    src/host_candidates.cpp
    src/protocol.cpp
    src/migration_manager.cpp
    src/enet_transport.cpp
    src/bootstrap_client.cpp
    src/input_pipeline.cpp
)

target_include_directories(breach_team_core PUBLIC include)
target_compile_definitions(breach_team_core PRIVATE BREACH_TEAM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    pkg_check_modules(SDL2_IMAGE QUIET IMPORTED_TARGET SDL2_image)
    pkg_check_modules(SDL2_MIXER QUIET IMPORTED_TARGET SDL2_mixer)
    if(NOT SDL2_MIXER_FOUND)
        pkg_check_modules(SDL2_MIXER QUIET IMPORTED_TARGET sdl2_mixer)
    endif()
    pkg_check_modules(ENET QUIET IMPORTED_TARGET libenet)
endif()

if(SDL2_FOUND)
    target_sources(breach_team_core PRIVATE src/sdl_game.cpp)
    target_link_libraries(breach_team_core PRIVATE PkgConfig::SDL2)
    if(SDL2_IMAGE_FOUND)
        target_link_libraries(breach_team_core PRIVATE PkgConfig::SDL2_IMAGE)
        target_compile_definitions(breach_team_core PUBLIC BREACH_TEAM_HAS_SDL2_IMAGE=1)
    endif()
    if(SDL2_MIXER_FOUND)
        target_link_libraries(breach_team_core PRIVATE PkgConfig::SDL2_MIXER)
        target_compile_definitions(breach_team_core PUBLIC BREACH_TEAM_HAS_SDL2_MIXER=1)
    else()
        find_library(SDL2_MIXER_LIB NAMES SDL2_mixer)
        find_path(SDL2_MIXER_INCLUDE_DIR SDL_mixer.h PATH_SUFFIXES SDL2)
        if(SDL2_MIXER_LIB AND SDL2_MIXER_INCLUDE_DIR)
            target_include_directories(breach_team_core PRIVATE ${SDL2_MIXER_INCLUDE_DIR})
            target_link_libraries(breach_team_core PRIVATE ${SDL2_MIXER_LIB})
            target_compile_definitions(breach_team_core PUBLIC BREACH_TEAM_HAS_SDL2_MIXER=1)
        else()
            message(WARNING "SDL2_mixer was not found; SDL mode will build without weapon sounds.")
        endif()
    endif()
    target_compile_definitions(breach_team_core PUBLIC BREACH_TEAM_HAS_SDL2=1)
endif()

if(ENET_FOUND)
    target_link_libraries(breach_team_core PRIVATE PkgConfig::ENET)
    target_compile_definitions(breach_team_core PUBLIC BREACH_TEAM_HAS_ENET=1)
else()
    message(WARNING "ENet was not found; multiplayer transport will be offline.")
endif()

add_executable(breach_team src/main.cpp)
target_link_libraries(breach_team PRIVATE breach_team_core)

add_custom_command(
    TARGET breach_team POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/assets
            $<TARGET_FILE_DIR:breach_team>/assets
)

enable_testing()

add_executable(host_candidates_test tests/host_candidates_test.cpp)
target_link_libraries(host_candidates_test PRIVATE breach_team_core)
add_test(NAME host_candidates_test COMMAND host_candidates_test)

add_executable(protocol_test tests/protocol_test.cpp)
target_link_libraries(protocol_test PRIVATE breach_team_core)
add_test(NAME protocol_test COMMAND protocol_test)

add_executable(migration_manager_test tests/migration_manager_test.cpp)
target_link_libraries(migration_manager_test PRIVATE breach_team_core)
add_test(NAME migration_manager_test COMMAND migration_manager_test)

add_executable(bootstrap_client_test tests/bootstrap_client_test.cpp)
target_link_libraries(bootstrap_client_test PRIVATE breach_team_core)
add_test(NAME bootstrap_client_test COMMAND bootstrap_client_test)

add_executable(input_pipeline_test tests/input_pipeline_test.cpp)
target_link_libraries(input_pipeline_test PRIVATE breach_team_core)
add_test(NAME input_pipeline_test COMMAND input_pipeline_test)

add_executable(enet_transport_test tests/enet_transport_test.cpp)
target_link_libraries(enet_transport_test PRIVATE breach_team_core)
add_test(NAME enet_transport_test COMMAND enet_transport_test)

foreach(test_target
    host_candidates_test
    protocol_test
    migration_manager_test
    bootstrap_client_test
    input_pipeline_test
    enet_transport_test
)
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
endforeach()
