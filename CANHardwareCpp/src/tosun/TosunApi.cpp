#include "TosunApi.h"

#include <array>
#include <stdexcept>

namespace canhw {
namespace tosun {

// ---------------- 事件分发(按通道号路由;同一通道的后注册者覆盖先注册者) ----------------

namespace {
constexpr size_t kMaxChannels = 32;
canhw::Mutex& DispatchMutex() {
    static canhw::Mutex m;
    return m;
}
std::array<TosunDispatch::CanHandler, kMaxChannels>& CanHandlers() {
    static std::array<TosunDispatch::CanHandler, kMaxChannels> handlers;
    return handlers;
}
std::array<TosunDispatch::CanFdHandler, kMaxChannels>& CanFdHandlers() {
    static std::array<TosunDispatch::CanFdHandler, kMaxChannels> handlers;
    return handlers;
}
} // namespace

void TosunDispatch::SetCanHandler(uint8_t channel, CanHandler handler) {
    canhw::LockGuard lock(DispatchMutex());
    if (channel < kMaxChannels) CanHandlers()[channel] = std::move(handler);
}

void TosunDispatch::ClearCanHandler(uint8_t channel) {
    canhw::LockGuard lock(DispatchMutex());
    if (channel < kMaxChannels) CanHandlers()[channel] = nullptr;
}

void TosunDispatch::SetCanFdHandler(uint8_t channel, CanFdHandler handler) {
    canhw::LockGuard lock(DispatchMutex());
    if (channel < kMaxChannels) CanFdHandlers()[channel] = std::move(handler);
}

void TosunDispatch::ClearCanFdHandler(uint8_t channel) {
    canhw::LockGuard lock(DispatchMutex());
    if (channel < kMaxChannels) CanFdHandlers()[channel] = nullptr;
}

void TosunDispatch::DispatchCan(const TLibCan* data) {
    CanHandler handler;
    {
        canhw::LockGuard lock(DispatchMutex());
        if (data->idxChn >= kMaxChannels) return;
        handler = CanHandlers()[data->idxChn];
    }
    if (handler) handler(*data);
}

void TosunDispatch::DispatchCanFd(const TLibCanFd* data) {
    CanFdHandler handler;
    {
        canhw::LockGuard lock(DispatchMutex());
        if (data->idxChn >= kMaxChannels) return;
        handler = CanFdHandlers()[data->idxChn];
    }
    if (handler) handler(*data);
}

void CANHW_TS_STDCALL TosunDispatch::CanThunk(const TLibCan* data) {
    // 回调运行在驱动线程,异常不能向外抛
    try {
        if (data) DispatchCan(data);
    } catch (...) {
    }
}

void CANHW_TS_STDCALL TosunDispatch::CanFdThunk(const TLibCanFd* data) {
    try {
        if (data) DispatchCanFd(data);
    } catch (...) {
    }
}

// ---------------- API 单例 ----------------

TosunApi& TosunApi::Instance() {
    static TosunApi api;
    return api;
}

bool TosunApi::Load() {
    if (loaded_) return true;

    canhw::LockGuard lock(initLock_);
    if (loaded_) return true;

    if (!lib_.Load(DefaultDllPath())) return false;

    pInit_ = reinterpret_cast<InitFn>(lib_.GetSymbol("initialize_lib_tscan"));
    pFinalize_ = reinterpret_cast<FinalizeFn>(lib_.GetSymbol("finalize_lib_tscan"));
    pConnect_ = reinterpret_cast<ConnectFn>(lib_.GetSymbol("tscan_connect"));
    pDisconnectByHandle_ =
        reinterpret_cast<DisconnectFn>(lib_.GetSymbol("tscan_disconnect_by_handle"));
    pCfgCan_ =
        reinterpret_cast<CfgCanFn>(lib_.GetSymbol("tscan_config_can_by_baudrate"));
    pCfgCanFd_ =
        reinterpret_cast<CfgCanFdFn>(lib_.GetSymbol("tscan_config_canfd_by_baudrate"));
    pCfgRegs_ =
        reinterpret_cast<CfgRegsFn>(lib_.GetSymbol("tscan_configure_canfd_regs"));
    pAddFilter_ = reinterpret_cast<FilterFn>(
        lib_.GetSymbol("tsfifo_add_can_canfd_pass_filter"));
    pRegister_ =
        reinterpret_cast<RegisterFn>(lib_.GetSymbol("tscan_register_event_can"));
    pUnregister_ =
        reinterpret_cast<UnregisterFn>(lib_.GetSymbol("tscan_unregister_event_can"));
    pRegisterFd_ =
        reinterpret_cast<RegisterFdFn>(lib_.GetSymbol("tscan_register_event_canfd"));
    pUnregisterFd_ = reinterpret_cast<UnregisterFdFn>(
        lib_.GetSymbol("tscan_unregister_event_canfd"));
    pTx_ = reinterpret_cast<TxFn>(lib_.GetSymbol("tscan_transmit_can_async"));
    pTxFd_ = reinterpret_cast<TxFdFn>(lib_.GetSymbol("tscan_transmit_canfd_async"));
    pClear_ = reinterpret_cast<ClearFn>(
        lib_.GetSymbol("tsfifo_clear_can_receive_buffers"));
    pClearFd_ = reinterpret_cast<ClearFn>(
        lib_.GetSymbol("tsfifo_clear_canfd_receive_buffers"));
    pErrorDesc_ =
        reinterpret_cast<ErrorDescFn>(lib_.GetSymbol("tscan_get_error_description"));

    const bool ok = pInit_ && pFinalize_ && pConnect_ && pDisconnectByHandle_ &&
                    pCfgCan_ && pCfgCanFd_ && pCfgRegs_ && pAddFilter_ && pRegister_ &&
                    pUnregister_ && pRegisterFd_ && pUnregisterFd_ && pTx_ && pTxFd_ &&
                    pClear_ && pClearFd_ && pErrorDesc_;
    if (!ok) {
        lib_.Unload();
        return false;
    }
    loaded_ = true;
    return true;
}

void TosunApi::EnsureLoaded() {
    if (loaded_) return;
    if (!Load()) {
        throw std::runtime_error(
            std::string("Failed to load ") + DefaultDllPath() +
            " (WinError=" + std::to_string(DynamicLibrary::LastError()) + ")");
    }
}

uint32_t TosunApi::ConnectDevice(const char* serial, bool enableFIFO,
                                 bool enableErrorFrame, bool enableTurbe,
                                 void** handle) {
    EnsureLoaded();
    canhw::LockGuard lock(deviceLock_);

    // 库级初始化(仅首次真正执行,引用计数)
    {
        canhw::LockGuard initLock(initLock_);
        if (libRefCount_ == 0) pInit_(enableFIFO, enableErrorFrame, enableTurbe);
        ++libRefCount_;
    }

    // 设备级连接(仅首次真正执行)
    if (sharedDeviceHandle_ != nullptr) {
        *handle = sharedDeviceHandle_;
        ++deviceRefCount_;
        return 0;
    }

    const uint32_t status = pConnect_(serial, &sharedDeviceHandle_);
    if (status == 0) {
        *handle = sharedDeviceHandle_;
        deviceRefCount_ = 1;
    } else {
        *handle = nullptr;
        // 连接失败:回滚本次库级引用
        canhw::LockGuard initLock(initLock_);
        if (libRefCount_ > 0) {
            --libRefCount_;
            if (libRefCount_ == 0) pFinalize_();
        }
    }
    return status;
}

void TosunApi::DisconnectDevice() {
    if (!loaded_) return;
    canhw::LockGuard lock(deviceLock_);

    if (deviceRefCount_ > 0) {
        --deviceRefCount_;
        if (deviceRefCount_ == 0 && sharedDeviceHandle_ != nullptr) {
            try {
                pDisconnectByHandle_(sharedDeviceHandle_);
            } catch (...) {
            }
            sharedDeviceHandle_ = nullptr;
        }
    }

    canhw::LockGuard initLock(initLock_);
    if (libRefCount_ > 0) {
        --libRefCount_;
        if (libRefCount_ == 0) pFinalize_();
    }
}

uint32_t TosunApi::ConfigCanByBaudrate(void* handle, int32_t channel, double rateKbps,
                                       uint32_t a120Ohm) {
    EnsureLoaded();
    return pCfgCan_(handle, channel, rateKbps, a120Ohm);
}

uint32_t TosunApi::ConfigCanFdByBaudrate(void* handle, int32_t channel, double arbKbps,
                                         double dataKbps, int32_t controllerType,
                                         int32_t controllerMode, uint32_t a120Ohm) {
    EnsureLoaded();
    return pCfgCanFd_(handle, channel, arbKbps, dataKbps, controllerType, controllerMode,
                      a120Ohm);
}

uint32_t TosunApi::ConfigureCanFdRegs(void* handle, int32_t channel, double arbRate,
                                      int seg1, int seg2, int prescaler, int sjw,
                                      double dataRate, int dataSeg1, int dataSeg2,
                                      int dataPrescaler, int dataSjw,
                                      int32_t controllerType, int32_t controllerMode,
                                      bool a120) {
    EnsureLoaded();
    return pCfgRegs_(handle, channel, arbRate, seg1, seg2, prescaler, sjw, dataRate,
                     dataSeg1, dataSeg2, dataPrescaler, dataSjw, controllerType,
                     controllerMode, a120);
}

uint32_t TosunApi::AddPassFilter(void* handle, int32_t channel, int32_t id, bool isStd) {
    EnsureLoaded();
    return pAddFilter_(handle, channel, id, isStd);
}

uint32_t TosunApi::RegisterEventCan(void* handle, CanEventFn callback) {
    EnsureLoaded();
    return pRegister_(handle, callback);
}

uint32_t TosunApi::UnregisterEventCan(void* handle, CanEventFn callback) {
    EnsureLoaded();
    return pUnregister_(handle, callback);
}

uint32_t TosunApi::RegisterEventCanFd(void* handle, CanFdEventFn callback) {
    EnsureLoaded();
    return pRegisterFd_(handle, callback);
}

uint32_t TosunApi::UnregisterEventCanFd(void* handle, CanFdEventFn callback) {
    EnsureLoaded();
    return pUnregisterFd_(handle, callback);
}

uint32_t TosunApi::TransmitCanAsync(void* handle, TLibCan* can) {
    EnsureLoaded();
    return pTx_(handle, can);
}

uint32_t TosunApi::TransmitCanFdAsync(void* handle, TLibCanFd* canFd) {
    EnsureLoaded();
    return pTxFd_(handle, canFd);
}

uint32_t TosunApi::ClearCanReceiveBuffers(void* handle, int32_t channel) {
    EnsureLoaded();
    return pClear_(handle, channel);
}

uint32_t TosunApi::ClearCanFdReceiveBuffers(void* handle, int32_t channel) {
    EnsureLoaded();
    return pClearFd_(handle, channel);
}

std::string TosunApi::ErrorText(uint32_t code) {
    if (!loaded_ || pErrorDesc_ == nullptr) return "code=" + std::to_string(code);
    const char* desc = nullptr;
    if (pErrorDesc_(code, &desc) != 0 || desc == nullptr)
        return "code=" + std::to_string(code);
    return desc;
}

} // namespace tosun
} // namespace canhw
