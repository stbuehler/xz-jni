cmake_minimum_required(VERSION 2.8)
project(xz-jni)

find_path(XZ_INCLUDE_DIR lzma.h DOC "xz header location")
find_library(XZ_LIB NAMES liblzma.so liblzma.a)

if(NOT XZ_INCLUDE_DIR OR NOT XZ_LIB)
	message(FATAL_ERROR "Couldn't find xz headers/lib")
endif(NOT XZ_INCLUDE_DIR OR NOT XZ_LIB)


include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${XZ_INCLUDE_DIR})

set(LIBS ${XZ_LIB} z)

if(ANDROID)
	set(LIBS ${LIBS} log)
else(ANDROID)
	find_package(JNI REQUIRED)
	include_directories(${INCLUDE_DIRECTORIES} ${JNI_INCLUDE_DIRS})
endif(ANDROID)

set(XZJNI_SOURCES
	file.cpp
	xz-file.cpp
	idx-defl-file.cpp
	de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream.cpp
)

add_library(xz-jni SHARED ${XZJNI_SOURCES})
target_link_libraries(xz-jni ${LIBS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wmissing-declarations -Wcast-align -Wsign-compare -Wpointer-arith -std=c++11 -D_FILE_OFFSET_BITS=64  -D_LARGEFILE_SOURCE -D_LARGE_FILES")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wmissing-declarations -Wcast-align -Wsign-compare -Wpointer-arith -Wdeclaration-after-statement -Wnested-externs -D_FILE_OFFSET_BITS=64  -D_LARGEFILE_SOURCE -D_LARGE_FILES")

set(WARN_LDFLAGS "-Wall -Wl,--as-needed")

target_link_libraries(xz-jni ${WARN_LDFLAGS})

if(NOT ANDROID)
	add_executable(idx-deflate idx-deflate.cpp file.cpp)
	target_link_libraries(idx-deflate ${LIBS})

	add_executable(idx-inflate idx-inflate.cpp file.cpp idx-defl-file.cpp)
	target_link_libraries(idx-inflate ${LIBS})

	add_executable(xz-inflate xz-inflate.cpp file.cpp xz-file.cpp)
	target_link_libraries(xz-inflate ${LIBS})
endif(NOT ANDROID)
