#define _FILE_OFFSET_BITS 64

#include "de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream.h"

#include <lzma.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
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

#define BUFSIZE 4096

typedef struct {
	int fd;
	lzma_stream strm;
	lzma_index *index;
	lzma_block block;
	lzma_filter filters[LZMA_FILTERS_MAX + 1];
	off_t position; /* uncompressed offset of outputBuffer[0] (NOT strm->next_out!) */
	lzma_index_iter iter;
	unsigned char inputBuffer[BUFSIZE];
	unsigned char outputBuffer[BUFSIZE];
} XZInputStream;


static int adjust_input_buffer(XZInputStream *stream) {
	if (0 == stream->strm.avail_in) {
		ssize_t res = read(stream->fd, stream->inputBuffer, BUFSIZE);

		if (res < 0) {
			LOG_ERROR("Reading input buffer failed: %s\n", strerror(errno));
			return -1;
		}

		// LOG_VERBOSE("read %i bytes", (int) res);

		stream->strm.avail_in = res;
		stream->strm.next_in = stream->inputBuffer;

		return res;
	}
	return 0;
}

static void discard_output(XZInputStream *stream) {
	stream->position += BUFSIZE - stream->strm.avail_out;
	stream->strm.avail_out = BUFSIZE;
	stream->strm.next_out = stream->outputBuffer;
}

static void reuse_output(XZInputStream *stream, int bytes) {
	stream->position += BUFSIZE - stream->strm.avail_out - bytes;
	memmove(stream->outputBuffer, stream->strm.next_out - bytes, bytes);
	stream->strm.avail_out = BUFSIZE - bytes;
	stream->strm.next_out = stream->outputBuffer + bytes;
}

static lzma_ret read_index(int fd, lzma_stream *strm, lzma_index **index, lzma_index **tmp_index, uint64_t memlimit);

static void clear_block(lzma_block *block) {
	// Free the memory allocated by lzma_block_header_decode().
	size_t i;
	for (i = 0; block->filters[i].id != LZMA_VLI_UNKNOWN; ++i) {
		free(block->filters[i].options);
	}
}

