#include "KvaserApi.h"

#include <stdexcept>

namespace canhw {
namespace kvaser {

KvaserApi& KvaserApi::Instance() {
    static KvaserApi api;
    return api;
}

bool KvaserApi::Load() {
    if (loaded_) return true;

    std::lock_guard<std::mutex> lock(initLock_);
    if (loaded_) return true;

    if (!lib_.Load(DefaultDllPath())) return false;

    pInitializeLibrary_ =
        reinterpret_cast<InitializeLibraryFn>(lib_.GetSymbol("canInitializeLibrary"));
    pUnloadLibrary_ = reinterpret_cast<UnloadLibraryFn>(lib_.GetSymbol("canUnloadLibrary"));
    pOpenChannel_ = reinterpret_cast<OpenChannelFn>(lib_.GetSymbol("canOpenChannel"));
    pSetBusParams_ = reinterpret_cast<SetBusParamsFn>(lib_.GetSymbol("canSetBusParams"));
    pSetBusParamsTq_ =
        reinterpret_cast<SetBusParamsTqFn>(lib_.GetSymbol("canSetBusParamsTq"));
    pSetBusParamsFd_ =
        reinterpret_cast<SetBusParamsFdFn>(lib_.GetSymbol("canSetBusParamsFd"));
    pSetBusParamsFdTq_ =
        reinterpret_cast<SetBusParamsFdTqFn>(lib_.GetSymbol("canSetBusParamsFdTq"));
    pBusOn_ = reinterpret_cast<BusOnFn>(lib_.GetSymbol("canBusOn"));
    pBusOff_ = reinterpret_cast<BusOnFn>(lib_.GetSymbol("canBusOff"));
    pClose_ = reinterpret_cast<BusOnFn>(lib_.GetSymbol("canClose"));
    pRead_ = reinterpret_cast<ReadFn>(lib_.GetSymbol("canRead"));
    pWrite_ = reinterpret_cast<WriteFn>(lib_.GetSymbol("canWrite"));
    pSetAcceptanceFilter_ = reinterpret_cast<SetAcceptanceFilterFn>(
        lib_.GetSymbol("canSetAcceptanceFilter"));
    pFlushReceiveQueue_ =
        reinterpret_cast<FlushFn>(lib_.GetSymbol("canFlushReceiveQueue"));
    pFlushTransmitQueue_ =
        reinterpret_cast<FlushFn>(lib_.GetSymbol("canFlushTransmitQueue"));
    pIoCtl_ = reinterpret_cast<IoCtlFn>(lib_.GetSymbol("canIoCtl"));

    const bool ok = pInitializeLibrary_ && pUnloadLibrary_ && pOpenChannel_ &&
                    pSetBusParams_ && pSetBusParamsTq_ && pSetBusParamsFd_ &&
                    pSetBusParamsFdTq_ && pBusOn_ && pBusOff_ && pClose_ && pRead_ &&
                    pWrite_ && pSetAcceptanceFilter_ && pFlushReceiveQueue_ &&
                    pFlushTransmitQueue_ && pIoCtl_;
    if (!ok) {
        lib_.Unload();
        return false;
    }
    loaded_ = true;
    return true;
}

void KvaserApi::EnsureLoaded() {
    if (loaded_) return;
    if (!Load()) {
        throw std::runtime_error(
            std::string("Failed to load ") + DefaultDllPath() +
            " (WinError=" + std::to_string(DynamicLibrary::LastError()) +
            ",请安装 Kvaser 驱动)");
    }
}

void KvaserApi::InitializeLibrary() {
    EnsureLoaded();
    std::lock_guard<std::mutex> lock(initLock_);
    // 与 .NET 一致:每次 Open 都调用 canInitializeLibrary 并引用计数 +1
    pInitializeLibrary_();
    ++refCount_;
}

void KvaserApi::UnloadLibrary() {
    if (!loaded_) return;
    std::lock_guard<std::mutex> lock(initLock_);
    if (refCount_ > 0) {
        --refCount_;
        if (refCount_ == 0) pUnloadLibrary_();
    }
}

int32_t KvaserApi::OpenChannel(int32_t channel, int32_t flags) {
    EnsureLoaded();
    return pOpenChannel_(channel, flags);
}

int32_t KvaserApi::SetBusParams(int32_t hnd, int32_t freq, uint32_t tseg1,
                                uint32_t tseg2, uint32_t sjw, uint32_t noSamp,
                                uint32_t syncmode) {
    EnsureLoaded();
    return pSetBusParams_(hnd, freq, tseg1, tseg2, sjw, noSamp, syncmode);
}

int32_t KvaserApi::SetBusParamsTq(int32_t hnd, const KvBusParamsTq& nominal) {
    EnsureLoaded();
    return pSetBusParamsTq_(hnd, &nominal);
}

int32_t KvaserApi::SetBusParamsFd(int32_t hnd, int32_t freqBrs, uint32_t tseg1Brs,
                                  uint32_t tseg2Brs, uint32_t sjwBrs) {
    EnsureLoaded();
    return pSetBusParamsFd_(hnd, freqBrs, tseg1Brs, tseg2Brs, sjwBrs);
}

int32_t KvaserApi::SetBusParamsFdTq(int32_t hnd, const KvBusParamsTq& nominal,
                                    const KvBusParamsTq& data) {
    EnsureLoaded();
    return pSetBusParamsFdTq_(hnd, &nominal, &data);
}

int32_t KvaserApi::BusOn(int32_t hnd) {
    EnsureLoaded();
    return pBusOn_(hnd);
}

int32_t KvaserApi::BusOff(int32_t hnd) {
    EnsureLoaded();
    return pBusOff_(hnd);
}

int32_t KvaserApi::Close(int32_t hnd) {
    EnsureLoaded();
    return pClose_(hnd);
}

int32_t KvaserApi::Read(int32_t hnd, int32_t* id, void* msg, uint32_t* dlc,
                        uint32_t* flag, uint32_t* time) {
    EnsureLoaded();
    return pRead_(hnd, id, msg, dlc, flag, time);
}

int32_t KvaserApi::Write(int32_t hnd, int32_t id, const void* msg, uint32_t dlc,
                         uint32_t flag) {
    EnsureLoaded();
    return pWrite_(hnd, id, const_cast<void*>(msg), dlc, flag);
}

int32_t KvaserApi::SetAcceptanceFilter(int32_t hnd, uint32_t code, uint32_t mask,
                                       int32_t ext) {
    EnsureLoaded();
    return pSetAcceptanceFilter_(hnd, code, mask, ext);
}

int32_t KvaserApi::FlushReceiveQueue(int32_t hnd) {
    EnsureLoaded();
    return pFlushReceiveQueue_(hnd);
}

int32_t KvaserApi::FlushTransmitQueue(int32_t hnd) {
    EnsureLoaded();
    return pFlushTransmitQueue_(hnd);
}

int32_t KvaserApi::IoCtl(int32_t hnd, uint32_t func, void* buf, uint32_t buflen) {
    EnsureLoaded();
    return pIoCtl_(hnd, func, buf, buflen);
}

int32_t KvaserApi::SetTimeScale(int32_t hnd, uint32_t us, uint32_t buflen) {
    return IoCtl(hnd, canlib::canIOCTL_SET_TIMER_SCALE, &us, buflen);
}

int32_t KvaserApi::BuildCanBitrate(KvaserCanBitrate bitrate) {
    switch (bitrate) {
        case KvaserCanBitrate::canBITRATE_10K:  return canlib::canBITRATE_10K;
        case KvaserCanBitrate::canBITRATE_50K:  return canlib::canBITRATE_50K;
        case KvaserCanBitrate::canBITRATE_62K:  return canlib::canBITRATE_62K;
        case KvaserCanBitrate::canBITRATE_83K:  return canlib::canBITRATE_83K;
        case KvaserCanBitrate::canBITRATE_100K: return canlib::canBITRATE_100K;
        case KvaserCanBitrate::canBITRATE_125K: return canlib::canBITRATE_125K;
        case KvaserCanBitrate::canBITRATE_250K: return canlib::canBITRATE_250K;
        case KvaserCanBitrate::canBITRATE_500K: return canlib::canBITRATE_500K;
        case KvaserCanBitrate::canBITRATE_1M:   return canlib::canBITRATE_1M;
    }
    return canlib::canBITRATE_500K;
}

int32_t KvaserApi::BuildCanFdBitrate(KvaserCanFdBitrate bitrate) {
    switch (bitrate) {
        case KvaserCanFdBitrate::canFD_BITRATE_500K_80P:
            return canlib::canFD_BITRATE_500K_80P;
        case KvaserCanFdBitrate::canFD_BITRATE_1M_80P:
            return canlib::canFD_BITRATE_1M_80P;
        case KvaserCanFdBitrate::canFD_BITRATE_2M_80P:
            return canlib::canFD_BITRATE_2M_80P;
        case KvaserCanFdBitrate::canFD_BITRATE_4M_80P:
            return canlib::canFD_BITRATE_4M_80P;
        case KvaserCanFdBitrate::canFD_BITRATE_8M_60P:
            return canlib::canFD_BITRATE_8M_60P;
    }
    return canlib::canFD_BITRATE_2M_80P;
}

std::string KvaserApi::StatusText(int32_t status) {
    switch (status) {
        case 0:  return "canOK";
        case -1: return "canERR_PARAM";
        case -2: return "canERR_NOMSG";
        case -3: return "canERR_NOTFOUND";
        case -7: return "canERR_TIMEOUT";
        case -10: return "canERR_INVHANDLE";
        case -13: return "canERR_TXBUFOFL";
        case -15: return "canERR_HARDWARE";
        case -16: return "canERR_DYNALOAD";
        case -19: return "canERR_NOT_SUPPORTED";
        case -26: return "canERR_NOCARD";
        default: return "canStatus(" + std::to_string(status) + ")";
    }
}

} // namespace kvaser
} // namespace canhw
