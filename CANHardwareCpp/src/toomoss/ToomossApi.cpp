#include "ToomossApi.h"

#include <stdexcept>

namespace canhw {
namespace toomoss {

ToomossApi& ToomossApi::Instance() {
    static ToomossApi api;
    return api;
}

bool ToomossApi::Load() {
    if (loaded_) return true;

    canhw::LockGuard lock(deviceLock_);
    if (loaded_) return true;

    if (!lib_.Load(DefaultDllPath())) return false;

    pScan_ = reinterpret_cast<ScanFn>(lib_.GetSymbol("USB_ScanDevice"));
    pUsbOpen_ = reinterpret_cast<OpenFn>(lib_.GetSymbol("USB_OpenDevice"));
    pUsbClose_ = reinterpret_cast<CloseFn>(lib_.GetSymbol("USB_CloseDevice"));
    pCanSpeedArg_ =
        reinterpret_cast<SpeedArgFn>(lib_.GetSymbol("CAN_GetCANSpeedArg"));
    pCanInit_ = reinterpret_cast<InitFn>(lib_.GetSymbol("CAN_Init"));
    pCanFilterInit_ =
        reinterpret_cast<FilterInitFn>(lib_.GetSymbol("CAN_Filter_Init"));
    pCanStartGetMsg_ =
        reinterpret_cast<ChannelFn>(lib_.GetSymbol("CAN_StartGetMsg"));
    pCanStopGetMsg_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("CAN_StopGetMsg"));
    pCanStop_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("CAN_Stop"));
    pCanClearMsg_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("CAN_ClearMsg"));
    pCanSendMsg_ = reinterpret_cast<SendFn>(lib_.GetSymbol("CAN_SendMsg"));
    pCanGetMsg_ = reinterpret_cast<GetMsgFn>(lib_.GetSymbol("CAN_GetMsgWithSize"));
    pFdSpeedArg_ =
        reinterpret_cast<FdSpeedArgFn>(lib_.GetSymbol("CANFD_GetCANSpeedArg"));
    pFdInit_ = reinterpret_cast<FdInitFn>(lib_.GetSymbol("CANFD_Init"));
    pFdStartGetMsg_ =
        reinterpret_cast<ChannelFn>(lib_.GetSymbol("CANFD_StartGetMsg"));
    pFdStopGetMsg_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("CANFD_StopGetMsg"));
    pFdStop_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("CANFD_Stop"));
    pFdClearMsg_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("CANFD_ClearMsg"));
    pFdSendMsg_ = reinterpret_cast<FdSendFn>(lib_.GetSymbol("CANFD_SendMsg"));
    pFdGetMsg_ = reinterpret_cast<GetMsgFn>(lib_.GetSymbol("CANFD_GetMsg"));
    pFdSetFilter_ =
        reinterpret_cast<FdSetFilterFn>(lib_.GetSymbol("CANFD_SetFilter"));

    const bool ok = pScan_ && pUsbOpen_ && pUsbClose_ && pCanSpeedArg_ && pCanInit_ &&
                    pCanFilterInit_ && pCanStartGetMsg_ && pCanStopGetMsg_ &&
                    pCanStop_ && pCanClearMsg_ && pCanSendMsg_ && pCanGetMsg_ &&
                    pFdSpeedArg_ && pFdInit_ && pFdStartGetMsg_ && pFdStopGetMsg_ &&
                    pFdStop_ && pFdClearMsg_ && pFdSendMsg_ && pFdGetMsg_ &&
                    pFdSetFilter_;
    if (!ok) {
        lib_.Unload();
        return false;
    }
    loaded_ = true;
    return true;
}

void ToomossApi::EnsureLoaded() {
    if (loaded_) return;
    if (!Load()) {
        throw std::runtime_error(
            std::string("Failed to load ") + DefaultDllPath() +
            " (WinError=" + std::to_string(DynamicLibrary::LastError()) + ")");
    }
}

bool ToomossApi::OpenDevice(uint32_t deviceIndex, int32_t& devHandle) {
    EnsureLoaded();
    canhw::LockGuard lock(deviceLock_);

    // 没有已打开设备时重新扫描,便于设备热插拔后重新连接
    if (refCounts_.empty()) {
        int32_t handles[MaxDevices] = {};
        const int32_t count = pScan_(handles);
        if (count <= 0) {
            scannedHandles_.clear();
            devHandle = 0;
            return false;
        }
        const size_t n = static_cast<size_t>(count) < MaxDevices
                             ? static_cast<size_t>(count)
                             : MaxDevices;
        scannedHandles_.assign(handles, handles + n);
    }

    if (deviceIndex >= scannedHandles_.size()) {
        devHandle = 0;
        return false;
    }

    const int32_t handle = scannedHandles_[deviceIndex];
    auto it = refCounts_.find(handle);
    if (it != refCounts_.end()) {
        ++it->second;
        devHandle = handle;
        return true;
    }

    // USB_OpenDevice 返回非 0 表示成功
    if (pUsbOpen_(handle) == 0) {
        devHandle = 0;
        return false;
    }

    refCounts_[handle] = 1;
    devHandle = handle;
    return true;
}

