#pragma once
// ==================================================================
//  线程同步兼容层(项目内统一入口:Mutex / LockGuard / Thread / SleepMs)
//
//  背景:本机 MinGW(gcc8,win32 线程模型)的 libstdc++ 没有
//  std::mutex / std::thread —— QC2015P_Cpp_Plus 参考工程也因同样限制
//  改用 Win32 临界区与 CreateThread(见其 main_final.cpp / can_tosun.cpp 注释)。
//
//  因此:
//    - 在无线程模型的 MinGW(_WIN32 && __GNUC__ && !_GLIBCXX_HAS_GTHREADS)
//      上,退化为 Win32 临界区 / CreateThread 实现;
//    - 在其他工具链(MSVC、posix 线程 MinGW 等)上直接使用标准库。
//  两条路径的使用方式完全一致。
// ==================================================================
#include <chrono>
#include <functional>
#include <stdexcept>

#if defined(_WIN32) && defined(__GNUC__) && !defined(_GLIBCXX_HAS_GTHREADS)

// ---------------- Win32 实现(本机 MinGW 走此分支) ----------------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace canhw {

/// 临界区封装(替代 std::mutex)
class Mutex {
public:
    Mutex() { InitializeCriticalSection(&cs_); }
    ~Mutex() { DeleteCriticalSection(&cs_); }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock() { EnterCriticalSection(&cs_); }
    void Unlock() { LeaveCriticalSection(&cs_); }

private:
    CRITICAL_SECTION cs_;
};

/// RAII 加锁(替代 std::lock_guard<std::mutex>)
class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.Lock(); }
    ~LockGuard() { m_.Unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& m_;
};

/// 线程封装(替代 std::thread,提供统一的 Start/Join 语义)
class Thread {
public:
    Thread() = default;
    ~Thread() { Join(); }
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    /// 启动线程,入口为 fn(args...)(如 Start(&Class::Proc, this))
    template <class Fn, class... Args>
    void Start(Fn fn, Args... args) {
        auto* task = new std::function<void()>(
            [=]() { std::invoke(fn, args...); });
        handle_ = CreateThread(nullptr, 0, &Thread::Entry, task, 0, nullptr);
        if (handle_ == nullptr) {
            delete task;
            throw std::runtime_error("CreateThread failed");
        }
    }

    /// 等待线程退出并回收句柄(可重复调用)
    void Join() {
        if (handle_ != nullptr) {
            WaitForSingleObject(handle_, INFINITE);
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    bool Joinable() const { return handle_ != nullptr; }

private:
    static DWORD WINAPI Entry(LPVOID p) {
        auto* task = static_cast<std::function<void()>*>(p);
        try {
            (*task)();
        } catch (...) {
            // 入口异常不得逃逸到 Win32 边界
        }
        delete task;
        return 0;
    }

    HANDLE handle_ = nullptr;
};

/// 线程休眠(替代 std::this_thread::sleep_for)
inline void SleepMs(int milliseconds) {
    Sleep(static_cast<DWORD>(milliseconds));
}

} // namespace canhw

#else

// ---------------- 标准库实现(MSVC / posix 线程 MinGW 走此分支) ----------------
#include <mutex>
#include <thread>

namespace canhw {

using Mutex = std::mutex;
using LockGuard = std::lock_guard<std::mutex>;

/// 线程封装(与 Win32 分支同语义:Start/Join)
class Thread {
public:
    Thread() = default;
    ~Thread() { Join(); }
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    template <class Fn, class... Args>
    void Start(Fn&& fn, Args&&... args) {
        thread_ = std::thread(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }

    void Join() {
        if (thread_.joinable()) thread_.join();
    }

    bool Joinable() const { return thread_.joinable(); }

private:
    std::thread thread_;
};

inline void SleepMs(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

} // namespace canhw

#endif
