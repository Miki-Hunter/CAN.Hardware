#pragma once
#include <cstdint>
#include <string>

namespace canhw {

/// 跨平台动态库加载封装(内部工具,不对外暴露)。
/// Windows 用 LoadLibrary/GetProcAddress,其他平台用 dlopen/dlsym。
/// 解析失败时返回 nullptr / false,由调用方决定是否抛异常。
class DynamicLibrary {
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    /// 加载动态库。相对路径先按当前工作目录解析,
    /// 失败后再按可执行文件所在目录解析(便于部署时把 HwDependency 放在 exe 旁)。
    bool Load(const std::string& path);

    /// 按完整路径加载(LOAD_WITH_ALTERED_SEARCH_PATH):
    /// 依赖 DLL 从被加载文件所在目录解析,用于 ETAS BOA 等安装目录不在搜索路径内的驱动
    bool LoadFullPath(const std::string& fullPath);

    void Unload();

    bool IsLoaded() const { return handle_ != nullptr; }

    /// 取符号地址;未加载或符号不存在返回 nullptr
    void* GetSymbol(const char* name) const;

    /// 最近一次平台加载错误码(Windows: GetLastError)
    static uint32_t LastError();

    /// 可执行文件所在目录(失败返回空串)
    static std::string ExecutableDir();

private:
    void* handle_ = nullptr;
};

} // namespace canhw
