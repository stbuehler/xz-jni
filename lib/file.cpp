#include "file.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void errnoToSt(const char *prefix, std::string &error) {
	error.assign(prefix);
	error.append(strerror(errno));
}

static void errnoFnameToSt(const char *prefix, const char *filename, std::string &error) {
	error.assign(prefix);
	error.push_back(' ');
	error.append(filename);
	error.push_back(':');
	error.append(strerror(errno));
}


/********************************************************************************
 *                                                                              *
 *                                 NormalFile                                   *
 *                                                                              *
 ********************************************************************************/


class NormalFileReaderState : public FileReaderState {
public:
	NormalFileReaderState() { }
	unsigned char buf[4096];
};

NormalFile::NormalFile(const char *filename, std::string &error /* out */)
: m_fd(-1), m_filesize(0) {
	m_fd = open(filename, O_RDONLY);
	if (-1 == m_fd) {
		errnoFnameToSt("Couldn't open file", filename, error);
		return;
	}

	struct stat st;
	if (-1 == fstat(m_fd, &st)) {
		errnoFnameToSt("Couldn't stat file", filename, error);
		close(m_fd);
		m_fd = -1;
		return;
	}

	m_filesize = st.st_size;
}

NormalFile::~NormalFile() {
	if (-1 != m_fd) {
		close(m_fd);
		m_fd = -1;
	}
	m_filesize = 0;
}

bool NormalFile::valid() {
	return -1 != m_fd;
}

int64_t NormalFile::filesize() {
	return m_filesize;
}

bool NormalFile::read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */) {
	NormalFileReaderState *state;
	if (nullptr == internalState) {
		internalState = state = new NormalFileReaderState();
	} else {
		state = dynamic_cast<NormalFileReaderState*>(internalState);
		assert(nullptr != state);
	}

	if (length > (ssize_t) sizeof(state->buf)) length = sizeof(state->buf);

	if (!readInto(internalState, offset, length, state->buf, error)) return false;

	data = state->buf;
	datasize = length;

	return true;
}

bool NormalFile::readInto(FileReaderState* &internalState, int64_t offset, ssize_t length, unsigned char* data, std::string &error /* out */) {
	if (-1 == m_fd) {
		error.assign("File not opened");
		return false;
	}

	if (length < 0 || offset < 0 || length > m_filesize || offset > m_filesize - length) {
		error.assign("Invalid offset/length");
		return false;
	}

	ssize_t r = pread(m_fd, data, length, offset);
	if (r != length) {
		if (r < 0) {
			errnoToSt("Couldn't read file:", error);
		} else {
			error.assign("Couldn't read file: didn't get enough data");
		}
		return false;
	}

	return true;
}


void NormalFile::finish(FileReaderState* &internalState) {
	if (nullptr != internalState) {
		NormalFileReaderState *state = dynamic_cast<NormalFileReaderState*>(internalState);
		assert(nullptr != state);
		delete state;
		internalState = nullptr;
	}
}

/********************************************************************************
 *                                                                              *
 *                                MMappedFile                                   *
 *                                                                              *
 ********************************************************************************/

class MMappedFileReaderState : public FileReaderState {
public:
	MMappedFileReaderState() : addr(MAP_FAILED), length(0) { }
	void *addr;
	size_t length;
};

MMappedFile::MMappedFile(const char *filename, std::string &error /* out */)
: NormalFile(filename, error) {
	m_pagesize = sysconf(_SC_PAGE_SIZE);
}

bool MMappedFile::read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */) {
	if (-1 == m_fd) {
		error.assign("File not opened");
		return false;
	}

	if (length < 0 || offset < 0 || length > m_filesize || offset > m_filesize - length) {
		error.assign("Invalid offset/length");
		return false;
	}

	MMappedFileReaderState *state;
	if (nullptr == internalState) {
		internalState = state = new MMappedFileReaderState();
	} else {
		state = dynamic_cast<MMappedFileReaderState*>(internalState);
		assert(nullptr != state);
		if (MAP_FAILED != state->addr) {
			munmap(state->addr, state->length);
			state->addr = MAP_FAILED;
		}
	}

	int pagedistance = offset % m_pagesize;

	state->length = length + pagedistance;
	state->addr = mmap(NULL, state->length, PROT_READ, MAP_SHARED, m_fd, offset - pagedistance);

	if (MAP_FAILED == state->addr) {
		errnoToSt("Couldn't mmap file", error);
		return false;
	}

	data = ((const unsigned char *) state->addr) + pagedistance;
	datasize = length;

	return true;
}

void MMappedFile::finish(FileReaderState* &internalState) {
	if (nullptr != internalState) {
		MMappedFileReaderState *state = dynamic_cast<MMappedFileReaderState*>(internalState);
		assert(nullptr != state);
		if (MAP_FAILED != state->addr) {
			munmap(state->addr, state->length); /* ignore errors */
			state->addr = MAP_FAILED;
		}
		delete state;
		internalState = nullptr;
	}
}