/*
 * Class:     de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream
 * Method:    openFile
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream_openFile(JNIEnv *env, jobject obj, jstring filename) {
	int fd = -1;
	XZInputStream *stream = NULL;
	lzma_ret ret;
	const char *errmsg = "Couldn't read xz archive";
	lzma_index *tmp_index = NULL;

	{
		const char *filenameUtf8 = (*env)->GetStringUTFChars(env, filename, NULL);
		fd = open(filenameUtf8, O_RDONLY);
		(*env)->ReleaseStringUTFChars(env, filename, filenameUtf8);
	}

	if (-1 == fd) {
		errmsg = "Couldn't open file";
		LOG_ERROR("%s: %s\n", errmsg, strerror(errno));
		goto cleanup;
	}

	stream = calloc(sizeof(*stream), 1);
	stream->fd = fd;
	stream->block.filters = stream->filters;
	stream->block.filters[0].id = LZMA_VLI_UNKNOWN;
	stream->position = -1;

	ret = read_index(fd, &stream->strm, &stream->index, &tmp_index, 16*1024*1024);
	if (NULL != tmp_index) lzma_index_end(tmp_index, NULL);
	if (LZMA_OK != ret) {
		errmsg = "Reading index failed";
		goto cleanup;
	}

	lzma_end(&stream->strm);
	memset(&stream->strm, 0, sizeof(stream->strm));
	lseek(fd, 0, SEEK_SET);

	{
		jlong filesize = lzma_index_uncompressed_size(stream->index);

		jclass cls =  (*env)->GetObjectClass(env, obj);
		jfieldID fNativePtr = (*env)->GetFieldID(env, cls, "nativePtr", "J");
		jfieldID fLength = (*env)->GetFieldID(env, cls, "m_length", "J");

		if (NULL == fNativePtr || NULL == fLength) {
			errmsg = "Initializing java object failed";
			LOG_ERROR("%s\n", errmsg);
			goto cleanup;
		}

		(*env)->SetLongField(env, obj, fLength, filesize);
		(*env)->SetLongField(env, obj, fNativePtr, (jlong) (uint64_t) (intptr_t) stream);
	}

	return;

cleanup:
	if (NULL != stream) {
		if (NULL != stream->index) {
			lzma_index_end(stream->index, NULL);
		}
		lzma_end(&stream->strm);
		free(stream);
	}
	if (-1 != fd) close(fd);

	{
		jclass excCls = (*env)->FindClass(env, "java/io/IOException");
		if (NULL != excCls) (*env)->ThrowNew(env, excCls, errmsg);
	}
}

/*
 * Class:     de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream
 * Method:    closeFile
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream_closeFile(JNIEnv *env, jobject obj) {
	XZInputStream *stream = NULL; // get object

	jclass cls =  (*env)->GetObjectClass(env, obj);
	jfieldID fNativePtr = (*env)->GetFieldID(env, cls, "nativePtr", "J");
	if (NULL == fNativePtr) return;

	stream = (XZInputStream*) (intptr_t) (uint64_t) (*env)->GetLongField(env, obj, fNativePtr);
	(*env)->SetLongField(env, obj, fNativePtr, 0);

	if (NULL != stream) {
		LOG_VERBOSE("closing file\n");

		if (-1 != stream->fd) {
			close(stream->fd);
			stream->fd = -1;
		}
		if (NULL != stream->index) {
			lzma_index_end(stream->index, NULL);
			stream->index = NULL;
		}
		lzma_end(&stream->strm);
		clear_block(&stream->block);
		free(stream);
	}
}

static lzma_ret load_block(XZInputStream *stream, lzma_index_iter *iter) {
#if BUFSIZE < LZMA_BLOCK_HEADER_SIZE_MAX
#	error BUFSIZE < LZMA_BLOCK_HEADER_SIZE_MAX
#endif
	lzma_ret ret;

	//LOG_VERBOSE("seeking to offset %i", (int) iter->block.compressed_file_offset);
	lseek(stream->fd, iter->block.compressed_file_offset, SEEK_SET);

	stream->strm.avail_in = 0; /* make sure we read new data after lseek */

	if (-1 == adjust_input_buffer(stream)) return LZMA_DATA_ERROR;

	//LOG_VERBOSE("clearing block\n");
	clear_block(&stream->block);

	stream->block.version = 0;
	stream->block.check = iter->stream.flags->check;

	stream->block.header_size = lzma_block_header_size_decode(stream->strm.next_in[0]);
	if (stream->block.header_size > stream->strm.avail_in) {
		LOG_ERROR("%s\n", "invalid block header: too large");
		return LZMA_DATA_ERROR;
	}

	// Decode the Block Header.
	ret = lzma_block_header_decode(&stream->block, NULL, stream->strm.next_in);
	if (LZMA_OK != ret) {
		LOG_ERROR("decoding block header failed (%i)\n", ret);
		return ret;
	}

	ret = lzma_block_compressed_size(&stream->block, iter->block.unpadded_size);
	if (LZMA_OK != ret) {
		LOG_ERROR("decoding block header failed: invalid compressed size (%i)\n", ret);
		return ret;
	}

	stream->strm.next_in += stream->block.header_size;
	stream->strm.avail_in -= stream->block.header_size;

	lzma_end(&stream->strm);
	ret = lzma_block_decoder(&stream->strm, &stream->block);
	if (LZMA_OK != ret) {
		LOG_ERROR("couldn't initialize block decoder (%i)\n", ret);
		return ret;
	}

	//LOG_VERBOSE("block loaded\n");

	return LZMA_OK;
}

/*
 * Class:     de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream
 * Method:    readInt
 * Signature: (J[III)V
 */
