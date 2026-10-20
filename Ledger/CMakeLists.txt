cmake_minimum_required(VERSION 3.24)
# Builds on its own, or as part of the suite (which fetches CommonLibSSE-NG once).
project(Ledger VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core: the economy simulation. No game dependencies.
add_library(ledger_core STATIC core/src/Economy.cpp)
target_include_directories(ledger_core PUBLIC core/include)
target_compile_features(ledger_core PUBLIC cxx_std_20)

include(CTest)
if(BUILD_TESTING)
	add_executable(economy_tests tests/economy_tests.cpp)
	target_link_libraries(economy_tests PRIVATE ledger_core)
	add_test(NAME economy_tests COMMAND economy_tests)

	add_executable(ledger_settings_tests tests/settings_tests.cpp plugin/src/Settings.cpp)
	target_include_directories(ledger_settings_tests PRIVATE plugin/src)
	target_link_libraries(ledger_settings_tests PRIVATE ledger_core)
	add_test(NAME ledger_settings_tests COMMAND ledger_settings_tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# The SKSE plugin (Windows only).
if(WIN32)
	if(NOT COMMAND add_commonlibsse_plugin)
		include(FetchContent)
		set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
		set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)  # SE + AE
		FetchContent_Declare(
			commonlibsse
			GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
			GIT_TAG b93280e832f263dbef44e44cbe2936622a02f91a
		)
		FetchContent_MakeAvailable(commonlibsse)
		include(${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake)
	endif()

	add_commonlibsse_plugin(Ledger
		NAME Ledger
		AUTHOR "Bloodfeud contributors"
		USE_ADDRESS_LIBRARY
		SOURCES
			plugin/src/main.cpp
			plugin/src/Game.cpp
			plugin/src/Events.cpp
			plugin/src/UI.cpp
			plugin/src/Settings.cpp
	)
	target_include_directories(Ledger PRIVATE plugin/src api)
	target_link_libraries(Ledger PRIVATE ledger_core)
	target_precompile_headers(Ledger PRIVATE plugin/src/PCH.h)
	target_compile_options(Ledger PRIVATE /utf-8 /W4 /wd4100 /wd4505 /wd4189)

	set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist/Ledger/SKSE/Plugins")
	add_custom_command(TARGET Ledger POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Ledger>" "${DIST_DIR}/"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/dist/SKSE/Plugins/Ledger.ini" "${DIST_DIR}/"
		COMMENT "Packaging Ledger into dist/Ledger"
	)
endif()
