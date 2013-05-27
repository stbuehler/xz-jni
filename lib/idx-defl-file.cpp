#include "idx-defl-file.h"

#include <sstream>

#include <arpa/inet.h>
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

static void errnoZToStr(const char *prefix, int res, std::string &error) {
	std::ostringstream s;
	s << prefix << ": ";
	switch (res) {
	case Z_OK:
		s << "Operation completed successfully";
		break;
	case Z_STREAM_END:
		s << "End of stream was reached";
		break;
	case Z_NEED_DICT:
		s << "Need dictionary";
		break;
	case Z_ERRNO:
		s << "System error: ";
		s << strerror(errno);
		break;
	case Z_STREAM_ERROR:
		s << "Stream error";
		break;
	case Z_DATA_ERROR:
		s << "Data is corrupt";
		break;
	case Z_MEM_ERROR:
		s << "Cannot allocate memory";
		break;
	case Z_BUF_ERROR:
		s << "No progress is possible";
		break;
	case Z_VERSION_ERROR:
		s << "Wrong version";
		break;
	default:
		s << "Unknown error (" << ((int) res) << ")";
		break;
	}
	error.assign(s.str());
}

class IndexedDeflateFileIndex;
class IndexedDeflateFileIndexIter;

class IndexedDeflateFileIndex {
public:
	uint32_t block_size, blocks;
	int64_t uncompressed_size, compressed_size;
	int64_t *offsets;

	IndexedDeflateFileIndex(uint32_t block_size, uint32_t blocks, int64_t uncompressed_size, int64_t compressed_size, int64_t *offsets)
	: block_size(block_size), blocks(blocks), uncompressed_size(uncompressed_size), compressed_size(compressed_size), offsets(offsets) {
	}

	~IndexedDeflateFileIndex() {
		if (nullptr != offsets) {
			delete[] offsets;
			offsets = nullptr;
		}
	}
};

class IndexedDeflateFileIndexIter {
private:
	IndexedDeflateFileIndex *m_index;
public:
	IndexedDeflateFileIndexIter(IndexedDeflateFileIndex *index) : m_index(index) { }

	bool seek(int64_t offset) {
		if (offset < 0 || offset > m_index->uncompressed_size) return false;
		int64_t block = offset / m_index->block_size;
		LOG_VERBOSE("calculated block %i (%i)\n", (int) block, (int) m_index->blocks);
		if (block >= m_index->blocks) return false; // shouldn't happen anyway...
		compressed_offset = m_index->offsets[block];
		compressed_length = m_index->offsets[block+1] - compressed_offset;
		uncompressed_offset = block * m_index->block_size;
		if (block + 1 == m_index->blocks) {
			uncompressed_length = m_index->uncompressed_size - (m_index->blocks - 1) * (int64_t) m_index->block_size;
		} else {
			uncompressed_length = m_index->block_size;
		}
		LOG_VERBOSE("seeked offset: %i, coff: %i, clen: %i, uoff: %i, ulen: %i\n",
			(int) offset, (int) compressed_offset, (int) compressed_length, (int) uncompressed_offset, (int) uncompressed_length);
		return true;
	}
	bool next() {
		return seek(uncompressed_offset + uncompressed_length);
	}

	int64_t compressed_offset, compressed_length;
	int64_t uncompressed_offset, uncompressed_length;
};

class IndexedDeflateFileReaderState : public FileReaderState {
public:
	z_stream strm;

	int64_t position; /* uncompressed offset of outputBuffer[0] (NOT strm->next_out!) */
	IndexedDeflateFileIndexIter iter;

	unsigned char *currentBuffer;
	size_t currentBufferSize;

	unsigned char defaultOutputBuffer[4096];

	FileReader reader;

	IndexedDeflateFileReaderState(File file, IndexedDeflateFileIndex *index)
	: iter(index), currentBuffer(nullptr), currentBufferSize(0), reader(file) {
		memset(&strm, 0, sizeof(strm));

		position = -1;
		selectDefaultBuffer();

		// lzma_index_iter_init(&iter, index);
	}

	void selectDefaultBuffer() {
		LOG_VERBOSE("selectDefaultBuffer\n");
		if (defaultOutputBuffer != currentBuffer) selectBuffer(defaultOutputBuffer, sizeof(defaultOutputBuffer));
	}

	void selectBuffer(unsigned char *buf, size_t size) {
		LOG_VERBOSE("selectBuffer, %i\n", (int) size);
		if (position >= 0) discard_output();
		currentBuffer = buf;
		currentBufferSize = size;
		strm.next_out = currentBuffer;
		strm.avail_out = currentBufferSize;
	}

