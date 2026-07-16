cmake_minimum_required(VERSION 3.16)

# Avoid mixing MD/MT with msvc
cmake_policy(SET CMP0091 NEW)

set(
    CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

include(FetchContent)

project(stenos
    VERSION 1.0
    DESCRIPTION "Compression library for structured binary data and video"
    HOMEPAGE_URL "https://github.com/Thermadiag/stenos"
    LANGUAGES C CXX
)
 

include(GNUInstallDirs)

# Check AVX2 support
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(AVX2)

# project options
option(STENOS_WIDE_TABLE "Enable wide table support for RLE encoding/decoding" OFF)
option(STENOS_BUILD_ZSTD "Fetch and build zstd internally" ON)
option(STENOS_BUILD_TESTS "Build tests" OFF)
option(STENOS_BUILD_BENCHS "Build benchmarks" OFF)
option(STENOS_NO_WARNINGS "Treat warnings as errors" OFF)
option(STENOS_WITH_OPENCL "Use OpenCL implementation of the video compressor" OFF)
option(STENOS_BUILD_SHARED "Build shared library" ON)
option(STENOS_BUILD_STATIC "Build static library" ON)
option(STENOS_NATIVE_ARCH "Optimize for the build machine CPU; binaries may not be portable" ON)

# tests and benchs require static build
if((STENOS_BUILD_TESTS OR STENOS_BUILD_BENCHS) AND NOT STENOS_BUILD_STATIC)
    message(FATAL_ERROR
        "STENOS_BUILD_TESTS and STENOS_BUILD_BENCHS require STENOS_BUILD_STATIC"
    )
endif()

# grab sources
file(GLOB_RECURSE StenosSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/stenos/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/stenos/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/stenos/*.hpp"
)


set(STENOS_INSTALL_LIBDIR       "${CMAKE_INSTALL_LIBDIR}")
set(STENOS_INSTALL_BINDIR       "${CMAKE_INSTALL_BINDIR}")
set(STENOS_INSTALL_DATAROOTDIR  "${CMAKE_INSTALL_DATAROOTDIR}")
set(STENOS_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/stenos")
set(STENOS_STATIC_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/stenos_static")

# Configure stenos_config.h
configure_file(stenos_config.h.in ${CMAKE_BINARY_DIR}/stenos_config.h @ONLY)


# Add zstd
if(NOT STENOS_BUILD_ZSTD)
    find_package(zstd CONFIG QUIET)
endif()

if(TARGET zstd::libzstd_static)
    set(STENOS_ZSTD_TARGET zstd::libzstd_static)
elseif(TARGET libzstd_static)
    set(STENOS_ZSTD_TARGET libzstd_static)
else()
    message(STATUS "Fetching zstd")

    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.7
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR build/cmake
    )

    FetchContent_MakeAvailable(zstd)

    set(STENOS_ZSTD_TARGET libzstd_static)
endif()


# Add opencl
if(STENOS_WITH_OPENCL)
    find_package(OpenCL QUIET)

    if(NOT TARGET OpenCL::OpenCL OR NOT TARGET OpenCL::HeadersCpp)
        set(OPENCL_SDK_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
        set(OPENCL_SDK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(OPENCL_SDK_BUILD_CLINFO OFF CACHE BOOL "" FORCE)
        set(OPENCL_SDK_BUILD_UTILITY_LIBRARIES OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            OpenCL
            GIT_REPOSITORY https://github.com/KhronosGroup/OpenCL-SDK.git
            GIT_TAG v2025.07.22
            GIT_SHALLOW TRUE
        )

        FetchContent_MakeAvailable(OpenCL)
    endif()

    if(NOT TARGET OpenCL::OpenCL)
        message(FATAL_ERROR "OpenCL::OpenCL is unavailable")
    endif()

    if(NOT TARGET OpenCL::HeadersCpp)
        message(FATAL_ERROR "OpenCL::HeadersCpp is unavailable")
    endif()
endif()




# Create target corresponding to compiled objects
add_library(stenos_obj OBJECT ${StenosSources})

target_compile_features(stenos_obj PRIVATE cxx_std_17)

# Add linker options
if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	# mingw
	target_link_options(stenos_obj PRIVATE -lKernel32 -lpsapi -lBcrypt)
endif()

if(WIN32)
	target_compile_definitions(stenos_obj PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Treat warnings as errors
if(STENOS_NO_WARNINGS)
	if(MSVC)
		target_compile_options(stenos_obj PRIVATE  /WX /W3 )
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(stenos_obj PRIVATE -Werror -Wall -Wno-c++98-compat -Wno-c++98-compat-pedantic)
	else()
		target_compile_options(stenos_obj PRIVATE -Werror -Wall)
	endif()
endif()
target_compile_definitions(stenos_obj PRIVATE STENOS_BUILD_SHARED_LIBRARY)

# Shared libraries need PIC
set_property(TARGET stenos_obj PROPERTY POSITION_INDEPENDENT_CODE 1)
set_target_properties(stenos_obj PROPERTIES INSTALL_RPATH "$ORIGIN")
target_include_directories(stenos_obj PRIVATE ${CMAKE_BINARY_DIR})

# Link 
target_link_libraries(stenos_obj PRIVATE ${STENOS_ZSTD_TARGET})

if(STENOS_WITH_OPENCL)
	target_link_libraries(stenos_obj PRIVATE OpenCL::OpenCL OpenCL::HeadersCpp)
	target_compile_definitions(stenos_obj PRIVATE STENOS_OPENCL)
endif()

# Add sse/avx flags
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
	if(AVX2_FOUND)
		message(STATUS "Compile with AVX2 support")
		target_compile_options(stenos_obj PRIVATE ${AVX2_C_FLAGS})
	endif()
else()
	#FIXME: need to be generalised for machines with incorrect binutil installed
	if(STENOS_NATIVE_ARCH)
		if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
			target_compile_options(stenos_obj PRIVATE -march=native)
		endif()
	endif()
	if (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		target_compile_options(stenos_obj PRIVATE -mno-avx512f)
	endif()
endif()

# Wide table support
if(STENOS_WIDE_TABLE)
	target_compile_options(stenos_obj PRIVATE STENOS_WIDE_TABLE)
endif()


if (NOT TARGET stenos)
if(STENOS_BUILD_SHARED)

	# Define stenos target
	add_library(stenos SHARED $<TARGET_OBJECTS:stenos_obj> )
	
	target_link_libraries(stenos PRIVATE ${STENOS_ZSTD_TARGET})
	
	if(STENOS_WITH_OPENCL)
		target_link_libraries(stenos PRIVATE OpenCL::OpenCL OpenCL::HeadersCpp)
	endif()

	# Export target
	target_include_directories(stenos  PUBLIC
						   "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
						   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
	
	install(TARGETS stenos EXPORT stenos ARCHIVE DESTINATION ${STENOS_INSTALL_LIBDIR} RUNTIME DESTINATION ${STENOS_INSTALL_BINDIR} LIBRARY DESTINATION ${STENOS_INSTALL_LIBDIR})
	install(EXPORT stenos DESTINATION ${STENOS_CMAKEDIR})
	
endif()
endif()

if (NOT TARGET stenos_static)
if(STENOS_BUILD_STATIC)

	# Define stenos_static target
	add_library(stenos_static STATIC $<TARGET_OBJECTS:stenos_obj> )
	
	# for static build we need to link dependencies as PUBLIC
	target_compile_definitions(stenos_static PUBLIC STENOS_STATIC)
	target_link_libraries(stenos_static PUBLIC ${STENOS_ZSTD_TARGET})
	
	if(STENOS_WITH_OPENCL)
		target_link_libraries(stenos_static PUBLIC OpenCL::OpenCL OpenCL::HeadersCpp)
	endif()
	
	# Export target
	target_include_directories(stenos_static  PUBLIC
						   "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
						   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
	
	
	install(TARGETS stenos_static EXPORT stenos_static DESTINATION ${STENOS_INSTALL_LIBDIR} LIBRARY)
	install(EXPORT stenos_static DESTINATION ${STENOS_STATIC_CMAKEDIR})
	
endif()
endif()


# add tests
if(STENOS_BUILD_TESTS)
	include(CTest)
    enable_testing()
	add_subdirectory(tests)
endif()

# add benchmarks
if(STENOS_BUILD_BENCHS)
	include(CTest)
    enable_testing()
	add_subdirectory(benchs)
endif()


# Install headers
file(GLOB StenosHeaders
     "stenos/*.h"
	 "stenos/*.hpp"
)
install(FILES ${StenosHeaders}
		DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/stenos"
		)

# Configure and install stenos.pc
configure_file(stenos.pc.in stenos.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stenos.pc
			DESTINATION  "${CMAKE_INSTALL_LIBDIR}/pkgconfig")


# Configure and install stenos_config.h
install(FILES ${CMAKE_BINARY_DIR}/stenos_config.h
			DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/stenos")

# Configure and install stenosConfig.cmake and stenosConfigVersion.cmake
include(CMakePackageConfigHelpers)

set(STENOS_CMAKE_FILE "stenos.cmake")
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/stenosConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/stenosConfig.cmake"
  INSTALL_DESTINATION ${STENOS_INSTALL_LIBDIR}/cmake/stenos
  PATH_VARS )
  
set(STENOS_CMAKE_FILE "stenos_static.cmake")
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/stenosConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/stenos_staticConfig.cmake"
  INSTALL_DESTINATION ${STENOS_INSTALL_LIBDIR}/cmake/stenos_static
  PATH_VARS )

  
# Generate stenosConfigVersion.cmake
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/stenosConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion )
  
# Generate stenos_staticConfigVersion.cmake
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/stenos_staticConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion )
  
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stenosConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/stenosConfigVersion.cmake
        DESTINATION ${STENOS_INSTALL_LIBDIR}/cmake/stenos )
		
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stenos_staticConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/stenos_staticConfigVersion.cmake
        DESTINATION ${STENOS_INSTALL_LIBDIR}/cmake/stenos_static )
