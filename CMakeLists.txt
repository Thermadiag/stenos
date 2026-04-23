cmake_minimum_required(VERSION 3.16)
include(FetchContent)

project(stenos
    VERSION 1.0
    DESCRIPTION "Compression library for structured binary data and video"
    HOMEPAGE_URL "https://github.com/Thermadiag/stenos"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

# Check AVX2 support
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(AVX2)

# project options
option(STENOS_WIDE_TABLE "Enable wide table support for RLE encoding/decoding" OFF)
option(STENOS_BUILD_ZSTD "Fetch and build zstd internally" OFF)
option(STENOS_BUILD_TESTS "Build tests" OFF)
option(STENOS_BUILD_BENCHS "Build benchmarks" OFF)
option(STENOS_NO_WARNINGS "Treat warnings as errors" OFF)
option(STENOS_WITH_OPENCL "Use OpenCL implementation of the video compressor" OFF)
option(STENOS_BUILD_SHARED "Build shared library" ON)
option(STENOS_BUILD_STATIC "Build static library" ON)

if(STENOS_BUILD_BENCHS OR STENOS_BUILD_BENCHS)
	# benchmarks and tests require stenos static
	set(STENOS_BUILD_STATIC ON)
endif()

# create source file list
file(GLOB StenosSources
     "stenos/*.h"
	 "stenos/*.hpp"
	 "stenos/internal/*.h"
	 "stenos/internal/*.hpp"
     "stenos/internal/*.cpp" 
	 "stenos/internal/video/*.h"
     "stenos/internal/video/*.cpp" 
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
	message(STATUS "Try to find package ZSTD")
endif()
if((NOT zstd_FOUND) OR (NOT ZSTD_LIB_DIR) OR (NOT ${ZSTD_LIB_DIR}))
	message(STATUS "Building ZSTD from sources")
	cmake_policy(SET CMP0114 NEW)
	cmake_policy(SET CMP0135 NEW)
	if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30.0")
		cmake_policy(SET CMP0169 OLD)
	endif()

	set(ZSTD_INSTALL_DIR ${CMAKE_BINARY_DIR}/3rd-party-install)
	set(ZSTD_MINIMUM_VERSION 1.5.5)

	# we are statically building from ZSTD
	set(ZSTD_BUILD_SHARED OFF CACHE INTERNAL "" FORCE)
	set(ZSTD_LEGACY_SUPPORT OFF CACHE INTERNAL "" FORCE)
	set(ZSTD_BUILD_PROGRAMS OFF CACHE INTERNAL "" FORCE)
	set(ZSTD_BUILD_CONTRIB OFF CACHE INTERNAL "" FORCE)
	set(ZSTD_BUILD_TESTS OFF CACHE INTERNAL "" FORCE)
	set(ZSTD_MULTITHREAD_SUPPORT ON CACHE INTERNAL "" FORCE)
	set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE INTERNAL "" FORCE)
	FetchContent_Declare(
		zstd
		GIT_REPOSITORY "https://github.com/facebook/zstd.git"
		GIT_TAG release#v${ZSTD_MINIMUM_VERSION}
		OVERRIDE_FIND_PACKAGE
		SOURCE_SUBDIR build/cmake
	)
	FetchContent_GetProperties(zstd)

	if(NOT zstd_POPULATED)
		FetchContent_Populate(zstd)
		message(STATUS "zstd_SOURCE_DIR: ${zstd_SOURCE_DIR}")
		add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR} EXCLUDE_FROM_ALL)
	endif()
	set(ZSTD_INCLUDE_DIR ${zstd_SOURCE_DIR}/lib CACHE INTERNAL "")
else()
message(STATUS "Using system ZSTD :${ZSTD_LIB_DIR} and ${ZSTD_INCLUDE_DIR}")
endif()

if(STENOS_WITH_OPENCL)
	find_package(OpenCL QUIET GLOBAL)
	if(NOT OpenCL_FOUND)
		# build a minimal version of OpenCL
		set (OPENCL_SDK_BUILD_UTILITY_LIBRARIES OFF CACHE INTERNAL "")
		set (OPENCL_SDK_BUILD_CLINFO  OFF CACHE INTERNAL "")
		include(FetchContent)
		FetchContent_Declare(
			OpenCL
			GIT_REPOSITORY https://github.com/KhronosGroup/OpenCL-SDK.git
			GIT_TAG main
			OVERRIDE_FIND_PACKAGE
		)
		message(STATUS "fetching OpenCL")
		find_package(OpenCL REQUIRED GLOBAL)
		#find_package(OpenCL-CLHPP REQUIRED GLOBAL)
	endif()
endif()




# Create target corresponding to compiled objects
add_library(stenos_obj OBJECT ${StenosSources})

# Add linker options
if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	# mingw
	target_link_options(stenos_obj PRIVATE -lKernel32 -lpsapi -lBcrypt)
endif()

if(WIN32)
	target_compile_definitions(stenos_obj PRIVATE -D_CRT_SECURE_NO_WARNINGS)
endif()