	~IndexedDeflateFileReaderState() {
		LOG_VERBOSE("~IndexedDeflateFileReaderState\n");
		inflateEnd(&strm);
	}

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
			strm.next_in = const_cast<unsigned char*>(data);
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
		position = -1;

		//LOG_VERBOSE("seeking to offset %i", (int) iter.compressed_file_offset);
		reader.seek(iter.compressed_offset, iter.compressed_length);
		strm.avail_in = 0; /* make sure we read new data after lseek */

		if (!fill_input_buffer(error)) return false;
		if (0 == strm.avail_in) {
			error.assign("Unexpected end of file while trying to read block header");
			return false;
		}

		inflateEnd(&strm);
		int ret = inflateInit2(&strm, 0);
		if (Z_OK != ret) {
			errnoZToStr("couldn't initialize block decoder", ret, error);
			return false;
		}

		position = iter.uncompressed_offset;
		return true;
	}

	bool seekBlockFor(int64_t offset, std::string &error) {
		bool matchingBlock =
			(position >= 0
			&& offset >= iter.uncompressed_offset
			&& offset < iter.uncompressed_offset + iter.uncompressed_length);

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

				if (!iter.seek(offset)) {
					error.assign("couldn't find offset in index");
					return false;
				}
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

			int ret = inflate(&strm, Z_SYNC_FLUSH);
			if (Z_OK != ret && Z_STREAM_END != ret) {
				errnoZToStr("failed decoding data", ret, error);
				return false;
			}

			if (Z_STREAM_END == ret && pos == strm.next_out) {
				/* end of stream AND we didn't get new data this round */
				if (!iter.next()) {
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

			int ret = inflate(&strm, Z_SYNC_FLUSH);
			if (Z_OK != ret && Z_STREAM_END != ret) {
				errnoZToStr("failed decoding data", ret, error);
				return false;
			}

			if (0 == strm.avail_out) return true; // done filling buffer

			if (Z_STREAM_END == ret) {
				if (!iter.next()) {
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

static IndexedDeflateFileIndex* read_index(File file, ssize_t memlimit, std::string &error);

IndexedDeflateFile::IndexedDeflateFile(File file, std::string &error /* out */)
: m_file(file), m_index(nullptr) {
	m_index = read_index(file, 16*1024*1024, error);
}

IndexedDeflateFile::~IndexedDeflateFile() {
	if (nullptr != m_index) {
		delete m_index;
		m_index = nullptr;
	}
}

bool IndexedDeflateFile::valid() {
	return (nullptr != m_index) && m_file;
}

int64_t IndexedDeflateFile::filesize() {
	return (nullptr != m_index) ? m_index->uncompressed_size : 0;
}

bool IndexedDeflateFile::read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */) {
	if (!valid()) {
		error.assign("Invalid file");
		return false;
	}

	IndexedDeflateFileReaderState *state;
	if (nullptr == internalState) {
		internalState = state = new IndexedDeflateFileReaderState(m_file, m_index);
	} else {
		state = dynamic_cast<IndexedDeflateFileReaderState*>(internalState);
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

bool IndexedDeflateFile::readInto(FileReaderState* &internalState, int64_t offset, ssize_t length, unsigned char* data, std::string &error /* out */) {
	if (!valid()) {
		error.assign("Invalid file");
		return false;
	}

	IndexedDeflateFileReaderState *state;
	if (nullptr == internalState) {
		internalState = state = new IndexedDeflateFileReaderState(m_file, m_index);
	} else {
		state = dynamic_cast<IndexedDeflateFileReaderState*>(internalState);
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

void IndexedDeflateFile::finish(FileReaderState* &internalState) {
	if (nullptr != internalState) {
		IndexedDeflateFileReaderState *state = dynamic_cast<IndexedDeflateFileReaderState*>(internalState);
		assert(nullptr != state);
		delete state;
		internalState = nullptr;
	}
}


static IndexedDeflateFileIndex* read_index(File file, ssize_t memlimit, std::string &error) {
	/* header: "idxdefl\0" */
	/* big endian footer: <index size> <block size> <full blocks> <last block size> */
	static  const unsigned char magic_header[8] = "idxdefl";

	unsigned char header[sizeof(magic_header)];
	uint32_t footer[4];

	int32_t index_size;
	int32_t block_size;
	int32_t full_blocks;
	int32_t last_block;

	int64_t uncompressed_size, index_offset;
	int64_t *compressed_offsets = nullptr;
	int32_t idx;
	int64_t current;

	FileReaderState *filestate = nullptr;

	unsigned char buf[4096];
	uint32_t intbuf[64];

	z_stream strm;

	int64_t filesize = file->filesize();

	// Current position in the file. We parse the file backwards so
	// initialize it to point to the end of the file.
	int64_t pos = filesize;

	if (pos < (int64_t) (sizeof(magic_header) + sizeof(footer))) {
		error.assign("invalid file (too small for header+footer)");
		goto failed;
	}

	if (!file->readInto(filestate, 0, sizeof(magic_header), header, error)) goto failed;
	if (0 != memcmp(magic_header, header, sizeof(magic_header))) {
		error.assign("invalid file header");
		goto failed;
	}

	pos -= sizeof(footer);
	if (!file->readInto(filestate, pos, sizeof(footer), (unsigned char*) footer, error)) goto failed;

	for (unsigned int i = 0; i < sizeof(footer)/sizeof(footer[0]); ++i) {
		footer[i] = htonl(footer[i]);
		if (footer[i] > (uint32_t) std::numeric_limits<int32_t>::max() - 16) {
			error.assign("too large number in footer");
			goto failed;
		}
	}

	index_size = footer[0];
	block_size = footer[1];
	full_blocks = footer[2];
	last_block = footer[3];

	if (pos - 8 < (int64_t) index_size) {
		error.assign("invalid index size");
		goto failed;
	}

	if (0 == block_size) {
		error.assign("invalid block size");
		goto failed;
	}

	if (full_blocks > memlimit / 8 - 256) {
		error.assign("too many blocks");
		goto failed;
	}

	if (full_blocks > 0 && block_size > std::numeric_limits<int64_t>::max() / full_blocks) {
		error.assign("invalid block size / count combination");
		goto failed;
	}

	uncompressed_size = full_blocks * block_size;
	if (last_block > std::numeric_limits<int64_t>::max() - uncompressed_size) {
		error.assign("invalid block size / count combination");
		goto failed;
	}
	uncompressed_size += last_block;

	compressed_offsets = new int64_t[full_blocks+2];

	pos -= index_size;
	index_offset = pos;

	memset(&strm, 0, sizeof(strm));

	inflateInit2(&strm, 0);

	idx = 0;
	current = 8;
	compressed_offsets[idx++] = current;
	while (strm.avail_in > 0 || index_size > 0) {
		strm.next_out = (unsigned char*) intbuf;
		strm.avail_out = sizeof(intbuf);

		while ((strm.avail_in > 0 || index_size > 0) && strm.avail_out > 0) {
			if (0 == strm.avail_in) {
				ssize_t want = std::min<size_t>(index_size, sizeof(buf));
				if (!file->readInto(filestate, pos, want, buf, error)) goto failed;

				strm.next_in = buf;
				strm.avail_in = want;
				index_size -= want;
				pos += want;
			}

			LOG_VERBOSE("inflating index: -%i, avail_in: %i, avail_out: %i\n", index_size, (int) strm.avail_in, (int) strm.avail_out);
			int ret = inflate(&strm, Z_SYNC_FLUSH);
			if ((Z_OK != ret && Z_STREAM_END != ret) ||
				(Z_STREAM_END == ret && (0 < strm.avail_in || 0 < index_size))) {
				errnoZToStr("failed decoding index data", ret, error);
				goto failed;
			}
		}

		int havebytes = sizeof(intbuf) - strm.avail_out;
		if (0 != havebytes % 4) {
			error.assign("invalid index decompressed size");
			goto failed;
		}

		LOG_VERBOSE("read %i bytes from index\n", havebytes);
		for (int i = 0; i < havebytes / 4; ++i) {
			if (idx > full_blocks) {
				error.assign("decompressed index too large");
				goto failed;
			}
			LOG_VERBOSE("found block %i (%i) with len %u starting at %i\n", idx, (int) (full_blocks+1), (unsigned int) htonl(intbuf[i]), (int) current);
			current += htonl(intbuf[i]);
			compressed_offsets[idx++] = current;
		}
	}

	if (idx != full_blocks + 1) {
		LOG_VERBOSE("missing %i index entries\n", full_blocks + 1 - idx);
		error.assign("decompressed index too small");
		goto failed;
	}
	if (current > index_offset) {
		error.assign("decompressed data reaches into index");
		goto failed;
	}

	compressed_offsets[idx] = index_offset;

	for (int i = 0; i < full_blocks+2;++i) {
		LOG_VERBOSE("compressed_offsets[%i]: %i\n", i, (int) compressed_offsets[i]);
	}

	inflateEnd(&strm);

	file->finish(filestate);

	return new IndexedDeflateFileIndex(block_size, full_blocks + 1, uncompressed_size, filesize, compressed_offsets);

failed:
	inflateEnd(&strm);
	if (nullptr != compressed_offsets) delete[] compressed_offsets;
	file->finish(filestate);

	return nullptr;
}
