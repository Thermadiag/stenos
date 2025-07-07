cmake_minimum_required(VERSION 3.16)
include(FetchContent)

project(stenos
    VERSION 0.2
    DESCRIPTION "Compression library for binary data"
    HOMEPAGE_URL "https://github.com/Thermadiag/stenos"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

# project options
option(STENOS_ENABLE_AVX2 "Enable AVX2 support" ON)
option(STENOS_WIDE_TABLE "Enable wide table support for RLE encoding/decoding" OFF)
option(STENOS_BUILD_ZSTD "Fetch and build zstd internally" OFF)
option(STENOS_BUILD_TESTS "Build tests" OFF)
option(STENOS_BUILD_BENCHS "Build benchmarks" OFF)
option(STENOS_NO_WARNINGS "Treat warnings as errors" OFF)
option(STENOS_BUILD_SHARED "Build shared library" ON)
option(STENOS_BUILD_STATIC "Build static library" ON)

# create source file list
file(GLOB StenosSources
     "stenos/*.h"
	 "stenos/*.hpp"
	 "stenos/internal/*.h"
	 "stenos/internal/*.hpp"
     "stenos/internal/*.cpp" 
)


set(STENOS_INSTALL_LIBDIR       "${CMAKE_INSTALL_LIBDIR}")
set(STENOS_INSTALL_BINDIR       "${CMAKE_INSTALL_BINDIR}")
set(STENOS_INSTALL_DATAROOTDIR  "${CMAKE_INSTALL_DATAROOTDIR}")
set(STENOS_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/stenos")

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
	FetchContent_Declare(
		zstd
		GIT_REPOSITORY "https://github.com/facebook/zstd.git"
		GIT_TAG v${ZSTD_MINIMUM_VERSION}
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


# Configure stenos_config.h
configure_file(stenos_config.h.in stenos_config.h @ONLY)

if(STENOS_BUILD_SHARED)
	# add sources
	add_library(stenos SHARED ${StenosSources})

	# add preprocessor definitions
	target_compile_definitions(stenos PRIVATE -DSTENOS_BUILD_SHARED_LIBRARY)
	
	#add linker options
	if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
		# mingw
		target_link_options(stenos PRIVATE -lKernel32 -lpsapi -lBcrypt)
	endif()

	if(WIN32)
		target_compile_definitions(stenos PRIVATE -D_CRT_SECURE_NO_WARNINGS)
	endif()
	
	# add sse/avx flags
	if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
		if(STENOS_ENABLE_AVX2)
			target_compile_options(stenos PRIVATE /arch:AVX2)
		endif()
	else()
		target_compile_options(stenos PRIVATE -march=native)
	endif()
	
	if(STENOS_WIDE_TABLE)
		target_compile_options(stenos PRIVATE -DSTENOS_WIDE_TABLE)
	endif()
	
	if(STENOS_NO_WARNINGS)
		if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
			target_compile_options(stenos PRIVATE  /WX /W3 )
		elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			target_compile_options(stenos PRIVATE -Werror -Wall -Wno-c++98-compat -Wno-c++98-compat-pedantic)
		else()
			target_compile_options(stenos PRIVATE -Werror -Wall)
		endif()
	endif()
	
	target_link_libraries(stenos PRIVATE libzstd_static)
	target_link_directories(stenos PRIVATE ${ZSTD_LIB_DIR})
	target_include_directories(stenos PRIVATE ${ZSTD_INCLUDE_DIR})

	target_include_directories(stenos PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

	target_include_directories(stenos PUBLIC
						   "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
						   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
						   
	install(TARGETS stenos EXPORT stenos ARCHIVE DESTINATION ${STENOS_INSTALL_LIBDIR} RUNTIME DESTINATION ${STENOS_INSTALL_BINDIR} LIBRARY DESTINATION ${STENOS_INSTALL_LIBDIR})
	install(EXPORT stenos DESTINATION ${STENOS_CMAKEDIR})
	
endif()


if(STENOS_BUILD_STATIC)
	# add sources
	add_library(stenos_static STATIC ${StenosSources})

	# add preprocessor definitions
	target_compile_definitions(stenos_static PRIVATE -DSTENOS_BUILD_STATIC_LIBRARY)
	target_compile_definitions(stenos_static PUBLIC -DSTENOS_STATIC)
	
	#add linker options
	if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
		# mingw
		target_link_options(stenos_static PRIVATE -lKernel32 -lpsapi -lBcrypt)
	endif()

	if(WIN32)
		target_compile_definitions(stenos_static PRIVATE -D_CRT_SECURE_NO_WARNINGS)
	endif()
	
	# add sse/avx flags
	if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
		if(STENOS_ENABLE_AVX2)
			target_compile_options(stenos_static PRIVATE /arch:AVX2)
		endif()
	else()
		target_compile_options(stenos_static PRIVATE -march=native)
	endif()
	
	if(STENOS_WIDE_TABLE)
		target_compile_options(stenos PRIVATE -DSTENOS_WIDE_TABLE)
	endif()
	
	if(STENOS_NO_WARNINGS)
		if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
			target_compile_options(stenos_static PRIVATE  /WX /W3 )
		elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			target_compile_options(stenos_static PRIVATE -Werror -Wall -Wno-c++98-compat -Wno-c++98-compat-pedantic)
		else()
			target_compile_options(stenos_static PRIVATE -Werror -Wall)
		endif()
	endif()

	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		# correct link error ""relocation R_X86_64_32 against symbol..."
		set_target_properties(stenos_static PROPERTIES CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall --std=c++14 -fPIC" )
		set_target_properties(stenos_static PROPERTIES CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -Wall -fPIC" )
	endif()
	
	target_link_libraries(stenos_static PRIVATE libzstd_static)
	target_link_directories(stenos_static PRIVATE ${ZSTD_LIB_DIR})
	target_include_directories(stenos_static PRIVATE ${ZSTD_INCLUDE_DIR})

	target_include_directories(stenos_static PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

	target_include_directories(stenos_static  PUBLIC
						   "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
						   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
	
	
	install(TARGETS stenos_static EXPORT stenos DESTINATION ${STENOS_INSTALL_LIBDIR} LIBRARY)
	install(EXPORT stenos DESTINATION ${STENOS_CMAKEDIR})
	
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

configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/stenosConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/stenosConfig.cmake"
  INSTALL_DESTINATION ${STENOS_INSTALL_LIBDIR}/cmake/stenos
  PATH_VARS )

  
# Generate stenosConfigVersion.cmake
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/stenosConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion )
  
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stenosConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/stenosConfigVersion.cmake
        DESTINATION ${STENOS_INSTALL_LIBDIR}/cmake/stenos )
		
