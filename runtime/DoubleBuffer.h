
#pragma once
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <new>

#include <unistd.h>

#include "Config.h"
#include "Portability.h"

class DoubleBuffer
{
    using MemPtrT = std::unique_ptr<char[]>;

    static MemPtrT allocateMemory()
    {
        return MemPtrT{new char[NanoLogConfig::OUTPUT_BUFFER_SIZE]};
    };

    static char *accessOnce(const MemPtrT &ptr)
    {
        NANOLOG_READ_WRITE_BARRIER;
        return ptr.get();
    }

    MemPtrT freeBuffer;
    MemPtrT compressingBuffer;
    MemPtrT writeBuffer;
    unsigned size;
    int errorCode;

    std::condition_variable condition;
    std::mutex mutex;

public:
    DoubleBuffer()
        : freeBuffer(allocateMemory()),
          compressingBuffer(allocateMemory()),
          writeBuffer(nullptr),
          size(0),
          errorCode(0),
          condition(),
          mutex(){};

    char *getCompressingBuffer() noexcept { return compressingBuffer.get(); }

    bool writeInProgress() const { return accessOnce(freeBuffer) == nullptr; }

    int swapBuffer(unsigned count) noexcept
    {
        while (accessOnce(writeBuffer) != nullptr) {}

        {
            std::unique_lock<std::mutex> lock(mutex);
            size = count;
            std::swap(writeBuffer, compressingBuffer);
            if (freeBuffer == nullptr) {
                condition.wait(lock, [this]() { return freeBuffer != nullptr; });
            } else {
                condition.notify_one();
            }
            std::swap(freeBuffer, compressingBuffer);
            return errorCode;
        }
    }

    void writeToFile(FILE* file) noexcept
    {
        unsigned tmp_size = 0;
        MemPtrT tmp_ptr = nullptr;

        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [this]() { return writeBuffer != nullptr; });
            tmp_size = size;
            std::swap(writeBuffer, tmp_ptr);
        }

        int res = 0;
        if (tmp_size != 0) {
            size_t wsize = fwrite(tmp_ptr.get(), 1u, tmp_size, file);
            res = (wsize != tmp_size) ? errno : 0;
        }

        while (accessOnce(freeBuffer) != nullptr) {}

        {
            std::unique_lock<std::mutex> lock(mutex);
            errorCode = res;
            std::swap(freeBuffer, tmp_ptr);
            if (compressingBuffer == nullptr) {
                condition.notify_one();
            }
        }
    }
};