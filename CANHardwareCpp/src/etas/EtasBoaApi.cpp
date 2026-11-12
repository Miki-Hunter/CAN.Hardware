#include "EtasBoaApi.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace canhw {
namespace etas {

namespace {

// ---------------- DLL 定位 ----------------

// 定位 BOA 安装目录:先查注册表 HKLM\SOFTWARE\ETAS\BOA\2 的 PATH 值,
// 再回退默认安装路径(与 .NET ResolveBoaDirectory 一致)
std::string ResolveBoaDirectory() {
    const std::string boaDll = "dll-ocdProxy.dll";

#ifdef _WIN32
    // 注册表:HKLM\SOFTWARE\ETAS\BOA\2 的 PATH 值
    char installedPath[1024] = {};
    DWORD size = sizeof(installedPath) - 1;
    const LSTATUS regResult = RegGetValueA(
        HKEY_LOCAL_MACHINE, "SOFTWARE\\ETAS\\BOA\\2", "PATH", RRF_RT_REG_SZ, nullptr,
        installedPath, &size);
    if (regResult == ERROR_SUCCESS && installedPath[0] != '\0') {
        std::string dir(installedPath);
        // 注册表路径可能指向与本进程位数不一致的目录,按进程位数修正
#ifdef _WIN64
        const size_t pos = dir.find("win32");
        if (pos != std::string::npos) dir.replace(pos, 5, "x64");
#else
        const size_t pos = dir.find("x64");
        if (pos != std::string::npos) dir.replace(pos, 3, "win32");
#endif
        const std::string probe = dir + "\\" + boaDll;
        DWORD attrs = GetFileAttributesA(probe.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            return dir;
    }

    // 回退:Program Files\ETAS\BOA_V2\Bin\x64\Dll\Framework
    const char* programFiles = std::getenv("ProgramFiles");
    if (programFiles != nullptr && programFiles[0] != '\0') {
#ifdef _WIN64
        const std::string arch = "x64";
#else
        const std::string arch = "win32";
#endif
        const std::string dir = std::string(programFiles) + "\\ETAS\\BOA_V2\\Bin\\" +
                                arch + "\\Dll\\Framework";
        const std::string probe = dir + "\\" + boaDll;
        DWORD attrs = GetFileAttributesA(probe.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            return dir;
    }
#endif

    throw std::runtime_error(
        "未检测到 ETAS BOA V2 驱动(注册表与默认路径均未找到 " + boaDll +
        "),请先安装 ETAS BOA V2(随 INCA 等 ETAS 工具链发布)");
}

void WalkTree(const EtasCsiTree* node, const std::string& prefix,
              const std::string& protocol, std::vector<std::string>& nodes,
              std::unordered_set<const EtasCsiTree*>& visited) {
    const EtasCsiTree* current = node;
    while (current != nullptr && visited.insert(current).second) {
        const std::string name =
            prefix + "/" + std::string(reinterpret_cast<const char*>(current->item.uriName));
        if (name.find(protocol) != std::string::npos) nodes.push_back(name);

        if (current->child != nullptr)
            WalkTree(current->child, name, protocol, nodes, visited);

        current = current->sibling;
    }
}

} // namespace

// ---------------- 配置构造 ----------------

EtasCanConfiguration EtasCanConfiguration::Create(uint32_t baudrateHz,
                                                  uint32_t fdBaudrateHz,
                                                  uint32_t samplePoint,
                                                  uint32_t btlCycles, uint32_t sjw,
                                                  uint32_t fdSamplePoint,
                                                  uint32_t fdBtlCycles,
                                                  uint32_t fdSjw) {
    EtasCanConfiguration cfg{};
    cfg.baudrate = baudrateHz;
    cfg.samplePoint = samplePoint;
    cfg.samplesPerBit = 1;
    cfg.btlCycles = btlCycles;
    cfg.sjw = sjw;
    cfg.syncEdge = 1;
    cfg.physicalMedia = 1;
    cfg.selfReceptionMode = 0;
    cfg.busParticipationMode = 2;
    cfg.canFDEnabled = fdBaudrateHz == 0 ? 0u : 1u;
    cfg.canFDConfig.bitRate = fdBaudrateHz;
    cfg.canFDConfig.samplePoint = fdSamplePoint;
    cfg.canFDConfig.btlCycles = fdBtlCycles;
    cfg.canFDConfig.sjw = fdSjw;
    cfg.canFDConfig.flags = 0;
    cfg.canFDConfig.txSecondarySamplePointOffset = 0;
    cfg.canFDConfig.canRxMode = 2;
    cfg.canFDConfig.canFdRxMode = 4;
    cfg.canFDConfig.canFdTxConfig = 4;
    cfg.canFDConfig.txSecondarySamplePointFilterWindow = 0;
    cfg.canFDConfig.reserved = 0;
    cfg.canTxPolicy = 1;
    return cfg;
}

// ---------------- API 单例 ----------------

EtasBoaApi& EtasBoaApi::Instance() {
    static EtasBoaApi api;
    return api;
}

void EtasBoaApi::EnsureLoaded() {
    if (loaded_) return;
    std::lock_guard<std::mutex> lock(loadLock_);
    if (loaded_) return;

    const std::string dir = ResolveBoaDirectory();

    // BOA DLL 的依赖项与其同目录,用完整路径加载(LOAD_WITH_ALTERED_SEARCH_PATH)
    if (!boaLib_.LoadFullPath(dir + "\\dll-ocdProxy.dll"))
        throw std::runtime_error("加载 dll-ocdProxy.dll 失败(目录: " + dir +
                                 ", WinError=" +
                                 std::to_string(DynamicLibrary::LastError()) + ")");
    if (!csiLib_.LoadFullPath(dir + "\\dll-csiBind.dll"))
        throw std::runtime_error("加载 dll-csiBind.dll 失败(目录: " + dir +
                                 ", WinError=" +
                                 std::to_string(DynamicLibrary::LastError()) + ")");

    // OCI (dll-ocdProxy.dll)
    pCreateController_ = reinterpret_cast<OciCreateControllerVersionFn>(
        boaLib_.GetSymbol("OCI_CreateCANControllerVersion"));
    pDestroyController_ =
        reinterpret_cast<OciHandleFn>(boaLib_.GetSymbol("OCI_DestroyCANController"));
    pCloseController_ =
        reinterpret_cast<OciHandleFn>(boaLib_.GetSymbol("OCI_CloseCANController"));
    pOpenCANController_ = reinterpret_cast<OciOpenCANControllerFn>(
        boaLib_.GetSymbol("OCI_OpenCANController"));
    pCreateTxQueue_ = reinterpret_cast<OciCreateTxQueueFn>(
        boaLib_.GetSymbol("OCI_CreateCANTxQueue"));
    pCreateRxQueue_ = reinterpret_cast<OciCreateRxQueueFn>(
        boaLib_.GetSymbol("OCI_CreateCANRxQueue"));
    pWriteCANDataEx_ =
        reinterpret_cast<OciWriteFn>(boaLib_.GetSymbol("OCI_WriteCANDataEx"));
    pAddFilter_ = reinterpret_cast<OciFilterFn>(
        boaLib_.GetSymbol("OCI_AddCANFrameFilter"));
    pGetTimerCapabilities_ = reinterpret_cast<OciTimerFn>(
        boaLib_.GetSymbol("OCI_GetTimerCapabilities"));

    // CSI (dll-csiBind.dll)
    pCreateTree_ = reinterpret_cast<CsiCreateTreeFn>(
        csiLib_.GetSymbol("CSI_CreateProtocolTree"));
    pDestroyTree_ = reinterpret_cast<CsiDestroyTreeFn>(
        csiLib_.GetSymbol("CSI_DestroyProtocolTree"));

    const bool ok = pCreateController_ && pDestroyController_ && pCloseController_ &&
                    pOpenCANController_ && pCreateTxQueue_ && pCreateRxQueue_ &&
                    pWriteCANDataEx_ && pAddFilter_ && pGetTimerCapabilities_ &&
                    pCreateTree_ && pDestroyTree_;
    if (!ok) {
        throw std::runtime_error(
            "BOA 导出函数不完整(OCI_CreateCANControllerVersion/.../CSI_"
            "DestroyProtocolTree),请确认安装的是完整的 ETAS BOA V2 驱动");
    }
    loaded_ = true;
}

EtasErrorCode EtasBoaApi::CreateCANControllerVersion(const char* uriLocation,
                                                     EtasVersion* version,
                                                     int32_t* handle) {
    EnsureLoaded();
    return pCreateController_(uriLocation, version, handle);
}

EtasErrorCode EtasBoaApi::DestroyCANController(int32_t handle) {
    EnsureLoaded();
    return pDestroyController_(handle);
}

EtasErrorCode EtasBoaApi::CloseCANController(int32_t handle) {
    EnsureLoaded();
    return pCloseController_(handle);
}

EtasErrorCode EtasBoaApi::OpenCANController(int32_t handle,
                                            EtasCanConfiguration* configuration,
                                            EtasControllerProperties* properties) {
    EnsureLoaded();
    return pOpenCANController_(handle, configuration, properties);
}

EtasErrorCode EtasBoaApi::CreateCANTxQueue(int32_t handle,
                                           EtasTxQueueConfiguration* configuration,
                                           int32_t* queue) {
    EnsureLoaded();
    return pCreateTxQueue_(handle, configuration, queue);
}

EtasErrorCode EtasBoaApi::CreateCANRxQueue(int32_t handle,
                                           EtasRxQueueConfiguration* configuration,
                                           int32_t* queue) {
    EnsureLoaded();
    return pCreateRxQueue_(handle, configuration, queue);
}

EtasErrorCode EtasBoaApi::WriteCANDataEx(int32_t handle, int64_t timeout,
                                         void** messages, uint32_t size,
                                         uint32_t* remaining) {
    EnsureLoaded();
    return pWriteCANDataEx_(handle, timeout, messages, size, remaining);
}

EtasErrorCode EtasBoaApi::AddCANFrameFilter(int32_t queue, EtasRxFilter* filter,
                                            uint32_t count) {
    EnsureLoaded();
    return pAddFilter_(queue, filter, count);
}

uint32_t EtasBoaApi::QueryTickFrequency(int32_t ctrlHandle) {
    EtasTimerCapabilities caps{};
    if (IsError(pGetTimerCapabilities_(ctrlHandle, &caps))) return 0;
    return caps.tickFrequency;
}

std::vector<std::string> EtasBoaApi::EnumerateNodes(const std::string& protocol) {
    EnsureLoaded();

    EtasCsiTree* treeHandle = nullptr;
    const EtasNodeRange range{NODE_TYPE_MIN_PHYSICAL_NODE,
                              NODE_TYPE_MAX_PHYSICAL_NODE};
    const EtasErrorCode status = pCreateTree_("", &range, &treeHandle);
    if (status != CODE_SUCCESS || treeHandle == nullptr)
        throw std::runtime_error("创建 CSI 协议树失败(" + StatusText(status) + ")");

    std::vector<std::string> nodes;
    try {
        // 按名称过滤(与 python-can 的 ETAS 绑定一致),nodeType 不作硬过滤
        std::unordered_set<const EtasCsiTree*> visited;
        WalkTree(treeHandle, "", protocol, nodes, visited);
        std::sort(nodes.begin(), nodes.end());  // Ordinal 排序
    } catch (...) {
        pDestroyTree_(treeHandle);
        throw;
    }
    pDestroyTree_(treeHandle);
    return nodes;
}

std::string EtasBoaApi::StatusText(EtasErrorCode code) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", code);
    return buf;
}

} // namespace etas
} // namespace canhw
