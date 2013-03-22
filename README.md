xz JNI wrapper
==============

The java library is rather slow. Otoh the C API for random access isn't very well documented...

This library only reads big endian integer arrays, but it should be easy to modify to read simple byte arrays.

Keep in mind: random access needs small block sizes, or it will be really slow.


(This library also supports a custom compression format, see doc/indexed-deflate-format.txt)

License
-------

xz-jni is in the public domain.

As usual, this software is provided "as is", without any warranty.

Dependencies
------------

 * C++11
 * zlib
 * liblzma-dev
 * JNI
 * cmake/ant environment

Building
--------

Build native part for android $ARMABI:
(needs liblzma from xz)

	mkdir "build_${ARMABI}"
	cd "build_${ARMABI}"
	cmake \
		-DCMAKE_TOOLCHAIN_FILE=../cmake/android.toolchain.cmake \
		-DANDROID_ABI="${ARMABI}" \
		-DANDROID_NATIVE_API_LEVEL="android-9" \
		-DXZ_INCLUDE_DIR="../../xz/src/liblzma/api" \
		-DXZ_LIB="../../xz/build_${ARMABI}/src/liblzma/.libs/liblzma.a" \
		-DCMAKE_BUILD_TYPE="RelWithDebInfo" \
		../

Build jar:

	cd java
	ant dist
