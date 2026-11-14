#include "ChuangXinCanApi.h"

#include <stdexcept>

namespace canhw {
namespace chuangxin {

ChuangXinCanApi& ChuangXinCanApi::Instance() {
    static ChuangXinCanApi api;
    return api;
}

bool ChuangXinCanApi::Load() {
    if (loaded_) return true;

    canhw::LockGuard lock(deviceLock_);
    if (loaded_) return true;

    if (!lib_.Load(DefaultDllPath())) return false;

    pOpenDevice_ = reinterpret_cast<OpenDeviceFn>(lib_.GetSymbol("VCI_OpenDevice"));
    pCloseDevice_ = reinterpret_cast<CloseDeviceFn>(lib_.GetSymbol("VCI_CloseDevice"));
    pInitCAN_ = reinterpret_cast<InitCanFn>(lib_.GetSymbol("VCI_InitCAN"));
    pStartCAN_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("VCI_StartCAN"));
    pResetCAN_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("VCI_ResetCAN"));
    pClearBuffer_ = reinterpret_cast<ChannelFn>(lib_.GetSymbol("VCI_ClearBuffer"));
    pTransmit_ = reinterpret_cast<TransmitFn>(lib_.GetSymbol("VCI_Transmit"));
    pReceive_ = reinterpret_cast<ReceiveFn>(lib_.GetSymbol("VCI_Receive"));

    const bool ok = pOpenDevice_ && pCloseDevice_ && pInitCAN_ && pStartCAN_ &&
                    pResetCAN_ && pClearBuffer_ && pTransmit_ && pReceive_;
    if (!ok) {
        lib_.Unload();
        return false;
    }
    loaded_ = true;
    return true;
}

void ChuangXinCanApi::EnsureLoaded() {
    if (loaded_) return;
    if (!Load()) {
        throw std::runtime_error(
            std::string("Failed to load ") + DefaultDllPath() +
            " (WinError=" + std::to_string(DynamicLibrary::LastError()) + ")");
    }
}

bool ChuangXinCanApi::OpenDevice(uint32_t deviceType, uint32_t deviceIndex) {
    EnsureLoaded();
    canhw::LockGuard lock(deviceLock_);
    const auto key = std::make_pair(deviceType, deviceIndex);
    auto it = deviceRefCounts_.find(key);
    if (it != deviceRefCounts_.end() && it->second > 0) {
        ++it->second;
        return true;
    }
    // VCI_OpenDevice 返回 1=STATUS_OK,0=失败
    if (pOpenDevice_(deviceType, deviceIndex, 0) != 1) return false;
    deviceRefCounts_[key] = 1;
    return true;
}

void ChuangXinCanApi::CloseDevice(uint32_t deviceType, uint32_t deviceIndex) {
    if (!loaded_) return;
    canhw::LockGuard lock(deviceLock_);
    const auto key = std::make_pair(deviceType, deviceIndex);
    auto it = deviceRefCounts_.find(key);
    if (it == deviceRefCounts_.end()) return;

    if (it->second > 1) {
        --it->second;
        return;
    }
    try {
        pCloseDevice_(deviceType, deviceIndex);
    } catch (...) {
    }
    deviceRefCounts_.erase(it);
}

uint32_t ChuangXinCanApi::InitCAN(uint32_t deviceType, uint32_t deviceIndex,
                                  uint32_t canIndex, const VciInitConfig* pInitConfig) {
    EnsureLoaded();
    return pInitCAN_(deviceType, deviceIndex, canIndex, pInitConfig);
}

uint32_t ChuangXinCanApi::StartCAN(uint32_t deviceType, uint32_t deviceIndex,
                                   uint32_t canIndex) {
    EnsureLoaded();
    return pStartCAN_(deviceType, deviceIndex, canIndex);
}

uint32_t ChuangXinCanApi::ResetCAN(uint32_t deviceType, uint32_t deviceIndex,
                                   uint32_t canIndex) {
    EnsureLoaded();
    return pResetCAN_(deviceType, deviceIndex, canIndex);
}

uint32_t ChuangXinCanApi::ClearBuffer(uint32_t deviceType, uint32_t deviceIndex,
                                      uint32_t canIndex) {
    EnsureLoaded();
    return pClearBuffer_(deviceType, deviceIndex, canIndex);
}

uint32_t ChuangXinCanApi::Transmit(uint32_t deviceType, uint32_t deviceIndex,
                                   uint32_t canIndex, const VciCanObj* pSend,
                                   uint32_t len) {
    EnsureLoaded();
    return pTransmit_(deviceType, deviceIndex, canIndex, pSend, len);
}

uint32_t ChuangXinCanApi::Receive(uint32_t deviceType, uint32_t deviceIndex,
                                  uint32_t canIndex, VciCanObj* pReceive, uint32_t len,
                                  int waitTime) {
    EnsureLoaded();
    return pReceive_(deviceType, deviceIndex, canIndex, pReceive, len, waitTime);
}

void ChuangXinCanApi::BuildTiming(ChuangXinCanBitrate bitrate, uint8_t& timing0,
                                  uint8_t& timing1) {
    switch (bitrate) {
        case ChuangXinCanBitrate::_50K:  timing0 = 0x09; timing1 = 0x1C; break;
        case ChuangXinCanBitrate::_100K: timing0 = 0x04; timing1 = 0x1C; break;
        case ChuangXinCanBitrate::_125K: timing0 = 0x03; timing1 = 0x1C; break;
        case ChuangXinCanBitrate::_250K: timing0 = 0x01; timing1 = 0x1C; break;
        case ChuangXinCanBitrate::_500K: timing0 = 0x00; timing1 = 0x1C; break;
        case ChuangXinCanBitrate::_800K: timing0 = 0x00; timing1 = 0x16; break;
        case ChuangXinCanBitrate::_1M:   timing0 = 0x00; timing1 = 0x14; break;
        default: timing0 = 0x00; timing1 = 0x1C; break;
    }
}

} // namespace chuangxin
} // namespace canhw
