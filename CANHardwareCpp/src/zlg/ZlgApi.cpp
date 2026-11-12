#include "ZlgApi.h"

#include <stdexcept>

namespace canhw {
namespace zlg {

ZlgApi& ZlgApi::Instance() {
    static ZlgApi api;
    return api;
}

bool ZlgApi::Load() {
    if (loaded_) return true;

    canhw::LockGuard lock(deviceLock_);
    if (loaded_) return true;

    if (!lib_.Load(DefaultDllPath())) return false;

    pOpenDevice_  = reinterpret_cast<OpenDeviceFn>(lib_.GetSymbol("ZCAN_OpenDevice"));
    pCloseDevice_ = reinterpret_cast<CloseDeviceFn>(lib_.GetSymbol("ZCAN_CloseDevice"));
    pInitCAN_     = reinterpret_cast<InitCANFn>(lib_.GetSymbol("ZCAN_InitCAN"));
    pSetValue_    = reinterpret_cast<SetValueFn>(lib_.GetSymbol("ZCAN_SetValue"));
    pClearBuffer_ = reinterpret_cast<ClearBufferFn>(lib_.GetSymbol("ZCAN_ClearBuffer"));
    pResetCAN_    = reinterpret_cast<ResetCANFn>(lib_.GetSymbol("ZCAN_ResetCAN"));
    pStartCAN_    = reinterpret_cast<StartCANFn>(lib_.GetSymbol("ZCAN_StartCAN"));
    pTransmit_    = reinterpret_cast<TransmitFn>(lib_.GetSymbol("ZCAN_Transmit"));
    pTransmitFD_  = reinterpret_cast<TransmitFn>(lib_.GetSymbol("ZCAN_TransmitFD"));
    pReceive_     = reinterpret_cast<ReceiveFn>(lib_.GetSymbol("ZCAN_Receive"));
    pReceiveFD_   = reinterpret_cast<ReceiveFn>(lib_.GetSymbol("ZCAN_ReceiveFD"));

    const bool ok = pOpenDevice_ && pCloseDevice_ && pInitCAN_ && pSetValue_ &&
                    pClearBuffer_ && pResetCAN_ && pStartCAN_ && pTransmit_ &&
                    pTransmitFD_ && pReceive_ && pReceiveFD_;
    if (!ok) {
        lib_.Unload();
        return false;
    }
    loaded_ = true;
    return true;
}

void ZlgApi::EnsureLoaded() {
    if (loaded_) return;
    if (!Load()) {
        throw std::runtime_error(
            std::string("Failed to load ") + DefaultDllPath() +
            " (WinError=" + std::to_string(DynamicLibrary::LastError()) + ")");
    }
}

void* ZlgApi::OpenDevice(uint32_t deviceType, uint32_t deviceIndex, uint32_t reserved) {
    EnsureLoaded();
    canhw::LockGuard lock(deviceLock_);
    if (sharedDeviceHandle_ != nullptr) {
        // 已打开的共享设备:引用计数 +1,直接复用句柄
        ++deviceRefCount_;
        return sharedDeviceHandle_;
    }
    void* handle = pOpenDevice_(deviceType, deviceIndex, reserved);
    if (handle != nullptr) {
        sharedDeviceHandle_ = handle;
        deviceRefCount_ = 1;
    }
    return handle;
}

void ZlgApi::CloseDevice() {
    if (!loaded_) return;
    canhw::LockGuard lock(deviceLock_);
    if (deviceRefCount_ > 0) {
        --deviceRefCount_;
        if (deviceRefCount_ == 0 && sharedDeviceHandle_ != nullptr) {
            pCloseDevice_(sharedDeviceHandle_);
            sharedDeviceHandle_ = nullptr;
        }
    }
}

void* ZlgApi::InitCAN(void* deviceHandle, uint32_t canIndex, void* pInitConfig) {
    EnsureLoaded();
    return pInitCAN_(deviceHandle, canIndex, pInitConfig);
}

uint32_t ZlgApi::SetValue(void* deviceHandle, const char* path, const char* value) {
    EnsureLoaded();
    return pSetValue_(deviceHandle, path, value);
}

uint32_t ZlgApi::ClearBuffer(void* channelHandle) {
    EnsureLoaded();
    return pClearBuffer_(channelHandle);
}

uint32_t ZlgApi::ResetCAN(void* channelHandle) {
    EnsureLoaded();
    return pResetCAN_(channelHandle);
}

uint32_t ZlgApi::StartCAN(void* channelHandle) {
    EnsureLoaded();
    return pStartCAN_(channelHandle);
}

uint32_t ZlgApi::Transmit(void* channelHandle, void* pTransmit, uint32_t len) {
    EnsureLoaded();
    return pTransmit_(channelHandle, pTransmit, len);
}

uint32_t ZlgApi::TransmitFD(void* channelHandle, void* pTransmit, uint32_t len) {
    EnsureLoaded();
    return pTransmitFD_(channelHandle, pTransmit, len);
}

uint32_t ZlgApi::Receive(void* channelHandle, void* pReceive, uint32_t len, int waitTime) {
    EnsureLoaded();
    return pReceive_(channelHandle, pReceive, len, waitTime);
}

uint32_t ZlgApi::ReceiveFD(void* channelHandle, void* pReceive, uint32_t len, int waitTime) {
    EnsureLoaded();
    return pReceiveFD_(channelHandle, pReceive, len, waitTime);
}

uint32_t ZlgApi::MakeCanId(uint32_t id, bool eff, bool rtr, bool err) {
    const uint32_t ueff = eff ? 1u : 0u;
    const uint32_t urtr = rtr ? 1u : 0u;
    const uint32_t uerr = err ? 1u : 0u;
    return id | (ueff << 31) | (urtr << 30) | (uerr << 29);
}

uint32_t ZlgApi::BuildCanBitrate(ZlgCanBitrate bitrate) {
    switch (bitrate) {
        case ZlgCanBitrate::_50K:  return 50000;
        case ZlgCanBitrate::_100K: return 100000;
        case ZlgCanBitrate::_125K: return 125000;
        case ZlgCanBitrate::_250K: return 250000;
        case ZlgCanBitrate::_500K: return 500000;
        case ZlgCanBitrate::_1M:   return 1000000;
    }
    return 500000;
}

uint32_t ZlgApi::BuildCanFdBitrate(ZlgCanFDBitrate bitrate) {
    switch (bitrate) {
        case ZlgCanFDBitrate::_500K_80P: return 500000;
        case ZlgCanFDBitrate::_1M_80P:   return 1000000;
        case ZlgCanFDBitrate::_2M_60P:
        case ZlgCanFDBitrate::_2M_80P:   return 2000000;
        case ZlgCanFDBitrate::_4M_80P:   return 4000000;
        case ZlgCanFDBitrate::_8M_60P:
        case ZlgCanFDBitrate::_8M_70P:
        case ZlgCanFDBitrate::_8M_80P:   return 8000000;
    }
    return 2000000;
}

} // namespace zlg
} // namespace canhw
