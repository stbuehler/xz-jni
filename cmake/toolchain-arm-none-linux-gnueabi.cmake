# in part from http://www.ros.org/doc/api/eros_toolchains/html/crossdev_2i686-pc-mingw32_8cmake_source.html

set(TOOLCHAIN_FAMILY "crossdev")
set(TOOLCHAIN_TUPLE "arm-none-linux-gnueabi" CACHE STRING "Toolchain signature identifying cpu-vendor-platform-clibrary.")
set(TOOLCHAIN_SYSROOT "/usr/${TOOLCHAIN_TUPLE}" CACHE STRING "Root of the target development environment (libraries, headers etc).")
set(TOOLCHAIN_INSTALL_PREFIX "${TOOLCHAIN_SYSROOT}/usr" CACHE STRING "Preferred install location when using the toolchain.")

# this one is important
set(CMAKE_SYSTEM_NAME Linux)
#this one not so much
set(CMAKE_SYSTEM_VERSION 1)

# specify the cross compiler
set(CMAKE_C_COMPILER   ${TOOLCHAIN_TUPLE}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_TUPLE}-g++)

# where is the target environment 
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_SYSROOT} CACHE STRING "Cmake search variable for finding libraries/headers.")

# search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)