#include "xz-file.h"

#include <sstream>
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

static void errnoLzmaToStr(const char *prefix, lzma_ret res, std::string &error) {
	std::ostringstream s;
	s << prefix << ": ";
	switch (res) {
	case LZMA_OK:
		s << "Operation completed successfully";
		break;
	case LZMA_STREAM_END:
		s << "End of stream was reached";
		break;
	case LZMA_NO_CHECK:
		s << "Input stream has no integrity check";
		break;
	case LZMA_UNSUPPORTED_CHECK:
		s << "Cannot calculate the integrity check";
		break;
	case LZMA_GET_CHECK:
		s << "Integrity check type is now available";
		break;
	case LZMA_MEM_ERROR:
		s << "Cannot allocate memory";
		break;
	case LZMA_MEMLIMIT_ERROR:
		s << "Memory usage limit was reached";
		break;
	case LZMA_FORMAT_ERROR:
		s << "File format not recognized";
		break;
	case LZMA_OPTIONS_ERROR:
		s << "Invalid or unsupported options";
		break;
	case LZMA_DATA_ERROR:
		s << "Data is corrupt";
		break;
	case LZMA_BUF_ERROR:
		s << "No progress is possible";
		break;
	case LZMA_PROG_ERROR:
		s << "Programming error";
		break;
	default:
		s << "Unknown error (" << ((int) res) << ")";
		break;
	}
	error.assign(s.str());
}


class XZFileReaderState : public FileReaderState {
public:
	int64_t position; /* uncompressed offset of currentBuffer[0] (NOT strm->next_out!) */

	/* if position >= 0 the folling data is "active" */
	/* decoder for current block */
	lzma_stream strm;
	/* current block (checksums, flags, filters, ...) */
	lzma_block block;
	/* block needs a reference to this list of filters (why on earth do you have to setup this manually? -.-)
	 * also these filters have pointers that needs to be free()d with clearFilters() */
	lzma_filter filters[LZMA_FILTERS_MAX + 1];
	/* current block offset and size (uncompressed, compressed) */
	lzma_index_iter iter;

	unsigned char *currentBuffer;
	size_t currentBufferSize;

	unsigned char defaultOutputBuffer[4096];

	FileReader reader;

	XZFileReaderState(File file, lzma_index *index)
	: currentBuffer(nullptr), currentBufferSize(0), reader(file) {
		LOG_VERBOSE("XZFileReaderState\n");
		memset(&strm, 0, sizeof(strm));

		for (int i = 0; i <= LZMA_FILTERS_MAX; ++i) {
			filters[i].id = LZMA_VLI_UNKNOWN;
			filters[i].options = nullptr;
		}

		block.filters = filters;
		position = -1;
		selectDefaultBuffer();

		lzma_index_iter_init(&iter, index);
	}

	void clearFilters() {
		LOG_VERBOSE("clearFilters\n");
		// Free the memory allocated by lzma_block_header_decode().
		for (int i = 0; (i < LZMA_FILTERS_MAX) && (filters[i].id != LZMA_VLI_UNKNOWN); ++i) {
			free(filters[i].options);
			filters[i].id = LZMA_VLI_UNKNOWN;
		}
	}

	void selectDefaultBuffer() {
		LOG_VERBOSE("selectDefaultBuffer\n");
		/* selectBuffer flushes the current buffer, so only call it when necesary */
		if (defaultOutputBuffer != currentBuffer || sizeof(defaultOutputBuffer) != currentBufferSize) {
			selectBuffer(defaultOutputBuffer, sizeof(defaultOutputBuffer));
		}
	}

	void selectBuffer(unsigned char *buf, size_t size) {
		LOG_VERBOSE("selectBuffer, %i\n", (int) size);
		if (position >= 0) discard_output();
		currentBuffer = buf;
		currentBufferSize = size;
		strm.next_out = currentBuffer;
		strm.avail_out = currentBufferSize;
	}

	~XZFileReaderState() {
		LOG_VERBOSE("~XZFileReaderState\n");
		clearFilters();
		lzma_end(&strm);
	}

	/* available output bytes in currentBuffer (== strm.next_out - availableBytes()) */
	size_t availableBytes() {
		return currentBufferSize - strm.avail_out;
	}

	bool fill_input_buffer(std::string &error) {
		if (0 == strm.avail_in) {
			const unsigned char *data;
			ssize_t datasize;
			LOG_VERBOSE("fill_input_buffer: reading at offset %i\n", (int) reader.offset());
			if (!reader.read(4*1024, data, datasize)) {
				error.assign(reader.lastError());
				return false;
			}
			LOG_VERBOSE("fill_input_buffer: read %i bytes\n", (int) datasize);
			strm.next_in = data;
			strm.avail_in = datasize;
		}
		return true;
	}

