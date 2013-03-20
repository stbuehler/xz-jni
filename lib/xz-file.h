#ifndef __MY_XZ_FILE_H
#define __MY_XZ_FILE_H __MY_XZ_FILE_H

#include "file.h"

extern "C" {
#include <lzma.h>
}

class XZFile : public IFile {
private:
	XZFile();
	XZFile(const IFile &);
	XZFile& operator=(const XZFile &);

protected:
	File m_file;
	lzma_index *m_index;

public:
	XZFile(File file, std::string &error /* out */);
	virtual ~XZFile();

	bool valid();

	virtual int64_t filesize();
	virtual bool read(FileReaderState* &internalState, int64_t offset, ssize_t length, const unsigned char* &data /* out */, ssize_t &datasize /* out */, std::string &error /* out */);
	virtual bool readInto(FileReaderState* &internalState, int64_t offset, ssize_t length, unsigned char* data, std::string &error /* out */);
	virtual void finish(FileReaderState* &internalState);
};

#endif
