#ifndef __MY_IDX_DEFL_FILE_H
#define __MY_IDX_DEFL_FILE_H __MY_IDX_DEFL_FILE_H

#include "file.h"

extern "C" {
#include <zlib.h>
}

class IndexedDeflateFileIndex;

/** abstraction for custom file compression format. see doc/indexed-deflate-format.txt */
class IndexedDeflateFile : public IFile {
private:
	IndexedDeflateFile();
	IndexedDeflateFile(const IFile &);
	IndexedDeflateFile& operator=(const IndexedDeflateFile &);

protected:
	File m_file;
	IndexedDeflateFileIndex *m_index;

public:
	IndexedDeflateFile(File file, std::string &error /* out */);
	virtual ~IndexedDeflateFile();

	bool valid();

	virtual int64_t filesize();
	virtual bool read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */);
	virtual bool readInto(FileReaderState* &internalState, int64_t offset, ssize_t length, unsigned char* data, std::string &error /* out */);
	virtual void finish(FileReaderState* &internalState);
};

#endif