	void discard_output() {
		if (position >= 0) position += availableBytes();
		strm.next_out = currentBuffer;
		strm.avail_out = currentBufferSize;
	}

	bool loadBlock(std::string &error) {
		clearFilters();

		position = -1;

		//LOG_VERBOSE("seeking to offset %i", (int) iter->block.compressed_file_offset);
		reader.seek(iter.block.compressed_file_offset);
		strm.avail_in = 0; /* make sure we read new data after lseek */

		if (!fill_input_buffer(error)) return false;
		if (0 == strm.avail_in) {
			error.assign("Unexpected end of file while trying to read block header");
			return false;
		}

		block.version = 0;
		block.check = iter.stream.flags->check;

		block.header_size = lzma_block_header_size_decode(strm.next_in[0]);
		if (block.header_size > strm.avail_in) {
			error.assign("Unexpected end of file while trying to read block header");
			return false;
		}

		// Decode the Block Header.
		lzma_ret ret = lzma_block_header_decode(&block, NULL, strm.next_in);
		if (LZMA_OK != ret) {
			errnoLzmaToStr("decoding block header failed", ret, error);
			return false;
		}

		ret = lzma_block_compressed_size(&block, iter.block.unpadded_size);
		if (LZMA_OK != ret) {
			errnoLzmaToStr("decoding block header failed, invalid compressed size", ret, error);
			return false;
		}

		strm.next_in += block.header_size;
		strm.avail_in -= block.header_size;

		lzma_end(&strm);
		ret = lzma_block_decoder(&strm, &block);
		if (LZMA_OK != ret) {
			errnoLzmaToStr("couldn't initialize block decoder", ret, error);
			return false;
		}

		position = iter.block.uncompressed_file_offset;
		return true;
	}

	bool seekBlockFor(int64_t offset, std::string &error) {
		bool matchingBlock =
			(position >= 0
			&& offset >= (int64_t)iter.block.uncompressed_file_offset
			&& offset < (int64_t)(iter.block.uncompressed_file_offset + iter.block.uncompressed_size));

		if (matchingBlock && position <= offset) {
			/* we already are in the needed block, and still before the requested data; just continue from here */
			LOG_VERBOSE("continue reading for offset %i (current position: %i)\n", (int) offset, (int) position);
		} else {
			if (matchingBlock) {
				/* already passed the index we wanted, but same block */
				LOG_VERBOSE("restarting block: %i (current position: %i)\n", (int) offset, (int) position);
			} else {
				LOG_VERBOSE("searching for offset: %i (current position: %i)\n", (int) offset, (int) position);

				position = -1;
				if (lzma_index_iter_locate(&iter, offset)) {
					error.assign("couldn't find offset in index");
					return false;
				}

				// LOG_VERBOSE("seeking to new block\n");
			}

			discard_output();

			/* restart decoder */
			if (!loadBlock(error)) return false;
		}

		return true;
	}

	bool decode(std::string &error) {
		assert(0 != strm.avail_out);
		const unsigned char *pos = strm.next_out;

		for (;;) {
			if (!fill_input_buffer(error)) return false;

			if (0 == strm.avail_in) {
				error.assign("Unexpected end of file");
				return false;
			}

			lzma_ret ret = lzma_code(&strm, LZMA_RUN);
			if (LZMA_OK != ret && LZMA_STREAM_END != ret) {
				errnoLzmaToStr("failed decoding data", ret, error);
				return false;
			}

			if (LZMA_STREAM_END == ret && pos == strm.next_out) {
				/* end of stream AND we didn't get new data this round */
				if (lzma_index_iter_next(&iter, LZMA_INDEX_ITER_ANY)) {
					error.assign("Unexepected end of file");
					return false;
				}
				/* restart decoder */
				if (!loadBlock(error)) return false;
			} else {
				return true;
			}
		}
	}

	bool decodeFillBuffer(std::string &error) {
		for (;0 != strm.avail_out;) {
			LOG_VERBOSE("decodeFillBuffer: %i bytes to go\n", (int) strm.avail_out);

			if (!fill_input_buffer(error)) return false;

			if (0 == strm.avail_in) {
				error.assign("Unexpected end of file");
				return false;
			}

			lzma_ret ret = lzma_code(&strm, LZMA_RUN);
			if (LZMA_OK != ret && LZMA_STREAM_END != ret) {
				errnoLzmaToStr("failed decoding data", ret, error);
				return false;
			}

			if (0 == strm.avail_out) return true; // done filling buffer

			if (LZMA_STREAM_END == ret) {
				if (lzma_index_iter_next(&iter, LZMA_INDEX_ITER_ANY)) {
					error.assign("Unexepected end of file");
					return false;
				}
				/* restart decoder */
				if (!loadBlock(error)) return false;
			}
		}
		return true;
	}
};