# Treat warnings as errors
if(STENOS_NO_WARNINGS)
	if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
		target_compile_options(stenos_obj PRIVATE  /WX /W3 )
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(stenos_obj PRIVATE -Werror -Wall -Wno-c++98-compat -Wno-c++98-compat-pedantic)
	else()
		target_compile_options(stenos_obj PRIVATE -Werror -Wall)
	endif()
endif()
target_compile_definitions(stenos_obj PRIVATE -DSTENOS_BUILD_SHARED_LIBRARY)

# Shared libraries need PIC
set_property(TARGET stenos_obj PROPERTY POSITION_INDEPENDENT_CODE 1)
set_target_properties(stenos_obj PROPERTIES INSTALL_RPATH "$ORIGIN")
target_include_directories(stenos_obj PRIVATE ${CMAKE_BINARY_DIR})

# Link 
target_link_libraries(stenos_obj PUBLIC libzstd_static)
target_include_directories(stenos_obj PUBLIC ${ZSTD_INCLUDE_DIR})

if(STENOS_WITH_OPENCL)
	target_link_libraries(stenos_obj PRIVATE OpenCL::OpenCL OpenCL::HeadersCpp)
	target_compile_definitions(stenos_obj PRIVATE -DSTENOS_OPENCL)
endif()

# Add sse/avx flags
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
	if(AVX2_FOUND)
		message(STATUS "Compile with AVX2 support")
		target_compile_options(stenos_obj PRIVATE ${AVX2_C_FLAGS})
	endif()
else()
	#FIXME: need to be generalised for machines with incorrect binutil installed
	target_compile_options(stenos_obj PRIVATE -march=native)
	if (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		target_compile_options(stenos_obj PRIVATE -mno-avx512f)
	endif()
endif()

# Wide table support
if(STENOS_WIDE_TABLE)
	target_compile_options(stenos_obj PRIVATE -DSTENOS_WIDE_TABLE)
endif()



if(STENOS_BUILD_SHARED)

	# Define stenos target
	add_library(stenos SHARED $<TARGET_OBJECTS:stenos_obj> )
	
	target_link_libraries(stenos PRIVATE libzstd_static)
	target_link_directories(stenos PRIVATE ${ZSTD_LIB_DIR})
	
	if(STENOS_WITH_OPENCL)
		target_link_libraries(stenos PRIVATE OpenCL::OpenCL)
	endif()

	# Export target
	target_include_directories(stenos  PUBLIC
						   "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
						   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
	
	
	install(TARGETS stenos EXPORT stenos DESTINATION ${STENOS_INSTALL_LIBDIR} LIBRARY)
	install(EXPORT stenos DESTINATION ${STENOS_CMAKEDIR})
	
endif()


if(STENOS_BUILD_STATIC)

	# Define stenos_static target
	add_library(stenos_static STATIC $<TARGET_OBJECTS:stenos_obj> )
	
	target_compile_definitions(stenos_static PUBLIC -DSTENOS_STATIC)
	target_link_libraries(stenos_static PRIVATE libzstd_static)
	target_link_directories(stenos_static PRIVATE ${ZSTD_LIB_DIR})
	
	if(STENOS_WITH_OPENCL)
		target_link_libraries(stenos_static PRIVATE OpenCL::OpenCL)
	endif()
	
	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		# correct link error ""relocation R_X86_64_32 against symbol..."
		set_target_properties(stenos_static PROPERTIES CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall --std=c++14 -fPIC" )
		set_target_properties(stenos_static PROPERTIES CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -Wall -fPIC" )
	endif()
	set_property(TARGET stenos_static PROPERTY POSITION_INDEPENDENT_CODE ON)

	# Export target
	target_include_directories(stenos_static  PUBLIC
						   "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
						   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
	
	
	install(TARGETS stenos_static EXPORT stenos_static DESTINATION ${STENOS_INSTALL_LIBDIR} LIBRARY)
	install(EXPORT stenos_static DESTINATION ${STENOS_STATIC_CMAKEDIR})
	
endif()


# add tests
enable_testing()

# add tests
if(STENOS_BUILD_TESTS)
	add_subdirectory(tests)
endif()

# add benchmarks
if(STENOS_BUILD_BENCHS)
	add_subdirectory(benchs)
endif()


# Install headers
file(GLOB StenosHeaders
     "stenos/*.h"
	 "stenos/*.hpp"
)
install(FILES ${StenosHeaders}
		DESTINATION "${CMAKE_INSTALL_PREFIX}/include/stenos"
		)

# Configure and install stenos.pc
configure_file(stenos.pc.in stenos.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stenos.pc
			DESTINATION ${STENOS_INSTALL_DATAROOTDIR}/pkgconfig)


# Configure and install stenos_config.h
configure_file(stenos_config.h.in stenos_config.h @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stenos_config.h
			DESTINATION "${CMAKE_INSTALL_PREFIX}/include/stenos")

# Configure stenos_config.h and put in the stenos folder for FetchContent_Declare from other projects
configure_file(stenos_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/stenos/stenos_config.h @ONLY)

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
