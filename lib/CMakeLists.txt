cmake_minimum_required(VERSION 2.8)
project(xz-jni)

find_path(XZ_INCLUDE_DIR lzma.h DOC "xz header location")
find_library(XZ_LIB NAMES liblzma.a liblzma.so)

if(NOT XZ_INCLUDE_DIR OR NOT XZ_LIB)
	message(FATAL_ERROR "Couldn't find xz headers/lib")
endif(NOT XZ_INCLUDE_DIR OR NOT XZ_LIB)


include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${XZ_INCLUDE_DIR})

set(LIBS ${XZ_LIB})

if(ANDROID)
	set(LIBS ${LIBS} log)
else(ANDROID)
	find_package(JNI REQUIRED)
	include_directories(${INCLUDE_DIRECTORIES} ${JNI_INCLUDE_DIRS})
endif(ANDROID)

set(XZJNI_SOURCES
	de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream.c
)

add_library(xz-jni SHARED ${XZJNI_SOURCES})
target_link_libraries(xz-jni ${LIBS})


SET(WARN_CFLAGS "-g -O2 -Wall -Wmissing-declarations -Wdeclaration-after-statement -Wcast-align -Wsign-compare -Wnested-externs -Wpointer-arith")
SET(WARN_LDFLAGS "-g -O2 -Wall -Wl,--as-needed")

TARGET_LINK_LIBRARIES(xz-jni ${WARN_LDFLAGS})
ADD_TARGET_PROPERTIES(xz-jni COMPILE_FLAGS ${WARN_CFLAGS})