void ToomossApi::CloseDevice(int32_t devHandle) {
    if (!loaded_) return;
    canhw::LockGuard lock(deviceLock_);

    auto it = refCounts_.find(devHandle);
    if (it == refCounts_.end()) return;

    if (it->second > 1) {
        --it->second;
        return;
    }

    try {
        pUsbClose_(devHandle);
    } catch (...) {
    }
    refCounts_.erase(it);
    if (refCounts_.empty()) scannedHandles_.clear();
}

int32_t ToomossApi::CanGetSpeedArg(int32_t devHandle, CanInitConfig* config,
                                   uint32_t speedBps) {
    EnsureLoaded();
    return pCanSpeedArg_(devHandle, config, speedBps);
}

int32_t ToomossApi::CanInit(int32_t devHandle, uint8_t channel,
                            const CanInitConfig* config) {
    EnsureLoaded();
    return pCanInit_(devHandle, channel, config);
}

int32_t ToomossApi::CanFilterInit(int32_t devHandle, uint8_t channel,
                                  const CanFilterConfig* filter) {
    EnsureLoaded();
    return pCanFilterInit_(devHandle, channel, filter);
}

int32_t ToomossApi::CanStartGetMsg(int32_t devHandle, uint8_t channel) {
    EnsureLoaded();
    return pCanStartGetMsg_(devHandle, channel);
}

int32_t ToomossApi::CanStopGetMsg(int32_t devHandle, uint8_t channel) {
    EnsureLoaded();
    return pCanStopGetMsg_(devHandle, channel);
}

int32_t ToomossApi::CanStop(int32_t devHandle, uint8_t channel) {
    EnsureLoaded();
    return pCanStop_(devHandle, channel);
}

int32_t ToomossApi::CanClearMsg(int32_t devHandle, uint8_t channel) {
    EnsureLoaded();
    return pCanClearMsg_(devHandle, channel);
}

int32_t ToomossApi::CanSendMsg(int32_t devHandle, uint8_t channel, const CanMsg* msg,
                               uint32_t num) {
    EnsureLoaded();
    return pCanSendMsg_(devHandle, channel, msg, num);
}

int32_t ToomossApi::CanGetMsgWithSize(int32_t devHandle, uint8_t channel, void* buffer,
                                      int32_t bufferSize) {
    EnsureLoaded();
    return pCanGetMsg_(devHandle, channel, buffer, bufferSize);
}

int32_t ToomossApi::CanFdGetSpeedArg(int32_t devHandle, CanFdInitConfig* config,
                                     uint32_t speedNbt, uint32_t speedDbt) {
    EnsureLoaded();
    return pFdSpeedArg_(devHandle, config, speedNbt, speedDbt);
}

int32_t ToomossApi::CanFdInit(int32_t devHandle, uint8_t channel,
                              const CanFdInitConfig* config) {
    EnsureLoaded();
    return pFdInit_(devHandle, channel, config);
}

int32_t ToomossApi::CanFdStartGetMsg(int32_t devHandle, uint8_t channel) {
    EnsureLoaded();
    return pFdStartGetMsg_(devHandle, channel);
}

int32_t ToomossApi::CanFdStopGetMsg(int32_t devHandle, uint8_t channel) {
    EnsureLoaded();
    return pFdStopGetMsg_(devHandle, channel);
}

int32_t ToomossApi::CanFdStop(int32_t devHandle, uint8_t channel) {
    EnsureLoaded();
    return pFdStop_(devHandle, channel);
}

int32_t ToomossApi::CanFdClearMsg(int32_t devHandle, uint8_t channel) {
    EnsureLoaded();
    return pFdClearMsg_(devHandle, channel);
}

int32_t ToomossApi::CanFdSendMsg(int32_t devHandle, uint8_t channel, const void* msg,
                                 uint32_t num) {
    EnsureLoaded();
    return pFdSendMsg_(devHandle, channel, msg, num);
}

int32_t ToomossApi::CanFdGetMsg(int32_t devHandle, uint8_t channel, void* buffer,
                                int32_t bufferSize) {
    EnsureLoaded();
    return pFdGetMsg_(devHandle, channel, buffer, bufferSize);
}

int32_t ToomossApi::CanFdSetFilter(int32_t devHandle, uint8_t channel,
                                   const CanFdFilterConfig* filter, uint8_t len) {
    EnsureLoaded();
    return pFdSetFilter_(devHandle, channel, filter, len);
}

} // namespace toomoss
} // namespace canhw
