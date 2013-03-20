
#include "xz-file.h"
#include "idx-defl-file.h"

#include "de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>

#ifdef ANDROID

# include <android/log.h>
# define LOG_VERBOSE(...) __android_log_print(ANDROID_LOG_VERBOSE, "xz-jni", __VA_ARGS__)
# define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "xz-jni", __VA_ARGS__)

#else

# define LOG_VERBOSE(...) fprintf(stderr, __VA_ARGS__)
# define LOG_ERROR(...) fprintf(stderr, __VA_ARGS__)

#endif

#if 1
# undef LOG_VERBOSE
# define LOG_VERBOSE(...) do { } while(0)
#endif

/*
 * Class:     de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream
 * Method:    openFile
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream_openFile(JNIEnv *env, jobject obj, jstring filename) {
	std::string error("Couldn't read xz archive");

	std::shared_ptr<NormalFile> osfile;
	std::shared_ptr<XZFile> xzfile;
	std::shared_ptr<IndexedDeflateFile> idxdeflfile;
	FileReader *reader = nullptr;
	jlong filesize = 0;
	FileReaderState *state = nullptr;

	static const unsigned char idxdefl_magic_header[8] = "idxdefl";
	unsigned char magic_header[sizeof(idxdefl_magic_header)];

	{
		const char *filenameUtf8 = env->GetStringUTFChars(filename, NULL);
		osfile.reset(new MMappedFile(filenameUtf8, error));
		env->ReleaseStringUTFChars(filename, filenameUtf8);
	}

	if (!osfile->valid()) goto failed;

	if (!osfile->readInto(state, 0, sizeof(idxdefl_magic_header), magic_header, error)) {
		osfile->finish(state);
		goto failed;
	}
	osfile->finish(state);

	if (0 == memcmp(idxdefl_magic_header, magic_header, sizeof(idxdefl_magic_header))) {
		idxdeflfile.reset(new IndexedDeflateFile(osfile, error));
		if (!idxdeflfile->valid()) goto failed;

		reader = new FileReader(idxdeflfile);

		filesize = idxdeflfile->filesize();
	} else {
		xzfile.reset(new XZFile(osfile, error));
		if (!xzfile->valid()) goto failed;

		reader = new FileReader(xzfile);

		filesize = xzfile->filesize();
	}

	{
		jclass cls =  env->GetObjectClass(obj);
		jfieldID fNativePtr = env->GetFieldID(cls, "nativePtr", "J");
		jfieldID fLength = env->GetFieldID(cls, "m_length", "J");

		if (NULL == fNativePtr || NULL == fLength) {
			error.assign("Initializing java object failed");
			goto failed;
		}

		env->SetLongField(obj, fLength, filesize);
		env->SetLongField(obj, fNativePtr, (jlong) (intptr_t) reader);
	}

	return;

failed:
	LOG_ERROR("opening xz-archive failed: %s\n", error.c_str());

	if (nullptr != reader) delete reader;

	{
		jclass excCls = env->FindClass("java/io/IOException");
		if (nullptr != excCls) env->ThrowNew(excCls, error.c_str());
	}

}

/*
 * Class:     de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream
 * Method:    closeFile
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream_closeFile(JNIEnv *env, jobject obj) {
	FileReader *reader;

	jclass cls =  env->GetObjectClass(obj);
	jfieldID fNativePtr = env->GetFieldID(cls, "nativePtr", "J");
	if (nullptr == fNativePtr) return;

	reader = (FileReader*) (intptr_t) env->GetLongField(obj, fNativePtr);
	env->SetLongField(obj, fNativePtr, 0);

	if (nullptr != reader) {
		LOG_VERBOSE("closing file\n");
		delete reader;
	}
}

/*
 * Class:     de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream
 * Method:    readInt
 * Signature: (J[III)V
 */
JNIEXPORT void JNICALL Java_de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream_readInt(JNIEnv *env, jobject obj, jlong offset, jintArray buffer, jint start, jint length) {
	FileReader *reader;

	jsize arrayLength;
	jint *buf = nullptr;

	std::string error("Couldn't read xz archive");

	jclass cls =  env->GetObjectClass(obj);
	jfieldID fNativePtr = env->GetFieldID(cls, "nativePtr", "J");
	if (nullptr == fNativePtr) goto failed;

	reader = (FileReader*) (intptr_t) env->GetLongField(obj, fNativePtr);
	if (nullptr == reader) goto failed;

	arrayLength = env->GetArrayLength(buffer);

	if (start < 0 || start > arrayLength || length > arrayLength - start) goto failed;

	buf = env->GetIntArrayElements(buffer, NULL);
	if (nullptr == buf) goto failed;

	reader->seek(offset);

	if (!reader->readInto((unsigned char*) (buf + start), 4 * length)) {
		error.assign(reader->lastError());
		goto failed;
	}
	for (int i = 0; i < length; ++i) {
		buf[start+i] = htonl(buf[start+i]);
	}

	env->ReleaseIntArrayElements(buffer, buf, 0);

	return;

failed:
	LOG_VERBOSE("reading ints failed: %s\n", error.c_str());

	if (nullptr != buf) env->ReleaseIntArrayElements(buffer, buf, 0);

	{
		jclass excCls = env->FindClass("java/io/IOException");
		if (nullptr != excCls) env->ThrowNew(excCls, error.c_str());
	}
}
