#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/Exception.hpp"

#include <iostream>
#include <memory>
#include <algorithm>
#include <stdexcept>

// #include <ext/stdio_filebuf.h>
#include <string.h>
#include <unistd.h>

namespace hmbdc { namespace os {
/**
 * @brief execute a program as a child and capture its stdin stdout and/or stderr
 * @details IO are exposed in the fd form
 */
struct ExecutionIo {
	/**
	 * @brief execute a program as a child and capture its stdin stdout and/or stderr
	 * 
	 * @param argv ullptr terminated command line args array, 
	 * for example, {"/usr/bin/bc", "-q", nullptr}
	 * @param captureStderr optionally capture
	 */
	ExecutionIo(const char* const argv[]
		, bool captureStderr = false);

	~ExecutionIo();

	/**
	 * @brief child stdin fd, use write on it - not read
	 * @details blocking by default
	 */
	int exeStdinFd;

	/**
	 * @brief child stdout fd, use read on it - not write
	 * @details blocking by default
	 */
	int exeStdoutFd;

    /**
     * @brief if captured, child stderr fd, use read on it - not write
     * @details blocking by default
     */
	int exeStderrFd;

	/**
     * @brief process id of the started child process
     */
	pid_t exePid;


	bool const writeStatus = true;
	bool const readStatus = true;
	bool const errStatus = true;
	
	// ostream exeCin;
	// istream exeCout;
	// istream exeCerr;

	ExecutionIo& operator << (char const* input) {
		auto s = write(exeStdinFd, input, strlen(input));
		if ((size_t)s != strlen(input)) {
			const_cast<bool&>(writeStatus) = false;
		}
		return *this;
	}

	ExecutionIo& operator << (std::string const& input) {
		auto s = write(exeStdinFd, input.c_str(), input.size());
		if ((size_t)s != input.size()) {
			const_cast<bool&>(writeStatus) = false;
		}
		return *this;
	}

	ExecutionIo& operator >> (std::string& output) {
		return read(exeStdoutFd, output, buf_, sizeof(buf_), bufLen_, readStatus);
	}

	ExecutionIo& readErr(std::string& output) {
		return read(exeStderrFd, output, errbuf_, sizeof(errbuf_), errbufLen_, errStatus);

		// char buf[1024];
		// int s = read(exeStderrFd, buf, sizeof(buf));
		// output = std::string(buf, s);
		// return *this;
	}

private:
	ExecutionIo& read(int fd, std::string& output, char* buf, size_t bufCap, size_t& bufLen, const bool& status) {
		for (;;) {
			auto l = std::find(buf, buf + bufLen, '\n');
			//is no \n in 4096 char buffer, just treat the last char as \n
			if (l == buf + bufCap) l--; 
			if (l != buf + bufLen) {
			    output = std::string(buf, l + 1);
		        bufLen -= output.size();
		        memmove(buf, l + 1, bufLen);
			    return *this;
			}
			auto s = ::read(fd, buf + bufLen, bufCap - bufLen);
			if (s > 0) {
				bufLen += s;
			} else {
			    output = std::string(buf, bufLen);
			    bufLen = 0;
			    const_cast<bool&>(status) = false;
			    return *this;
			}
		}
	}
	char buf_[4096];
	size_t bufLen_ = 0;

	char errbuf_[4096];
	size_t errbufLen_ = 0;


// private:
// 	unique_ptr<__gnu_cxx::stdio_filebuf<char>> exeCinBuf_;
// 	unique_ptr<__gnu_cxx::stdio_filebuf<char>> exeCoutBuf_;
// 	unique_ptr<__gnu_cxx::stdio_filebuf<char>> exeCerrBuf_;
};
}}
