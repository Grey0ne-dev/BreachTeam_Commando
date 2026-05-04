cmake_minimum_required(VERSION 3.20)

project(breach_team_p2p_clone VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(breach_team_core
    src/game_session.cpp
    src/host_candidates.cpp
    src/protocol.cpp
    src/migration_manager.cpp
)

target_include_directories(breach_team_core PUBLIC include)

add_executable(breach_team src/main.cpp)
target_link_libraries(breach_team PRIVATE breach_team_core)

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