static lzma_index* read_index(File file, uint64_t memlimit, std::string &error);

XZFile::XZFile(File file, std::string &error /* out */)
: m_file(file), m_index(nullptr) {
	m_index = read_index(file, 16*1024*1024, error);
}

XZFile::~XZFile() {
	if (nullptr != m_index) {
		lzma_index_end(m_index, NULL);
		m_index = nullptr;
	}
}

bool XZFile::valid() {
	return (nullptr != m_index) && m_file;
}

int64_t XZFile::filesize() {
	return (nullptr != m_index) ? lzma_index_uncompressed_size(m_index) : 0;
}

bool XZFile::read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */) {
	if (!valid()) {
		error.assign("Invalid file");
		return false;
	}

	XZFileReaderState *state;
	if (nullptr == internalState) {
		internalState = state = new XZFileReaderState(m_file, m_index);
	} else {
		state = dynamic_cast<XZFileReaderState*>(internalState);
		assert(nullptr != state);
	}

	state->selectDefaultBuffer(); // always reset buffer, readInto might have left an old pointer
	if (!state->seekBlockFor(offset, error)) return false;

	for (;;) {
		ssize_t have = state->availableBytes();
		if (state->position + have > offset) {
			ssize_t overlap = (state->position + have - offset);
			data = state->strm.next_out - have;
			datasize = overlap;
			return true;
		}

		state->discard_output();
		if (!state->decode(error)) return false;
	}

	return false;
}

bool XZFile::readInto(FileReaderState* &internalState, int64_t offset, ssize_t length, unsigned char* data, std::string &error /* out */) {
	if (!valid()) {
		error.assign("Invalid file");
		return false;
	}

	XZFileReaderState *state;
	if (nullptr == internalState) {
		internalState = state = new XZFileReaderState(m_file, m_index);
	} else {
		state = dynamic_cast<XZFileReaderState*>(internalState);
		assert(nullptr != state);
	}

	state->selectDefaultBuffer(); // always reset buffer, readInto might have left an old pointer
	if (!state->seekBlockFor(offset, error)) return false;

	ssize_t skipInBlock = offset - state->position + state->availableBytes();
	LOG_VERBOSE("have to skip %i bytes (negative: overlap)\n", (int) skipInBlock);

	if (skipInBlock > 0) {
		// read exactly skipInBlock bytes into our defaultOutputBuffer
		state->discard_output();

		for (; skipInBlock > 0; ) {
			if ((ssize_t) state->strm.avail_out > skipInBlock) {
				state->selectBuffer(state->defaultOutputBuffer, skipInBlock);
			}
			if (!state->decodeFillBuffer(error)) return false;
			skipInBlock -= state->availableBytes();
			state->discard_output();
		}

		// now the real output starts
		state->selectBuffer(data, length);
	} else {
		// copy the (possible empty) overlap we need
		ssize_t overlap = -skipInBlock;

		memmove(data, state->strm.next_in - overlap, overlap);
		state->selectBuffer(data + overlap, length - overlap);
	}

	if (!state->decodeFillBuffer(error)) return false;

	return true;
}

void XZFile::finish(FileReaderState* &internalState) {
	if (nullptr != internalState) {
		XZFileReaderState *state = dynamic_cast<XZFileReaderState*>(internalState);
		assert(nullptr != state);
		delete state;
		internalState = nullptr;
	}
}

