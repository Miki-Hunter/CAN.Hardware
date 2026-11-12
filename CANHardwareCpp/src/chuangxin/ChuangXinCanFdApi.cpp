#include "ChuangXinCanFdApi.h"

#include <stdexcept>

#include <canhw/CanFrameHelper.h>

namespace canhw {
namespace chuangxin {

ChuangXinCanFdApi& ChuangXinCanFdApi::Instance() {
    static ChuangXinCanFdApi api;
    return api;
}

bool ChuangXinCanFdApi::Load() {
    if (loaded_) return true;

    std::lock_guard<std::mutex> lock(deviceLock_);
    if (loaded_) return true;

    if (!lib_.Load(DefaultDllPath())) return false;

    pOpenDevice_ = reinterpret_cast<OpenDeviceFn>(lib_.GetSymbol("ZCAN_OpenDevice"));
    pCloseDevice_ = reinterpret_cast<CloseDeviceFn>(lib_.GetSymbol("ZCAN_CloseDevice"));
    pInitCAN_ = reinterpret_cast<InitCanFn>(lib_.GetSymbol("ZCAN_InitCAN"));
    pStartCAN_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("ZCAN_StartCAN"));
    pResetCAN_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("ZCAN_ResetCAN"));
    pClearBuffer_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("ZCAN_ClearBuffer"));
    pTransmit_ = reinterpret_cast<TransmitFn>(lib_.GetSymbol("ZCAN_Transmit"));
    pTransmitFD_ = reinterpret_cast<TransmitFn>(lib_.GetSymbol("ZCAN_TransmitFD"));
    pReceive_ = reinterpret_cast<ReceiveFn>(lib_.GetSymbol("ZCAN_Receive"));
    pReceiveFD_ = reinterpret_cast<ReceiveFn>(lib_.GetSymbol("ZCAN_ReceiveFD"));
    pSetAbitBaud_ = reinterpret_cast<DeviceUintFn>(lib_.GetSymbol("ZCAN_SetAbitBaud"));
    pSetDbitBaud_ = reinterpret_cast<DeviceUintFn>(lib_.GetSymbol("ZCAN_SetDbitBaud"));
    pSetCANFDStandard_ =
        reinterpret_cast<DeviceUintFn>(lib_.GetSymbol("ZCAN_SetCANFDStandard"));
    pSetResistanceEnable_ =
        reinterpret_cast<DeviceUintFn>(lib_.GetSymbol("ZCAN_SetResistanceEnable"));
    pSetBaudRateCustom_ =
        reinterpret_cast<DeviceStrFn>(lib_.GetSymbol("ZCAN_SetBaudRateCustom"));
    pClearFilter_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("ZCAN_ClearFilter"));
    pSetFilterMode_ =
        reinterpret_cast<ChannelUintFn>(lib_.GetSymbol("ZCAN_SetFilterMode"));
    pSetFilterStartID_ =
        reinterpret_cast<ChannelUintFn>(lib_.GetSymbol("ZCAN_SetFilterStartID"));
    pSetFilterEndID_ =
        reinterpret_cast<ChannelUintFn>(lib_.GetSymbol("ZCAN_SetFilterEndID"));
    pAckFilter_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("ZCAN_AckFilter"));

    const bool ok = pOpenDevice_ && pCloseDevice_ && pInitCAN_ && pStartCAN_ &&
                    pResetCAN_ && pClearBuffer_ && pTransmit_ && pTransmitFD_ &&
                    pReceive_ && pReceiveFD_ && pSetAbitBaud_ && pSetDbitBaud_ &&
                    pSetCANFDStandard_ && pSetResistanceEnable_ &&
                    pSetBaudRateCustom_ && pClearFilter_ && pSetFilterMode_ &&
                    pSetFilterStartID_ && pSetFilterEndID_ && pAckFilter_;
    if (!ok) {
        lib_.Unload();
        return false;
    }
    loaded_ = true;
    return true;
}

void ChuangXinCanFdApi::EnsureLoaded() {
    if (loaded_) return;
    if (!Load()) {
        throw std::runtime_error(
            std::string("Failed to load ") + DefaultDllPath() +
            " (WinError=" + std::to_string(DynamicLibrary::LastError()) + ")");
    }
}

void* ChuangXinCanFdApi::OpenDevice(uint32_t deviceType, uint32_t deviceIndex) {
    EnsureLoaded();
    std::lock_guard<std::mutex> lock(deviceLock_);
    const auto key = std::make_pair(deviceType, deviceIndex);
    auto it = devices_.find(key);
    if (it != devices_.end()) {
        ++it->second.refCount;
        return it->second.handle;
    }
    void* handle = pOpenDevice_(deviceType, deviceIndex, 0);
    if (handle == nullptr) return nullptr;
    devices_[key] = DeviceEntry{handle, 1};
    return handle;
}

void ChuangXinCanFdApi::CloseDevice(uint32_t deviceType, uint32_t deviceIndex) {
    if (!loaded_) return;
    std::lock_guard<std::mutex> lock(deviceLock_);
    const auto key = std::make_pair(deviceType, deviceIndex);
    auto it = devices_.find(key);
    if (it == devices_.end()) return;

    if (--it->second.refCount > 0) return;
    try {
        pCloseDevice_(it->second.handle);
    } catch (...) {
    }
    devices_.erase(it);
}

void* ChuangXinCanFdApi::InitCAN(void* deviceHandle, uint32_t canIndex,
                                 const ZcanChannelInitConfig* pInitConfig) {
    EnsureLoaded();
    return pInitCAN_(deviceHandle, canIndex, pInitConfig);
}

