cmake_minimum_required(VERSION 3.24)

project(QuestStickScope LANGUAGES C CXX)

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

	add_library(qss_windows_ipc
		platform/windows/SteamVRSharedMemory.cpp
	)
	target_include_directories(qss_windows_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_definitions(qss_windows_ipc PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
	target_link_libraries(qss_windows_ipc PUBLIC qss_platform)
endif()

if(QSS_BUILD_APP)
	add_executable(QuestStickScope app/main.cpp)
	target_link_libraries(QuestStickScope PRIVATE qss_core qss_platform)
	if(WIN32)
		target_compile_definitions(QuestStickScope PRIVATE QSS_WINDOWS_IPC=1 WIN32_LEAN_AND_MEAN NOMINMAX)
		target_link_libraries(QuestStickScope PRIVATE qss_windows_ipc)
	endif()
endif()

if(QSS_BUILD_PROBES AND WIN32)
	include(FetchContent)

	FetchContent_Declare(
		openvr
		GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
		GIT_TAG 0924064316de3effbcd1acf1e309182a2deb1c05
		GIT_SHALLOW FALSE
		SOURCE_SUBDIR qss-no-cmake
	)
	FetchContent_MakeAvailable(openvr)

	FetchContent_Declare(
		minhook
		GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
		GIT_TAG 8af6b4acae5a9388fd742b56fa79ece89d96f823
		GIT_SHALLOW FALSE
	)
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(minhook)

	add_library(qss_openvr_api STATIC IMPORTED GLOBAL)
	set_target_properties(qss_openvr_api PROPERTIES
		IMPORTED_LOCATION "${openvr_SOURCE_DIR}/lib/win64/openvr_api.lib"
		INTERFACE_INCLUDE_DIRECTORIES "${openvr_SOURCE_DIR}/headers"
	)

	add_library(QuestStickScopeSteamVRProbe SHARED
		probes/steamvr/ProbeModule.cpp
		probes/steamvr/ServerProvider.cpp
		probes/steamvr/DriverInputHooks.cpp
	)
	target_include_directories(QuestStickScopeSteamVRProbe PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${openvr_SOURCE_DIR}/headers
	)
	target_compile_definitions(QuestStickScopeSteamVRProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
	target_link_libraries(QuestStickScopeSteamVRProbe PRIVATE
		qss_platform
		qss_windows_ipc
		qss_openvr_api
		minhook
	)

	set(QSS_STEAMVR_DRIVER_ROOT "${CMAKE_BINARY_DIR}/steamvr-driver/queststickscope")
	set_target_properties(QuestStickScopeSteamVRProbe PROPERTIES
		OUTPUT_NAME "driver_queststickscope"
		RUNTIME_OUTPUT_DIRECTORY "${QSS_STEAMVR_DRIVER_ROOT}/bin/win64"
	)
	add_custom_command(
		TARGET QuestStickScopeSteamVRProbe
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${QSS_STEAMVR_DRIVER_ROOT}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/probes/steamvr/driver/driver.vrdrivermanifest"
			"${QSS_STEAMVR_DRIVER_ROOT}/driver.vrdrivermanifest"
	)
endif()

if(QSS_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
