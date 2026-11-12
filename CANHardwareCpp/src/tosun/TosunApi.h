#pragma once
// ==================================================================
//  同星 Tosun:libTSCAN.dll 动态绑定(对应 .NET BaseLibDll/ToSunCanLibDLL.cs)
//  接收为事件驱动(驱动线程回调),非轮询。
//  库级 initialize/finalize 与设备级 connect/disconnect 均为引用计数共享。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <functional>
#include <canhw/Sync.h>
#include <string>

#include <canhw/TosunConfig.h>

#include "DynamicLibrary.h"

#ifdef _WIN32
#define CANHW_TS_STDCALL __stdcall
#else
#define CANHW_TS_STDCALL
#endif

namespace canhw {
namespace tosun {

// ---------------- 原生结构(Pack=1,与 libTSCAN 头一致) ----------------

/// 经典 CAN 帧(TLIBCAN)
struct TLibCan {
    uint8_t idxChn;      ///< 通道号
    uint8_t properties;  ///< bit0 Tx, bit1 Remote, bit2 Ext, bit6 Logged, bit7 Error
    uint8_t dlc;
    uint8_t reserved;
    int32_t identifier;
    uint64_t timeUs;     ///< 时间戳,单位 µs
    uint8_t data[8];
};

/// CAN FD 帧(TLIBCANFD)
struct TLibCanFd {
    uint8_t idxChn;
    uint8_t properties;  ///< 同 TLibCan
    uint8_t dlc;         ///< 经典帧 0-8;FD 帧为 DLC 编码 0-15
    uint8_t fdProperties;///< bit0 FD, bit1 BRS, bit2 ESI
    int32_t identifier;
    uint64_t timeUs;
    uint8_t data[64];
};

// FProperties 位
constexpr uint8_t PROP_TX = 0x01;
constexpr uint8_t PROP_REMOTE = 0x02;
constexpr uint8_t PROP_EXT = 0x04;
constexpr uint8_t PROP_ERROR = 0x80;
// FFDProperties 位
constexpr uint8_t FD_PROP_FDF = 0x01;
constexpr uint8_t FD_PROP_BRS = 0x02;
constexpr uint8_t FD_PROP_ESI = 0x04;

/// READ_TX_RX_DEF
constexpr int32_t ONLY_RX_MESSAGES = 0;
constexpr int32_t TX_RX_MESSAGES = 1;

/// 事件回调(驱动线程触发;回调内不可抛异常)
using CanEventFn = void(CANHW_TS_STDCALL*)(const TLibCan*);
using CanFdEventFn = void(CANHW_TS_STDCALL*)(const TLibCanFd*);

// ---------------- 事件分发(回调不带设备上下文,按通道号路由到 Wrapper) ----------------
class TosunDispatch {
public:
    using CanHandler = std::function<void(const TLibCan&)>;
    using CanFdHandler = std::function<void(const TLibCanFd&)>;

    static void SetCanHandler(uint8_t channel, CanHandler handler);
    static void ClearCanHandler(uint8_t channel);
    static void SetCanFdHandler(uint8_t channel, CanFdHandler handler);
    static void ClearCanFdHandler(uint8_t channel);

    static void DispatchCan(const TLibCan* data);
    static void DispatchCanFd(const TLibCanFd* data);

    /// 固定回调桩(注册给 DLL 的唯一入口)
    static void CANHW_TS_STDCALL CanThunk(const TLibCan* data);
    static void CANHW_TS_STDCALL CanFdThunk(const TLibCanFd* data);
};

class TosunApi {
public:
    static TosunApi& Instance();

    bool Load();
    bool IsLoaded() const { return loaded_; }
    void EnsureLoaded();

    /// 连接设备(内部完成库级初始化;同一物理设备共享句柄,引用计数)
    uint32_t ConnectDevice(const char* serial, bool enableFIFO, bool enableErrorFrame,
                           bool enableTurbe, void** handle);
    /// 断开设备(设备级 + 库级引用计数,仅最后一个实例真正断开)
    void DisconnectDevice();

