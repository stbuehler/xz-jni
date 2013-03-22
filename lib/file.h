#ifndef __MY_FILE_H
#define __MY_FILE_H __MY_FILE_H

#include <cassert>
#include <cstdint>

#include <algorithm>
#include <memory>
#include <string>

/**
 * each IFile implementation can allocate a state object;
 * a state is supposed to help with sequential access,
 * and also is used to hold the returned buffer
 * each state must be "finish"ed with the file it was used,
 * and cannot be reused with other files (even from the same class)
 */
class FileReaderState {
protected:
	virtual ~FileReaderState() { }
	FileReaderState() { }
	FileReaderState(const FileReaderState &) { }
	FileReaderState& operator=(const FileReaderState &);
};

/**
 * random access file abstraction
 */
class IFile {
protected:
	IFile() { }
	IFile(const IFile &) { }
	IFile& operator=(const IFile &);

public:
	virtual ~IFile() { };

	/* these should be thread safe (assuming each thread has its own state) */
	virtual int64_t filesize() = 0;
	/**
	 * read up to length bytes from given file offset, storing a pointer to the data in data
	 * and storing the amount of actuall read bytes in datasize
	 *
	 * returns true on successful read and false otherwise; on error an error message is stored in error.
	 * the requested range offset/length should be valid for the file; the method may still succeed by returning less data,
	 * but it also might return an error
	 *
	 * in any case you have to release the internalState later with finish(); but you can reuse the same state with
	 * the same file in other reads before finish()ing it.
	 * initialize the internalState pointer to nullptr before the first call!
	 */
	virtual bool read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */) = 0;
	/**
	 * same as read, but reads into the already allocated storage at data (which has to be large enough for length bytes)
	 * also reads exactly length bytes - or throws an error.
	 */
	virtual bool readInto(FileReaderState* &internalState, int64_t offset, ssize_t length, unsigned char* data, std::string &error /* out */) = 0;
	/**
	 * free the internalState. does nothing if internalState is nullptr, and resets internalState to nullptr.
	 */
	virtual void finish(FileReaderState* &internalState) = 0;
};

typedef std::shared_ptr<IFile> File;

/**
 * helper for the FileReaderState/IFile handling
 *
 * provides sequential access to a part (offset+length) of the file,
 * which can be reset to another part at any time (defaults to the complete file).
 *
 * keeps the file open with a reference, and ensures the state is finished
 * correctly.
 *
 * not thread safe - use different FileReaders in different threads (same file is ok).
 */
class FileReader {
private:
	File m_file;
	FileReaderState *m_state;
	std::string m_lastError;

	int64_t m_offset, m_length; /* next read offset, remaining length */

private:
	void fixLength() {
		int64_t fsize = m_file ? m_file->filesize() : 0;
		if (m_offset > fsize) {
			m_length = 0;
		} else {
			fsize -= m_offset;
			if (m_length > fsize || -1 == m_length) m_length = fsize;
		}
	}

public:
	~FileReader() {
		close();
	}

	FileReader()
	: m_file(nullptr), m_state(nullptr), m_offset(0), m_length(0) {
	}

	FileReader(File file) : FileReader(file, 0, -1) { }
	FileReader(File file, int64_t offset) : FileReader(file, offset, -1) { }

	FileReader(File file, int64_t offset, int64_t length)
	: m_file(file), m_state(nullptr), m_offset(offset), m_length(length) {
		fixLength();
	}

	FileReader(const FileReader &other)
	: FileReader(other.m_file, other.m_offset, other.m_length) { }

	FileReader& operator=(const FileReader &other) {
		release();
		m_lastError.clear();
		m_file = other.m_file;
		m_offset = other.m_offset;
		m_length = other.m_length;
		return *this;
	}

	/* release buffers, release file reference */
	void close() {
		if (nullptr != m_state) m_file->finish(m_state);
		m_state = nullptr;
		m_file.reset();
	}

	/* releases temporary buffers, does not close file */
	void release() {
		if (nullptr != m_state) m_file->finish(m_state);
		m_state = nullptr;
	}

	/* reset selected range to [offset, end of file] */
	void seek(int64_t offset) {
		seek(offset, -1);
	}

	void seek(int64_t offset, int64_t length) {
		release();
		m_offset = offset;
		m_length = length;
		fixLength();
	}

	int64_t offset() const { return m_offset; } /* next read offset */
	int64_t length() const { return m_length; } /* remaining length */
	File file() const { return m_file; }

	std::string lastError() { return m_lastError; }

	/**
	 * read up to maxBufSize bytes. eof() is signaled by returning zero datasize
	 */
	bool read(ssize_t maxBufSize, const unsigned char* &data /* out */, ssize_t &datasize /* out */) {
		assert(maxBufSize > 0);

		if (nullptr == m_file.get()) {
			m_lastError.assign("File not opened");
			return false;
		}

		ssize_t want = (ssize_t) std::min((int64_t) maxBufSize, m_length);

		if (0 == want) {
			data = nullptr;
			datasize = 0;
			return true; /* end of file is not an error */
		}

		if (!m_file->read(m_state, m_offset, want, data, datasize, m_lastError)) return false;

		if (datasize > want) datasize = want;
		m_offset += datasize;
		m_length -= datasize;
		return true;
	}

	/**
	 * read exactly datasize bytes into data
	 */
	bool readInto(unsigned char* data, ssize_t datasize) {
		assert(datasize >= 0);

		if (nullptr == m_file.get()) {
			m_lastError.assign("File not opened");
			return false;
		}

		if (0 == datasize) return true;

		if (!m_file->readInto(m_state, m_offset, datasize, data, m_lastError)) return false;

		m_offset += datasize;
		m_length -= datasize;
		return true;
	}
};


/* OS provided file, using pread() */
class NormalFile : public IFile {
private:
	NormalFile();
	NormalFile(const IFile &);
	NormalFile& operator=(const NormalFile &);

protected:
	int m_fd;
	int64_t m_filesize;

public:
	NormalFile(const char *filename, std::string &error /* out */);
	virtual ~NormalFile();

	bool valid(); /** whether open() in the constructor succeeded */

	virtual int64_t filesize();
	virtual bool read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */);
	/** special case: does not use the state */
	virtual bool readInto(FileReaderState* &internalState, int64_t offset, ssize_t length, unsigned char* data, std::string &error /* out */);
	virtual void finish(FileReaderState* &internalState);
};

/* uses mmap() instead of pread() */
class MMappedFile : public NormalFile {
private:
	MMappedFile();
	MMappedFile(const IFile &);
	MMappedFile& operator=(const MMappedFile &);

protected:
	int m_pagesize; /** cache sysconf(_SC_PAGE_SIZE) */

public:
	MMappedFile(const char *filename, std::string &error /* out */);

	/** always mmap()s the complete requested range */
	virtual bool read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */);
	virtual void finish(FileReaderState* &internalState);
	/** readInto with mmap() doesn't make sense, so just use NormalFile::readInto; NormalFile::readInto does not use state, so the mmap state doesn't conflict with it */
};

#endif
