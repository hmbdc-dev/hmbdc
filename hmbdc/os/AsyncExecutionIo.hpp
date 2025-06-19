
#pragma once

#include "hmbdc/time/Time.hpp"
#include "hmbdc/Exception.hpp"

#include <boost/asio.hpp>
#include <boost/process.hpp>

#include <utility>
#include <string>
#include <vector>
#include <stdexcept>
#include <functional>

#include <signal.h>

namespace hmbdc {
namespace os {

/**
 * @brief start a child process with cmdline and manage its stdin stdout and stderr
 * This class run in async and callback mechanism. It owns no threads, the user is expected
 * to call runOneFor() to power the callback invokations
 * 
 */
class AsyncExecutionIo final {
public:
    /**
     * @brief exception for failed to launch the process
     * 
     */
    struct LaunchError : std::runtime_error { using std::runtime_error::runtime_error; };

    /**
     * @brief exception for stdout reading error - could mean the process terminated or not started
     * 
     */
    struct StdoutIoError : std::runtime_error { using std::runtime_error::runtime_error; };

    /**
     * @brief exception for stderr reading error - could mean the process terminated or not started
     * 
     */
    struct StderrIoError : std::runtime_error { using std::runtime_error::runtime_error; };

    AsyncExecutionIo(AsyncExecutionIo const&) = delete;
    AsyncExecutionIo& operator = (AsyncExecutionIo const&) = delete;

    /**
     * @brief callback signiature when one line (line breaker dependent) becomes available
     * 
     */
    using LineAvailableCb = std::function<void (std::string const& line)>;

    /**
     * @brief Launch the cmd with args as a child process
     * user need to catch exception of LaunchError
     * 
     * @param cmd - command
     * @param cmd_args - args
     * @param stdoutCb - callback when a line is available in stdout
     * @param stderrCb - callback when a line is available in stderr
     * @param stdout_line_delimiter - a line breaker char for stdout
     * @param stderr_line_delimiter - a line breaker char for stderr
     */
    AsyncExecutionIo(std::string const& cmd
        , std::vector<std::string> cmd_args
        , LineAvailableCb stdoutCb
        , LineAvailableCb stderrCb
        , char stdout_line_delimiter = '\n'
        , char stderr_line_delimiter = '\n')
    try 
    : process_(cmd, boost::process::args(cmd_args)
        , boost::process::std_in < stdin_stream_
        , boost::process::std_out > pipe_out_
        , boost::process::std_err > pipe_err_) {
        cmdline_ = cmd;
        using namespace std::literals;
        using namespace boost;

        for (auto i = 0u; i < cmd_args.size(); ++i) {
            cmdline_ += " "s + cmd_args[i];
        }

        std::error_code ec;
        on_stdout_ = [this, stdoutCb, stdout_line_delimiter](const system::error_code & ec, size_t n) {
            if (!ec) {
                std::istream is(&stdout_buf_);
                std::string line;
                std::getline(is, line, stdout_line_delimiter);
                if (is) {
                    stdoutCb(line);
                } else {
                    HMBDC_THROW(StdoutIoError, "internal error");
                }
                asio::async_read_until(pipe_out_, stdout_buf_, stdout_line_delimiter, on_stdout_);
            } else {
                HMBDC_THROW(StdoutIoError, ec);
            }
        };

        on_stderr_ = [this, stderrCb, stderr_line_delimiter](const system::error_code & ec, std::size_t n) {
            if (!ec) {
                std::istream is(&stderr_buf_);
                std::string line;
                std::getline(is, line, stderr_line_delimiter);
                if (is) {
                    stderrCb(line);
                } else {
                    HMBDC_THROW(StderrIoError, "internal error");
                }
                asio::async_read_until(pipe_err_, stderr_buf_, stderr_line_delimiter, on_stderr_);
            } else {
                HMBDC_THROW(StderrIoError, ec);
            }
        };
        
        // schedule read the stdout and stderr for the first time
        asio::async_read_until(pipe_out_, stdout_buf_, stdout_line_delimiter, on_stdout_);
        asio::async_read_until(pipe_err_, stderr_buf_, stderr_line_delimiter, on_stderr_);

    } catch (std::exception const& e) {
        HMBDC_THROW(LaunchError, '"' << cmd << '"' 
            << " launching error:" << e.what());
    }

    /**
     * @brief check if the process is running
     * 
     * @return true - is running
     * @return false - not running
     */
    bool running() {
        return process_.running();
    }

    /**
     * @brief send a signal to the child process
     * 
     * @param signal - Linux signal id
     */
    void kill(int signal = SIGINT) {
        ::kill(process_.id(), signal);
    }

    /**
     * @brief wait until the child prcess exits
     * @return the exit code
     */
    int wait() {
        process_.wait();
        return process_.exit_code();
    }

    /**
     * @brief call this to have the callbacks excercised, it returns when one callback
     * is called OR the max_blocking_time is reached
     * user need to catch exceptions StdoutIoError and StderrIoError
     * @param max_blocking_time return if the max time is reached
     * @return return true if callback has been called
     */
    bool runOneFor(time::Duration max_blocking_time) {
        return ios_.run_one_for(
            std::chrono::milliseconds(static_cast<uint32_t>(max_blocking_time.milliseconds())));
    }

    /**
     * @brief inject an item into the stdin of the process, don't forget to call syncStdin to flush
     * the content out. 
     * This operation can be called in a different thread than the one runnning runOneFor().
     * @tparam T a streamable item
     * @param item 
     * @return AsyncExecutionIo& this object itself
     */
    template <typename T>
    AsyncExecutionIo& operator << (T&& item) {
        stdin_stream_ << std::forward<T>(item);
        return *this;
    }

    /**
     * @brief sync the stdin of the process
     */
    void syncStdin() {
        stdin_stream_.flush();
    }

    /**
     * @brief Destroy the Process Io object - with clean ups
     */
    ~AsyncExecutionIo() {
        stdin_stream_.close();
        this->kill(SIGINT);
        this->wait();
    }
private:
    std::string cmdline_;
    boost::asio::io_service ios_;
    boost::process::async_pipe pipe_out_{ios_};
    boost::process::async_pipe pipe_err_{ios_};
    boost::process::opstream stdin_stream_;
    boost::asio::streambuf stdout_buf_;
    boost::asio::streambuf stderr_buf_;
    std::function<void(const boost::system::error_code & ec, std::size_t n)> on_stdout_;
    std::function<void(const boost::system::error_code & ec, std::size_t n)> on_stderr_;
    boost::process::child process_;
};
}}
