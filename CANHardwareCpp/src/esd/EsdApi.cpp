#include "EsdApi.h"

#include <cstdio>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>

namespace canhw {
namespace esd {

EsdApi& EsdApi::Instance() {
    static EsdApi api;
    return api;
}

void EsdApi::EnsureLoaded() {
    if (loaded_) return;
    std::lock_guard<std::mutex> lock(loadLock_);
    if (loaded_) return;

    // 按系统搜索路径加载(与 .NET 一致:ntcan64.dll)
    if (!lib_.Load(DefaultDllPath())) {
        throw std::runtime_error(
            std::string("未检测到 ESD NTCAN 驱动(未找到 ") + DefaultDllPath() +
            "),请先安装 ESD CAN 驱动(NTCAN API,随 esd electronics CAN 接口卡发布) "
            "(WinError=" + std::to_string(DynamicLibrary::LastError()) + ")");
    }

    pOpen_ = reinterpret_cast<OpenFn>(lib_.GetSymbol("canOpen"));
    pClose_ = reinterpret_cast<CloseFn>(lib_.GetSymbol("canClose"));
    pReadT_ = reinterpret_cast<ReadTFn>(lib_.GetSymbol("canReadT"));
    pWriteT_ = reinterpret_cast<WriteTFn>(lib_.GetSymbol("canWriteT"));
    pReadX_ = reinterpret_cast<ReadXFn>(lib_.GetSymbol("canReadX"));
    pWriteX_ = reinterpret_cast<WriteXFn>(lib_.GetSymbol("canWriteX"));
    pSetBaudrate_ = reinterpret_cast<SetBaudrateFn>(lib_.GetSymbol("canSetBaudrate"));
    pSetBaudrateX_ =
        reinterpret_cast<SetBaudrateXFn>(lib_.GetSymbol("canSetBaudrateX"));
    pIdRegionAdd_ =
        reinterpret_cast<IdRegionAddFn>(lib_.GetSymbol("canIdRegionAdd"));

    // 关键函数缺失时立即报错,避免后续空调用
    if (!pOpen_ || !pClose_ || !pReadT_ || !pWriteT_ || !pReadX_ || !pWriteX_ ||
        !pSetBaudrate_ || !pSetBaudrateX_ || !pIdRegionAdd_) {
        lib_.Unload();
        throw std::runtime_error(
            std::string(DefaultDllPath()) +
            " 导出函数不完整(canOpen/canClose/canReadT/canWriteT/canReadX/"
            "canWriteX/canSetBaudrate/canSetBaudrateX/canIdRegionAdd),"
            "请确认安装的是完整的 ESD NTCAN 驱动");
    }
    loaded_ = true;
}

EsdCanResult EsdApi::CanOpen(int32_t channel, uint32_t flags, int32_t txQueueSize,
                             int32_t rxQueueSize, int32_t txTimeout, int32_t rxTimeout,
                             void** handle) {
    EnsureLoaded();
    return pOpen_(channel, flags, txQueueSize, rxQueueSize, txTimeout, rxTimeout,
                  handle);
}

EsdCanResult EsdApi::CanClose(void* handle) {
    EnsureLoaded();
    return pClose_(handle);
}

EsdCanResult EsdApi::CanRead(void* handle, EsdCanMsg* msg, int32_t* len) {
    EnsureLoaded();
    return pReadT_(handle, msg, len, nullptr);
}

EsdCanResult EsdApi::CanWrite(void* handle, EsdCanMsg* msg, int32_t* len) {
    EnsureLoaded();
    return pWriteT_(handle, msg, len, nullptr);
}

EsdCanResult EsdApi::CanReadX(void* handle, EsdCanFdMsg* msg, int32_t* len) {
    EnsureLoaded();
    return pReadX_(handle, msg, len, nullptr);
}

EsdCanResult EsdApi::CanWriteX(void* handle, EsdCanFdMsg* msg, int32_t* len) {
    EnsureLoaded();
    return pWriteX_(handle, msg, len, nullptr);
}

EsdCanResult EsdApi::CanSetBaudrate(void* handle, uint32_t baud) {
    EnsureLoaded();
    return pSetBaudrate_(handle, baud);
}

EsdCanResult EsdApi::CanSetBaudrateX(void* handle, const EsdBaudrateXNum* config) {
    EnsureLoaded();
    return pSetBaudrateX_(handle, config);
}

EsdCanResult EsdApi::CanIdRegionAdd(void* handle, int32_t idStart, int32_t* idCount) {
    EnsureLoaded();
    return pIdRegionAdd_(handle, idStart, idCount);
}

uint32_t EsdApi::BuildNtcanId(int64_t id) {
    uint32_t result = CanFrameHelper::GetId(id);
    if (CanFrameHelper::IsEFF(id)) result |= NTCAN_EXT_FLAG;
    return result;
}

int64_t EsdApi::BuildManagedId(uint32_t ntcanId, uint8_t len) {
    int64_t id = ntcanId & NTCAN_EXT_ID_MASK;
    if ((ntcanId & NTCAN_EXT_FLAG) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
    const bool isFd = (len & LEN_FLAG_FD) != 0;
    if (!isFd && (len & LEN_FLAG_RTR) != 0) id |= CanFrameDef::CAN_RTR_FLAG;
    return id;
}

std::string EsdApi::ResultText(EsdCanResult result) {
    switch (result) {
        case RESULT_OK: return "OK";
        case RX_TIMEOUT: return "RX_TIMEOUT";
        case HANDLE_FORCED_CLOSE: return "HANDLE_FORCED_CLOSE";
        default: return "EsdCanResult(0x" + [&] {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%X", result);
            return std::string(buf);
        }() + ")";
    }
}

} // namespace esd
} // namespace canhw
