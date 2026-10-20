cmake_minimum_required(VERSION 3.24)
# Builds on its own, or as part of the suite (which fetches CommonLibSSE-NG once).
project(Bloodfeud VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Core: the whole feud/faction simulation. No game dependencies, builds and
# tests on any platform.
# ---------------------------------------------------------------------------
add_library(bloodfeud_core STATIC
	core/src/Text.cpp
	core/src/World.cpp
)
target_include_directories(bloodfeud_core PUBLIC core/include)
target_compile_features(bloodfeud_core PUBLIC cxx_std_20)

include(CTest)
if(BUILD_TESTING)
	add_executable(core_tests tests/core_tests.cpp)
	target_link_libraries(core_tests PRIVATE bloodfeud_core)
	add_test(NAME core_tests COMMAND core_tests)

	add_executable(settings_tests tests/settings_tests.cpp plugin/src/Settings.cpp)
	target_include_directories(settings_tests PRIVATE plugin/src)
	target_link_libraries(settings_tests PRIVATE bloodfeud_core)
	add_test(NAME settings_tests COMMAND settings_tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# ---------------------------------------------------------------------------
# The SKSE plugin (Windows only).
# ---------------------------------------------------------------------------
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

	add_commonlibsse_plugin(Bloodfeud
		NAME Bloodfeud
		AUTHOR "Bloodfeud contributors"
		USE_ADDRESS_LIBRARY
		SOURCES
			plugin/src/main.cpp
			plugin/src/Game.cpp
			plugin/src/Events.cpp
			plugin/src/UI.cpp
			plugin/src/Settings.cpp
	)
	target_include_directories(Bloodfeud PRIVATE plugin/src api)
	target_link_libraries(Bloodfeud PRIVATE bloodfeud_core)
	target_precompile_headers(Bloodfeud PRIVATE plugin/src/PCH.h)
	target_compile_options(Bloodfeud PRIVATE /utf-8 /W4 /wd4100 /wd4505 /wd4189)

	# Mod-manager-ready folder: dist/Bloodfeud/SKSE/Plugins/{Bloodfeud.dll, Bloodfeud.ini}
	set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist/Bloodfeud/SKSE/Plugins")
	add_custom_command(TARGET Bloodfeud POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Bloodfeud>" "${DIST_DIR}/"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/dist/SKSE/Plugins/Bloodfeud.ini" "${DIST_DIR}/"
		COMMENT "Packaging Bloodfeud into dist/Bloodfeud"
	)
endif()
