cmake_minimum_required(VERSION 2.8)
project(xz-jni)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

MACRO(ADD_TARGET_PROPERTIES _target _name)
	SET(_properties)
	FOREACH(_prop ${ARGN})
		SET(_properties "${_properties} ${_prop}")
	ENDFOREACH(_prop)
	GET_TARGET_PROPERTY(_old_properties ${_target} ${_name})
	MESSAGE(STATUS "adding property to ${_target} ${_name}:" ${_properties})
	IF(NOT _old_properties)
		# in case it's NOTFOUND
		SET(_old_properties)
	ENDIF(NOT _old_properties)
	SET_TARGET_PROPERTIES(${_target} PROPERTIES ${_name} "${_old_properties} ${_properties}")
ENDMACRO(ADD_TARGET_PROPERTIES)

if(NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)


# can be disabled in the cache
set(WARN_LDFLAGS "-Wall -Wl,--as-needed -Wl,--no-undefined -fvisibility=hidden" CACHE STRING "standard ld warnings")
set(WARN_C_FLAGS "-Wall -Wmissing-declarations -Wcast-align -Wsign-compare -Wpointer-arith -Wdeclaration-after-statement -Wnested-externs" CACHE STRING "standard c warnings")
set(WARN_CXX_FLAGS "-Wall -Wmissing-declarations -Wcast-align -Wsign-compare -Wpointer-arith" CACHE STRING "standard c++ warnings")

# set these always
set(OTHER_CFLAGS "-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGE_FILES -fvisibility=hidden")

# apply configured flags
set(CMAKE_CXX_FLAGS "-fPIC -std=c++11 ${CMAKE_CXX_FLAGS} ${WARN_CXX_FLAGS} ${OTHER_CFLAGS}")
set(CMAKE_C_FLAGS "-fPIC ${CMAKE_C_FLAGS} ${WARN_C_FLAGS} ${OTHER_CFLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${WARN_LDFLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${WARN_LDFLAGS}")

find_path(XZ_INCLUDE_DIR lzma.h DOC "xz header location")
find_library(XZ_LIB NAMES liblzma.so liblzma.a)

if(NOT XZ_INCLUDE_DIR OR NOT XZ_LIB)
	message(FATAL_ERROR "Couldn't find xz headers/lib")
endif(NOT XZ_INCLUDE_DIR OR NOT XZ_LIB)

include_directories(${XZ_INCLUDE_DIR})

set(COMMON_LIBS ${XZ_LIB} z)

if(ANDROID)
	set(COMMON_LIBS ${COMMON_LIBS} log)
else(ANDROID)
	find_package(JNI REQUIRED)
	include_directories(${INCLUDE_DIRECTORIES} ${JNI_INCLUDE_DIRS})
endif(ANDROID)

add_library(common OBJECT
	lib/file.cpp
	lib/xz-file.cpp
	lib/idx-defl-file.cpp
)

add_library(xz-jni SHARED
	lib/de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream.cpp
	$<TARGET_OBJECTS:common>
)
target_link_libraries(xz-jni ${COMMON_LIBS})

if(NOT ANDROID)
	add_executable(idx-deflate tools/idx-deflate.cpp $<TARGET_OBJECTS:common>)
	target_link_libraries(idx-deflate ${COMMON_LIBS} z)

	add_executable(idx-inflate tools/idx-inflate.cpp $<TARGET_OBJECTS:common>)
	target_link_libraries(idx-inflate ${COMMON_LIBS} z)

	add_executable(xz-inflate tools/xz-inflate.cpp $<TARGET_OBJECTS:common>)
	target_link_libraries(xz-inflate ${COMMON_LIBS} ${XZ_LIB})
endif(NOT ANDROID)