JNIEXPORT void JNICALL Java_de_unistuttgart_informatik_OfflineToureNPlaner_xz_XZInputStream_readInt(JNIEnv *env, jobject obj, jlong offset, jintArray buffer, jint start, jint length) {
	XZInputStream *stream = NULL;
	lzma_ret ret;
	jlong byteLength = 4 * length;
	jsize arrayLength;
	const char *errmsg = "Couldn't read xz archive";
	int matchingBlock = 0;

	jint *buf = NULL;

	jclass cls =  (*env)->GetObjectClass(env, obj);
	jfieldID fNativePtr = (*env)->GetFieldID(env, cls, "nativePtr", "J");
	if (NULL == fNativePtr) goto failed;

	stream = (XZInputStream*) (intptr_t) (uint64_t) (*env)->GetLongField(env, obj, fNativePtr);
	if (NULL == stream) goto failed;

	arrayLength = (*env)->GetArrayLength(env, buffer);

	if (start < 0 || length > arrayLength - start) goto failed;

	buf = (*env)->GetIntArrayElements(env, buffer, NULL);
	if (NULL == buf) goto failed;

	LOG_VERBOSE("reading ints: offset: %i (current position: %i, block: %i - %i) array start %i, length: %i\n",
		(int) offset, (int) stream->position,
		(int) stream->iter.block.uncompressed_file_offset, (int) (stream->iter.block.uncompressed_file_offset + stream->iter.block.uncompressed_size),
		start, length);

	matchingBlock =
		(  stream->position >= 0
		&& offset >= (off_t)stream->iter.block.uncompressed_file_offset
		&& offset < (off_t)(stream->iter.block.uncompressed_file_offset + stream->iter.block.uncompressed_size));

	if (matchingBlock && stream->position <= offset) {
		/* we already are in the needed block, and still before the requested data; just continue from here */
		LOG_VERBOSE("continue reading for offset %i (current position: %i)\n", (int) offset, (int) stream->position);
	} else {
		if (matchingBlock) {
			/* already passed the index we wanted, but same block */
			LOG_VERBOSE("restarting block: %i (current position: %i)\n", (int) offset, (int) stream->position);
		} else {
			LOG_VERBOSE("searching for offset: %i (current position: %i)\n", (int) offset, (int) stream->position);

			stream->position = -1;
			lzma_index_iter_init(&stream->iter, stream->index);
			if (lzma_index_iter_locate(&stream->iter, offset)) {
				errmsg = "couldn't find offset in index";
				goto failed;
			}

			// LOG_VERBOSE("seeking to new block\n");
		}

		discard_output(stream);
		stream->position = stream->iter.block.uncompressed_file_offset;

		/* restart decoder */
		ret = load_block(stream, &stream->iter);
		if (LZMA_OK != ret) goto failed;
	}

	{
		off_t skip = offset - (stream->position + (BUFSIZE - stream->strm.avail_out));
		if (skip > 0) LOG_VERBOSE("will have to skip %i bytes", (int) skip);
	}

	for (;;) {
		int haveRead;

		if (-1 == adjust_input_buffer(stream)) goto failed;

		if (0 != stream->strm.avail_out) {
			// LOG_VERBOSE("decoding\n");
			ret = lzma_code(&stream->strm, LZMA_RUN);
			if (LZMA_OK != ret && LZMA_STREAM_END != ret) {
				errmsg = "failed decoding data";
				LOG_ERROR("%s (%i)\n", errmsg, ret);
				goto failed;
			}
		}

		haveRead = BUFSIZE - stream->strm.avail_out;
		if (stream->position + haveRead > offset) {
			int i, want, overlap;
			const unsigned char *data;

			/* overlap of read data and data we want: */
			want = overlap = (stream->position + haveRead - offset);
			/* operlap data start */
			data = stream->strm.next_out - want;
			/* limit how much data of the overlap we use in this round */
			if (want > byteLength) want = byteLength;
			want = want & ~0x3; /* only read complete ints */

			/* DECODE want bytes from data into array */
			for (i = 0; i < want;) {
				jint v = data[i++] << 24;
				v |= data[i++] << 16;
				v |= data[i++] << 8;
				v |= data[i++];
				buf[start++] = v;
			}

			byteLength -= want;
			if (byteLength > 0) {
				// LOG_VERBOSE("overlap: %i, wanted: %i, reuse: %i\n", overlap, want, overlap - want);
				reuse_output(stream, overlap - want);
			}
			break;
		}

		// LOG_VERBOSE("skipping data\n");
		discard_output(stream);

		if (LZMA_STREAM_END == ret) {
			// LOG_VERBOSE("block end, loading next\n");
			if (lzma_index_iter_next(&stream->iter, LZMA_INDEX_ITER_ANY)) {
				errmsg = "unexepected end of file";
				LOG_ERROR("%s\n", errmsg);
				goto failed; /* end of file */
			}
			/* restart decoder */
			ret = load_block(stream, &stream->iter);
			if (LZMA_OK != ret) goto failed;
		}
	}

	while (byteLength > 0) {
		int i, haveRead, want;
		const unsigned char *data = stream->outputBuffer;

		if (-1 == adjust_input_buffer(stream)) goto failed;

		if (0 != stream->strm.avail_out) {
			ret = lzma_code(&stream->strm, LZMA_RUN);
			if (LZMA_OK != ret && LZMA_STREAM_END != ret) {
				errmsg = "failed decoding data";
				LOG_ERROR("%s (%i)\n", errmsg, ret);
				goto failed;
			}
		}

		want = haveRead = BUFSIZE - stream->strm.avail_out;
		if (want > byteLength) want = byteLength;
		want = want & ~0x3; /* only read complete ints */

		/* DECODE want bytes from data into array */
		for (i = 0; i < want;) {
			jint v = data[i++] << 24;
			v |= data[i++] << 16;
			v |= data[i++] << 8;
			v |= data[i++];
			buf[start++] = v;
		}

		byteLength -= want;
		if (byteLength > 0) {
			reuse_output(stream, haveRead - want);
			if (LZMA_STREAM_END == ret) {
				if (lzma_index_iter_next(&stream->iter, LZMA_INDEX_ITER_ANY)) {
					errmsg = "unexepected end of file";
					LOG_ERROR("%s\n", errmsg);
					goto failed; /* end of file */
				}
				/* restart decoder */
				ret = load_block(stream, &stream->iter);
				if (LZMA_OK != ret) goto failed;
			}
		}
	}

	// LOG_VERBOSE("read %i ints\n", length);

	(*env)->ReleaseIntArrayElements(env, buffer, buf, 0);

	return;

failed:
	LOG_VERBOSE("reading ints failed: %s\n", errmsg);

	if (NULL != buf) (*env)->ReleaseIntArrayElements(env, buffer, buf, 0);

	lzma_end(&stream->strm);
	memset(&stream->strm, 0, sizeof(stream->strm));
	stream->position = -1;

	{
		jclass excCls = (*env)->FindClass(env, "java/io/IOException");
		if (NULL != excCls) (*env)->ThrowNew(env, excCls, errmsg);
	}
}



