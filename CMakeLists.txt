cmake_minimum_required(VERSION 3.24)

project(QuestStickScope LANGUAGES CXX)

option(QSS_BUILD_APP "Build QuestStickScope application." ON)
option(QSS_BUILD_TESTS "Build QuestStickScope tests." ON)
option(QSS_BUILD_PROBES "Build QuestStickScope probe modules." ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(qss_core
	core/correction/Correction.cpp
)

target_include_directories(qss_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(qss_platform
	platform/Clock.cpp
)

target_include_directories(qss_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
	target_compile_definitions(qss_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(QSS_BUILD_APP)
	add_executable(QuestStickScope app/main.cpp)
	target_link_libraries(QuestStickScope PRIVATE qss_core qss_platform)
endif()

if(QSS_BUILD_PROBES AND WIN32)
	add_library(QuestStickScopeSteamVRProbe SHARED probes/steamvr/ProbeModule.cpp)
	target_include_directories(QuestStickScopeSteamVRProbe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_definitions(QuestStickScopeSteamVRProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(QSS_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