    uint32_t ConfigCanByBaudrate(void* handle, int32_t channel, double rateKbps,
                                 uint32_t a120Ohm);
    uint32_t ConfigCanFdByBaudrate(void* handle, int32_t channel, double arbKbps,
                                   double dataKbps, int32_t controllerType,
                                   int32_t controllerMode, uint32_t a120Ohm);
    uint32_t ConfigureCanFdRegs(void* handle, int32_t channel, double arbRate, int seg1,
                                int seg2, int prescaler, int sjw, double dataRate,
                                int dataSeg1, int dataSeg2, int dataPrescaler,
                                int dataSjw, int32_t controllerType,
                                int32_t controllerMode, bool a120);
    uint32_t AddPassFilter(void* handle, int32_t channel, int32_t id, bool isStd);
    uint32_t RegisterEventCan(void* handle, CanEventFn callback);
    uint32_t UnregisterEventCan(void* handle, CanEventFn callback);
    uint32_t RegisterEventCanFd(void* handle, CanFdEventFn callback);
    uint32_t UnregisterEventCanFd(void* handle, CanFdEventFn callback);
    uint32_t TransmitCanAsync(void* handle, TLibCan* can);
    uint32_t TransmitCanFdAsync(void* handle, TLibCanFd* canFd);
    uint32_t ClearCanReceiveBuffers(void* handle, int32_t channel);
    uint32_t ClearCanFdReceiveBuffers(void* handle, int32_t channel);

    /// 错误码转文本(内部调用 tscan_get_error_description)
    std::string ErrorText(uint32_t code);

    static const char* DefaultDllPath() {
        return ".\\HwDependency\\Tosun\\Generic\\Win\\x64\\libTSCAN.dll";
    }

private:
    TosunApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};

    // 库级引用计数(initialize_lib_tscan / finalize_lib_tscan)
    canhw::Mutex initLock_;
    int32_t libRefCount_ = 0;

    // 设备级共享句柄引用计数
    canhw::Mutex deviceLock_;
    void* sharedDeviceHandle_ = nullptr;
    int32_t deviceRefCount_ = 0;

    using InitFn = void (*)(bool, bool, bool);
    using FinalizeFn = void (*)();
    using ConnectFn = uint32_t (*)(const char*, void**);
    using DisconnectFn = uint32_t (*)(void*);
    using CfgCanFn = uint32_t (*)(void*, int32_t, double, uint32_t);
    using CfgCanFdFn = uint32_t (*)(void*, int32_t, double, double, int32_t, int32_t,
                                    uint32_t);
    using CfgRegsFn = uint32_t (*)(void*, int32_t, double, int, int, int, int, double,
                                   int, int, int, int, int32_t, int32_t, bool);
    using FilterFn = uint32_t (*)(void*, int32_t, int32_t, bool);
    using RegisterFn = uint32_t (*)(void*, CanEventFn);
    using UnregisterFn = uint32_t (*)(void*, CanEventFn);
    using RegisterFdFn = uint32_t (*)(void*, CanFdEventFn);
    using UnregisterFdFn = uint32_t (*)(void*, CanFdEventFn);
    using TxFn = uint32_t (*)(void*, TLibCan*);
    using TxFdFn = uint32_t (*)(void*, TLibCanFd*);
    using ClearFn = uint32_t (*)(void*, int32_t);
    using ErrorDescFn = uint32_t (*)(uint32_t, const char**);

    InitFn pInit_ = nullptr;
    FinalizeFn pFinalize_ = nullptr;
    ConnectFn pConnect_ = nullptr;
    DisconnectFn pDisconnectByHandle_ = nullptr;
    CfgCanFn pCfgCan_ = nullptr;
    CfgCanFdFn pCfgCanFd_ = nullptr;
    CfgRegsFn pCfgRegs_ = nullptr;
    FilterFn pAddFilter_ = nullptr;
    RegisterFn pRegister_ = nullptr;
    UnregisterFn pUnregister_ = nullptr;
    RegisterFdFn pRegisterFd_ = nullptr;
    UnregisterFdFn pUnregisterFd_ = nullptr;
    TxFn pTx_ = nullptr;
    TxFdFn pTxFd_ = nullptr;
    ClearFn pClear_ = nullptr;
    ClearFn pClearFd_ = nullptr;
    ErrorDescFn pErrorDesc_ = nullptr;
};

} // namespace tosun
} // namespace canhw
