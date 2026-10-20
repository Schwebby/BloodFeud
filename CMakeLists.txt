cmake_minimum_required(VERSION 3.24)
project(BloodfeudSuite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)

# Fetch CommonLibSSE-NG once for both plugins.
if(WIN32)
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

add_subdirectory(Bloodfeud)
add_subdirectory(Ledger)
