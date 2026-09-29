include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
	googletest
	GIT_REPOSITORY https://github.com/google/googletest.git
	GIT_TAG v1.17.0
	GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(googletest)

add_executable(qss_core_tests
	CalibrationTests.cpp
	CorrectionStoreTests.cpp
	CorrectionTests.cpp
	LiveStateTests.cpp
	RecordingTests.cpp
	ReplayTests.cpp
	RingBufferTests.cpp
	StickStatisticsTests.cpp
)

target_link_libraries(qss_core_tests PRIVATE qss_core GTest::gtest_main)
target_include_directories(qss_core_tests PRIVATE ${CMAKE_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(qss_core_tests)
