package de.unistuttgart.informatik.OfflineToureNPlaner.xz;

import java.io.IOException;

public class XZInputStream {
	private long nativePtr;

	private long m_length; // uncompressed length

	private native void openFile(String filename) throws IOException;
	private native void closeFile() throws IOException;

	public native void readInt(long offset, int[] buffer, int start, int length) throws IOException;

	public XZInputStream(String filename) throws IOException {
		openFile(filename);
	}

	protected void finalize() throws Throwable {
		try {
			closeFile();
		} finally {
			super.finalize();
		}
	}

	public long length() {
		return m_length;
	}

	static {
		System.loadLibrary("xz-jni");
	}
}
