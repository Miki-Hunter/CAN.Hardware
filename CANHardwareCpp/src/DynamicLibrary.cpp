#include "DynamicLibrary.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // 防止 windows.h 的 min/max 宏污染 std::min/std::max
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace canhw {
namespace {

std::string StripLeadingDot(const std::string& p) {
    std::string s = p;
    while (s.rfind("./", 0) == 0 || s.rfind(".\\", 0) == 0) s = s.substr(2);
    return s;
}

} // namespace

DynamicLibrary::~DynamicLibrary() { Unload(); }

bool DynamicLibrary::Load(const std::string& path) {
    Unload();
#ifdef _WIN32
    handle_ = reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
    if (handle_ == nullptr) {
        // 回退:相对可执行文件目录解析
        const std::string exeDir = ExecutableDir();
        if (!exeDir.empty()) {
            const std::string alt = exeDir + "\\" + StripLeadingDot(path);
            handle_ = reinterpret_cast<void*>(LoadLibraryA(alt.c_str()));
        }
    }
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle_ == nullptr) {
        const std::string exeDir = ExecutableDir();
        if (!exeDir.empty()) {
            const std::string alt = exeDir + "/" + StripLeadingDot(path);
            handle_ = dlopen(alt.c_str(), RTLD_NOW | RTLD_LOCAL);
        }
    }
#endif
    return handle_ != nullptr;
}

bool DynamicLibrary::LoadFullPath(const std::string& fullPath) {
    Unload();
#ifdef _WIN32
    // LOAD_WITH_ALTERED_SEARCH_PATH(0x00000008):依赖 DLL 从本文件所在目录解析
    handle_ = reinterpret_cast<void*>(
        LoadLibraryExA(fullPath.c_str(), nullptr, 0x00000008));
#else
    handle_ = dlopen(fullPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    return handle_ != nullptr;
}

void DynamicLibrary::Unload() {
    if (handle_ == nullptr) return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

void* DynamicLibrary::GetSymbol(const char* name) const {
    if (handle_ == nullptr || name == nullptr) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle_), name));
#else
    return dlsym(handle_, name);
#endif
}

uint32_t DynamicLibrary::LastError() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetLastError());
#else
    return 0;
#endif
}

std::string DynamicLibrary::ExecutableDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::string s(buf, static_cast<size_t>(n));
    const size_t pos = s.find_last_of("\\/");
    if (pos == std::string::npos) return {};
    return s.substr(0, pos);
#else
    return {};
#endif
}

} // namespace canhw
