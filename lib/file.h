#ifndef __MY_FILE_H
#define __MY_FILE_H __MY_FILE_H

#define _FILE_OFFSET_BITS 64

#include <cassert>
#include <cstdint>

#include <algorithm>
#include <memory>
#include <string>

class FileReaderState {
protected:
	virtual ~FileReaderState() { }
	FileReaderState() { }
	FileReaderState(const FileReaderState &) { }
	FileReaderState& operator=(const FileReaderState &);
};

class IFile {
protected:
	IFile() { }
	IFile(const IFile &) { }
	IFile& operator=(const IFile &);

public:
	virtual ~IFile() { };

	/* these should be thread safe (assuming each thread has its own state) */
	virtual int64_t filesize() = 0;
	virtual bool read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */) = 0;
	virtual bool readInto(FileReaderState* &internalState, int64_t offset, ssize_t length, unsigned char* data, std::string &error /* out */) = 0;
	virtual void finish(FileReaderState* &internalState) = 0;
};

typedef std::shared_ptr<IFile> File;

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

	bool valid();

	virtual int64_t filesize();
	virtual bool read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */);
	virtual bool readInto(FileReaderState* &internalState, int64_t offset, ssize_t length, unsigned char* data, std::string &error /* out */);
	virtual void finish(FileReaderState* &internalState);
};

class MMappedFile : public NormalFile {
private:
	MMappedFile();
	MMappedFile(const IFile &);
	MMappedFile& operator=(const MMappedFile &);

protected:
	int m_pagesize;

public:
	MMappedFile(const char *filename, std::string &error /* out */);

	virtual bool read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */);
	virtual void finish(FileReaderState* &internalState);
};

#endif