uint32_t ChuangXinCanFdApi::StartCAN(void* channelHandle) {
    EnsureLoaded();
    return pStartCAN_(channelHandle);
}

uint32_t ChuangXinCanFdApi::ResetCAN(void* channelHandle) {
    EnsureLoaded();
    return pResetCAN_(channelHandle);
}

uint32_t ChuangXinCanFdApi::ClearBuffer(void* channelHandle) {
    EnsureLoaded();
    return pClearBuffer_(channelHandle);
}

uint32_t ChuangXinCanFdApi::Transmit(void* channelHandle, const void* pTransmit,
                                     uint32_t len) {
    EnsureLoaded();
    return pTransmit_(channelHandle, pTransmit, len);
}

uint32_t ChuangXinCanFdApi::TransmitFD(void* channelHandle, const void* pTransmit,
                                       uint32_t len) {
    EnsureLoaded();
    return pTransmitFD_(channelHandle, pTransmit, len);
}

uint32_t ChuangXinCanFdApi::Receive(void* channelHandle, void* pReceive, uint32_t len,
                                    int waitTime) {
    EnsureLoaded();
    return pReceive_(channelHandle, pReceive, len, waitTime);
}

uint32_t ChuangXinCanFdApi::ReceiveFD(void* channelHandle, void* pReceive, uint32_t len,
                                      int waitTime) {
    EnsureLoaded();
    return pReceiveFD_(channelHandle, pReceive, len, waitTime);
}

uint32_t ChuangXinCanFdApi::SetAbitBaud(void* deviceHandle, uint32_t canIndex,
                                        uint32_t abitBaud) {
    EnsureLoaded();
    return pSetAbitBaud_(deviceHandle, canIndex, abitBaud);
}

uint32_t ChuangXinCanFdApi::SetDbitBaud(void* deviceHandle, uint32_t canIndex,
                                        uint32_t dbitBaud) {
    EnsureLoaded();
    return pSetDbitBaud_(deviceHandle, canIndex, dbitBaud);
}

uint32_t ChuangXinCanFdApi::SetCANFDStandard(void* deviceHandle, uint32_t canIndex,
                                             uint32_t standard) {
    EnsureLoaded();
    return pSetCANFDStandard_(deviceHandle, canIndex, standard);
}

uint32_t ChuangXinCanFdApi::SetResistanceEnable(void* deviceHandle, uint32_t canIndex,
                                                uint32_t enable) {
    EnsureLoaded();
    return pSetResistanceEnable_(deviceHandle, canIndex, enable);
}

uint32_t ChuangXinCanFdApi::SetBaudRateCustom(void* deviceHandle, uint32_t canIndex,
                                              const char* rateCustom) {
    EnsureLoaded();
    return pSetBaudRateCustom_(deviceHandle, canIndex, rateCustom);
}

uint32_t ChuangXinCanFdApi::ClearFilter(void* channelHandle) {
    EnsureLoaded();
    return pClearFilter_(channelHandle);
}

uint32_t ChuangXinCanFdApi::SetFilterMode(void* channelHandle, uint32_t mode) {
    EnsureLoaded();
    return pSetFilterMode_(channelHandle, mode);
}

uint32_t ChuangXinCanFdApi::SetFilterStartID(void* channelHandle, uint32_t startId) {
    EnsureLoaded();
    return pSetFilterStartID_(channelHandle, startId);
}

uint32_t ChuangXinCanFdApi::SetFilterEndID(void* channelHandle, uint32_t endId) {
    EnsureLoaded();
    return pSetFilterEndID_(channelHandle, endId);
}

uint32_t ChuangXinCanFdApi::AckFilter(void* channelHandle) {
    EnsureLoaded();
    return pAckFilter_(channelHandle);
}

uint32_t ChuangXinCanFdApi::MakeCanId(uint32_t id, bool eff, bool rtr, bool err) {
    uint32_t raw = id & CanFrameDef::CAN_ID_FLAG;
    if (eff) raw |= CanFrameDef::CAN_EFF_FLAG;
    if (rtr) raw |= CanFrameDef::CAN_RTR_FLAG;
    if (err) raw |= CanFrameDef::CAN_ERR_FLAG;
    return raw;
}

uint32_t ChuangXinCanFdApi::BuildCanBitrate(ChuangXinCanBitrate bitrate) {
    switch (bitrate) {
        case ChuangXinCanBitrate::_50K:  return 50000;
        case ChuangXinCanBitrate::_100K: return 100000;
        case ChuangXinCanBitrate::_125K: return 125000;
        case ChuangXinCanBitrate::_250K: return 250000;
        case ChuangXinCanBitrate::_500K: return 500000;
        case ChuangXinCanBitrate::_800K: return 800000;
        case ChuangXinCanBitrate::_1M:   return 1000000;
    }
    return 500000;
}

uint32_t ChuangXinCanFdApi::BuildCanFdBitrate(ChuangXinCanFdBitrate bitrate) {
    switch (bitrate) {
        case ChuangXinCanFdBitrate::_100K: return 100000;
        case ChuangXinCanFdBitrate::_125K: return 125000;
        case ChuangXinCanFdBitrate::_250K: return 250000;
        case ChuangXinCanFdBitrate::_500K: return 500000;
        case ChuangXinCanFdBitrate::_800K: return 800000;
        case ChuangXinCanFdBitrate::_1M:   return 1000000;
        case ChuangXinCanFdBitrate::_2M:   return 2000000;
        case ChuangXinCanFdBitrate::_4M:   return 4000000;
        case ChuangXinCanFdBitrate::_5M:   return 5000000;
    }
    return 2000000;
}

} // namespace chuangxin
} // namespace canhw
