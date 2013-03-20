
#include "idx-defl-file.h"

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

int main(int argc, char **argv) {
	if (argc < 2) {
		std::cerr << "syntax: " << argv[0] << " filename\n";
		exit(1);
	}

	std::string error;

	std::string inFilename = argv[1];
	std::shared_ptr<NormalFile> plainfile(new MMappedFile(inFilename.c_str(), error));
	if (!plainfile->valid()) {
		std::cerr << "couldn't open file: " << error << "\n";
		exit(1);
	}
	std::shared_ptr<IndexedDeflateFile> file(new IndexedDeflateFile(plainfile, error));
	if (!file->valid()) {
		std::cerr << "couldn't open archive: " << error << "\n";
		exit(1);
	}

	std::cerr << "Filesize: " << file->filesize() << "\n";

	FileReader reader(file);
	while (reader.length() > 0) {
		int want = std::min<int64_t>(4096, reader.length());
		const unsigned char *data;
		ssize_t datasize;
		if (!reader.read(want, data, datasize)) {
			std::cerr << "read failed: " << reader.lastError() << "\n";
			exit(1);
		}
		dowrite(1, data, datasize);
	}

	return 0;
}