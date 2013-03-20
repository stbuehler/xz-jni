
#include "file.h"

#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <zlib.h>

static void dowrite(int fd, const unsigned char *data, ssize_t datalen) {
	while (datalen > 0) {
		ssize_t r = write(fd, data, datalen);
		if (r < 0) {
			std::cerr << "write failed: " << strerror(errno) << "\n";
			exit(1);
		}
		datalen -= r;
		data += r;
	}
}

static uint32_t store(int fd, const unsigned char *data, ssize_t datasize) {
	z_stream strm;
	memset(&strm, 0, sizeof(strm));

	unsigned char outbuf[4096];

	uint32_t complen = 0;

	strm.next_in = const_cast<unsigned char*>(data);
	strm.avail_in = datasize;

	deflateInit2(&strm, 7, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);
	for (;;) {
		strm.next_out = outbuf;
		strm.avail_out = sizeof(outbuf);
		int ret = deflate(&strm, Z_FINISH);
		int have = sizeof(outbuf) - strm.avail_out;
		complen += have;
		dowrite(fd, outbuf, have);

		if (ret == Z_STREAM_END) break;
		if (ret != Z_OK) {
			std::cerr << "deflate failed: " << ret << "\n";
			exit(1);
		}
	}
	deflateEnd(&strm);

	return complen;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::cerr << "syntax: " << argv[0] << " filename\n";
		exit(1);
	}

	std::string error;

	std::string inFilename = argv[1];
	std::shared_ptr<NormalFile> file(new MMappedFile(inFilename.c_str(), error));
	if (!file->valid()) {
		std::cerr << "couldn't open file: " << error << "\n";
		exit(1);
	}

	uint32_t blocksize = 64*1024;
	int64_t filesize = file->filesize();
	int64_t blocks = (filesize + blocksize - 1) / blocksize;

	std::string outFilename = inFilename + std::string(".idxdefl");

	int fd = open(outFilename.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);

	if (-1 == fd) {
		std::cerr << "couldn't create file: " << strerror(errno) << "\n";
		exit(1);
	}

	/* header: "idxdefl\0" */
	/* big endian footer: <index size> <block size> <full blocks> <last block size> */
	static const unsigned char magic_header[8] = "idxdefl";

	write(fd, magic_header, sizeof(magic_header));

	uint32_t *index = new uint32_t[blocks-1];
	uint32_t blockndx = 0;
	int64_t pos = 0;
	uint32_t lastBlocksize = 0;

	FileReader reader(file);

	int64_t compressedSize = 24;
	printf("Progress: %i, Ratio: %0.2f", (int) (100 * (int64_t)blockndx / blocks), 0.);

	while (pos < filesize) {
		const unsigned char *data;
		ssize_t datasize;
		if (!reader.read(std::min<int64_t>(blocksize, reader.length()), data, datasize)) {
			std::cerr << "failed to read data: " << reader.lastError() << "\n";
			exit(1);
		}
		// std::cerr << "read block with size: " << datasize << "\n";

		uint32_t complen = store(fd, data, datasize);
		compressedSize += complen;

		// std::cerr << "stored block with size: " << complen << "\n";

		pos += datasize;
		if (pos < filesize) {
			index[blockndx++] = htonl(complen);
		} else {
			lastBlocksize = datasize;
		}
		printf("\rProgress: %i, Ratio: %0.2f", (int) (100 * (int64_t)blockndx / blocks), compressedSize / (double) pos);
	}
	printf("\n");

	uint32_t indexsize = store(fd, (const unsigned char*) index, 4 * (blocks - 1));

	uint32_t footer[4];
	footer[0] = htonl(indexsize);
	footer[1] = htonl(blocksize);
	footer[2] = htonl(blocks - 1);
	footer[3] = htonl(lastBlocksize);

	dowrite(fd, (const unsigned char*) footer, sizeof(footer));
	close(fd);

	return 0;
}