static lzma_ret read_index(int fd, lzma_stream *strm, lzma_index **index, lzma_index **tmp_index, uint64_t memlimit) {
	union {
		unsigned char buf[BUFSIZE];
		unsigned char u8[LZMA_STREAM_HEADER_SIZE];
		uint32_t u32[LZMA_STREAM_HEADER_SIZE/4];
	} buf;

	lzma_stream_flags header_flags;
	lzma_stream_flags footer_flags;
	lzma_ret ret;

	// The Index currently being decoded
	lzma_index *this_index = NULL;

	// Current position in the file. We parse the file backwards so
	// initialize it to point to the end of the file.
	off_t pos;

	{
		struct stat st;
		if (-1 == fstat(fd, &st)) {
			LOG_VERBOSE("fstat failed: %s\n", strerror(errno));
			return LZMA_DATA_ERROR;
		}
		pos = st.st_size;
	}

	// Each loop iteration decodes one Index.
	do {
		lzma_vli stream_padding, index_size;
		uint64_t memused;

		// Check that there is enough data left to contain at least
		// the Stream Header and Stream Footer. This check cannot
		// fail in the first pass of this loop.
		if (pos < 2 * LZMA_STREAM_HEADER_SIZE) {
			LOG_VERBOSE("file too small for xz archive\n");
			return LZMA_DATA_ERROR;
		}

		pos -= LZMA_STREAM_HEADER_SIZE;
		stream_padding = 0;

		// Locate the Stream Footer. There may be Stream Padding which
		// we must skip when reading backwards.
		for (;;) {
			ssize_t r;

			if (pos < LZMA_STREAM_HEADER_SIZE) {
				LOG_VERBOSE("file too small for xz archive\n");
				return LZMA_DATA_ERROR;
			}

			if (LZMA_STREAM_HEADER_SIZE != (r = pread(fd, &buf, LZMA_STREAM_HEADER_SIZE, pos))) {
				LOG_VERBOSE("couldn't read stream footer: %s\n", r < 0 ? strerror(errno) : "file too small");
				return LZMA_DATA_ERROR;
			}

			if (buf.u32[2] != 0) break;
			pos -= 4; stream_padding += 4;
			if (buf.u32[1] != 0) break;
			pos -= 4; stream_padding += 4;
			if (buf.u32[0] != 0) break;
			pos -= 4; stream_padding += 4;
		}

		// Decode the Stream Footer.
		ret = lzma_stream_footer_decode(&footer_flags, buf.u8);
		if (LZMA_OK != ret) {
			LOG_VERBOSE("invalid footer (%i)\n", ret);
			return ret;
		}

		// Check that the size of the Index field looks sane.
		index_size = footer_flags.backward_size;
		if ((lzma_vli)(pos) < index_size + LZMA_STREAM_HEADER_SIZE) {
			LOG_VERBOSE("%s\n", "invalid index size");
			return LZMA_DATA_ERROR;
		}

		// Set pos to the beginning of the Index.
		pos -= index_size;

		// See how much memory we can use for decoding this Index.
		memused = NULL != *tmp_index ? lzma_index_memused(*tmp_index) : 0;
		if (memused > memlimit) {
			LOG_VERBOSE("%s\n", "mem limit hit");
			return LZMA_PROG_ERROR;
		}

		// Decode the Index.
		ret = lzma_index_decoder(strm, &this_index, memlimit - memused);
		if (ret != LZMA_OK) {
			LOG_VERBOSE("couldn't allocate new index (%i)\n", ret);
			return ret;
		}
		*index = this_index;

		do {
			ssize_t r, want = (index_size < BUFSIZE ? index_size : BUFSIZE);
			if (want < 0) { ret = LZMA_DATA_ERROR; break; }

			// Don't give the decoder more input than the index size.
			if (want != (r = pread(fd, &buf, want, pos))) {
				LOG_VERBOSE("couldn't read index data: %s\n", r < 0 ? strerror(errno) : "file too small");
				return LZMA_DATA_ERROR;
			}

			strm->avail_in = want;
			pos += want;
			index_size -= want;

			strm->next_in = buf.buf;
			ret = lzma_code(strm, LZMA_RUN);
		} while (ret == LZMA_OK);

		// If the decoding seems to be successful, check also that
		// the Index decoder consumed as much input as indicated
		// by the Backward Size field.
		if (ret == LZMA_STREAM_END)
			if (index_size != 0 || strm->avail_in != 0)
				ret = LZMA_DATA_ERROR;

		if (ret != LZMA_STREAM_END) {
			// LZMA_BUFFER_ERROR means that the Index decoder
			// would have liked more input than what the Index
			// size should be according to Stream Footer.
			// The message for LZMA_DATA_ERROR makes more
			// sense in that case.
			if (ret == LZMA_BUF_ERROR)
				ret = LZMA_DATA_ERROR;

			LOG_VERBOSE("decoding index failed (%i)\n", ret);
			return ret;
		}

		// Decode the Stream Header and check that its Stream Flags
		// match the Stream Footer.
		pos -= footer_flags.backward_size + LZMA_STREAM_HEADER_SIZE;
		if ((lzma_vli)(pos) < lzma_index_total_size(this_index)) {
			LOG_VERBOSE("%s\n", "invalid archive - index large than available data");
			return LZMA_DATA_ERROR;
		}

		{
			ssize_t r;
			pos -= lzma_index_total_size(this_index);
			if (LZMA_STREAM_HEADER_SIZE != (r = pread(fd, &buf, LZMA_STREAM_HEADER_SIZE, pos))) {
				LOG_VERBOSE("couldn't read stream header: %s\n", r < 0 ? strerror(errno) : "file too small");
				return LZMA_DATA_ERROR;
			}
		}

		ret = lzma_stream_header_decode(&header_flags, buf.u8);
		if (ret != LZMA_OK) {
			LOG_VERBOSE("invalid header (%i)\n", ret);
			return ret;
		}

		ret = lzma_stream_flags_compare(&header_flags, &footer_flags);
		if (ret != LZMA_OK) {
			LOG_VERBOSE("%s\n", "invalid stream: footer doesn't match header");
			return ret;
		}

		// Store the decoded Stream Flags into this_index. This is
		// needed so that we can print which Check is used in each
		// Stream.
		ret = lzma_index_stream_flags(this_index, &footer_flags);
		if (ret != LZMA_OK) return ret;

		// Store also the size of the Stream Padding field. It is
		// needed to show the offsets of the Streams correctly.
		ret = lzma_index_stream_padding(this_index, stream_padding);
		if (ret != LZMA_OK) return ret;

		if (NULL != *tmp_index) {
			// Append the earlier decoded Indexes
			// after this_index.
			ret = lzma_index_cat(this_index, *tmp_index, NULL);
			*tmp_index = NULL;
			if (ret != LZMA_OK) {
				LOG_VERBOSE("%s\n", "failed to concatenate indexes");
				return ret;
			}
		}
		*tmp_index = this_index;
		*index = this_index = NULL;
	} while (pos > 0);

	*index = *tmp_index;
	*tmp_index = NULL;

	return LZMA_OK;
}