/* adapted from official xz source: xz/src/xz/list.c */
static lzma_index* read_index(File file, uint64_t memlimit, std::string &error) {
	union {
		unsigned char buf[4096];
		unsigned char u8[LZMA_STREAM_HEADER_SIZE];
		uint32_t u32[LZMA_STREAM_HEADER_SIZE/4];
	} buf;

	lzma_stream strm =  LZMA_STREAM_INIT;
	lzma_index *cur_index = nullptr;
	lzma_index *col_index = nullptr;
	FileReaderState *filestate = nullptr;

	lzma_stream_flags header_flags;
	lzma_stream_flags footer_flags;
	lzma_ret ret;

	// Current position in the file. We parse the file backwards so
	// initialize it to point to the end of the file.
	int64_t pos = file->filesize();;

	// Each loop iteration decodes one Index.
	do {
		lzma_vli stream_padding, index_size;
		uint64_t memused;

		// Check that there is enough data left to contain at least
		// the Stream Header and Stream Footer. This check cannot
		// fail in the first pass of this loop.
		if (pos < 2 * LZMA_STREAM_HEADER_SIZE) {
			error.assign("file too small for xz archive");
			goto failed;
		}

		pos -= LZMA_STREAM_HEADER_SIZE;
		stream_padding = 0;

		// Locate the Stream Footer. There may be Stream Padding which
		// we must skip when reading backwards.
		for (;;) {
			if (pos < LZMA_STREAM_HEADER_SIZE) {
				error.assign("file too small for xz archive");
				goto failed;
			}

			if (!file->readInto(filestate, pos, LZMA_STREAM_HEADER_SIZE, buf.buf, error)) goto failed;

			/* padding must be a multiple of 4 */
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
			errnoLzmaToStr("invalid footer", ret, error);
			goto failed;
		}

		// Check that the size of the Index field looks sane.
		index_size = footer_flags.backward_size;
		if ((lzma_vli)(pos) < index_size + LZMA_STREAM_HEADER_SIZE) {
			error.assign("invalid index size");
			goto failed;
		}

		// Set pos to the beginning of the Index.
		pos -= index_size;

		// See how much memory we can use for decoding this Index.
		memused = nullptr != col_index ? lzma_index_memused(col_index) : 0;
		if (memused > memlimit) {
			error.assign("mem limit hit");
			goto failed;
		}

		// Decode the Index.
		ret = lzma_index_decoder(&strm, &cur_index, memlimit - memused);
		if (ret != LZMA_OK) {
			errnoLzmaToStr("couldn't allocate new index", ret, error);
			goto failed;
		}

		do {
			ssize_t want = (index_size < sizeof(buf.buf) ? index_size : sizeof(buf.buf));
			if (want < 0) { ret = LZMA_DATA_ERROR; break; }

			if (!file->readInto(filestate, pos, want, buf.buf, error)) goto failed;

			strm.avail_in = want;
			strm.next_in = buf.buf;
			pos += want;
			index_size -= want;

			ret = lzma_code(&strm, LZMA_RUN);
		} while (ret == LZMA_OK);

		// If the decoding seems to be successful, check also that
		// the Index decoder consumed as much input as indicated
		// by the Backward Size field.
		if (ret == LZMA_STREAM_END)
			if (index_size != 0 || strm.avail_in != 0)
				ret = LZMA_DATA_ERROR;

		if (ret != LZMA_STREAM_END) {
			// LZMA_BUFFER_ERROR means that the Index decoder
			// would have liked more input than what the Index
			// size should be according to Stream Footer.
			// The message for LZMA_DATA_ERROR makes more
			// sense in that case.
			if (ret == LZMA_BUF_ERROR)
				ret = LZMA_DATA_ERROR;

			errnoLzmaToStr("decoding index failed", ret, error);
			goto failed;
		}

		// Decode the Stream Header and check that its Stream Flags
		// match the Stream Footer.
		pos -= footer_flags.backward_size + LZMA_STREAM_HEADER_SIZE;
		if ((lzma_vli)(pos) < lzma_index_total_size(cur_index)) {
			error.assign("invalid archive - index large than available data");
			goto failed;
		}

		pos -= lzma_index_total_size(cur_index);
		if (!file->readInto(filestate, pos, LZMA_STREAM_HEADER_SIZE, buf.buf, error)) goto failed;

		ret = lzma_stream_header_decode(&header_flags, buf.u8);
		if (ret != LZMA_OK) {
			errnoLzmaToStr("invalid header", ret, error);
			goto failed;
		}

		ret = lzma_stream_flags_compare(&header_flags, &footer_flags);
		if (ret != LZMA_OK) {
			errnoLzmaToStr("invalid stream: footer doesn't match header", ret, error);
			goto failed;
		}

		// Store the decoded Stream Flags into this_index. This is
		// needed so that we can print which Check is used in each
		// Stream.
		ret = lzma_index_stream_flags(cur_index, &footer_flags);
		if (ret != LZMA_OK) {
			errnoLzmaToStr("decoding stream flags failed", ret, error);
			goto failed;
		}

		// Store also the size of the Stream Padding field. It is
		// needed to show the offsets of the Streams correctly.
		ret = lzma_index_stream_padding(cur_index, stream_padding);
		if (ret != LZMA_OK) {
			errnoLzmaToStr("storing stream padding failed", ret, error);
			goto failed;
		}

		if (nullptr != col_index) {
			// Append the earlier decoded Indexes
			// after this_index.
			ret = lzma_index_cat(cur_index, col_index, NULL);
			col_index = nullptr;
			if (ret != LZMA_OK) {
				errnoLzmaToStr("failed to concatenate indexes", ret, error);
				goto failed;
			}
		}
		col_index = cur_index;
		cur_index = nullptr;
	} while (pos > 0);

	lzma_end(&strm);
	file->finish(filestate);

	return col_index;

failed:
	lzma_end(&strm);
	if (nullptr != cur_index) lzma_index_end(cur_index, NULL);
	if (nullptr != col_index) lzma_index_end(col_index, NULL);
	file->finish(filestate);

	return nullptr;
}
