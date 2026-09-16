/**
 * @file Logger.h
 * @brief Global IO interceptor to duplicate std::cout output to a log file.
 *
 * Workflow:
 * 1. Initialize the Logger singleton with the target output filename.
 * 2. It redirects the std::cout stream buffer into a custom TeeBuffer.
 * 3. TeeBuffer overrides the sync() and overflow() methods to write character data
 *    simultaneously to the console and to an std::ofstream.
 * 4. At program exit, the Logger destructor restores the original std::cout buffer.
 */

#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <streambuf>
#include <string>

// Stream buffer that mirrors output to the terminal and log file.

/**
 * @class TeeBuffer
 * @brief A custom stream buffer that duplicates output to two underlying stream buffers.
 */
class TeeBuffer : public std::streambuf {
public:
    TeeBuffer(std::streambuf* sb1, std::streambuf* sb2)
        : sb1_(sb1), sb2_(sb2) {}

protected:
    virtual int overflow(int c) override {
        if (c == EOF) return !EOF;
        int r1 = sb1_->sputc(c);
        int r2 = sb2_->sputc(c);
        return (r1 == EOF || r2 == EOF) ? EOF : c;
    }
    virtual int sync() override {
        int r1 = sb1_->pubsync();
        int r2 = sb2_->pubsync();
        return (r1 == 0 && r2 == 0) ? 0 : -1;
    }

private:
    std::streambuf* sb1_;
    std::streambuf* sb2_;
};

// Process-wide log owner.

/**
 * @class Logger
 * @brief Singleton class that manages the log file and the TeeBuffer injection into std::cout.
 */
class Logger {
public:
    /**
     * @brief Initialize the global Logger and redirect std::cout to include the file.
     * @param log_filename The path to the log file to generate.
     * @param append If true, append to the file instead of overwriting it.
     */
    static void Init(const std::string& log_filename, bool append) {
        if (instance_) return;
        instance_ = std::unique_ptr<Logger>(new Logger(log_filename, append));
    }

    /**
     * @brief Destructor. Restores the original std::cout buffer when the program exits.
     */
    ~Logger() {
        if (original_cout_buf_) {
            std::cout.rdbuf(original_cout_buf_);
        }
    }

private:
    Logger(const std::string& log_filename, bool append) {
        file_stream_.open(log_filename, append ? std::ios::app : std::ios::trunc);
        if (file_stream_.is_open()) {
            original_cout_buf_ = std::cout.rdbuf();
            tee_buf_ = std::make_unique<TeeBuffer>(original_cout_buf_, file_stream_.rdbuf());
            std::cout.rdbuf(tee_buf_.get());
        } else {
            std::cerr << "[Warning] Could not open log file: " << log_filename << std::endl;
        }
    }

    std::ofstream file_stream_;
    std::streambuf* original_cout_buf_ = nullptr;
    std::unique_ptr<TeeBuffer> tee_buf_;

    static std::unique_ptr<Logger> instance_;
};

inline std::unique_ptr<Logger> Logger::instance_ = nullptr;
