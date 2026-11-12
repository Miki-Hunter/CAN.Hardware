#pragma once
// ==================================================================
//  Kvaser canlib32.dll 的动态绑定(对应 .NET BaseLibDll/KvaserCanLibDLL.cs)
//  函数签名与 Kvaser CANLIB SDK 的 canlib.h 一致(x64)。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <canhw/KvaserConfig.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace kvaser {

// ---------------- canlib 常量(与 .NET KvaserCanLibDLL 一致) ----------------
namespace canlib {
constexpr int32_t canOK = 0;
constexpr int32_t canERR_NOMSG = -2;
constexpr int32_t canERR_TXBUFOFL = -13;

constexpr int32_t canOPEN_EXCLUSIVE = 8;
constexpr int32_t canOPEN_ACCEPT_VIRTUAL = 32;
constexpr int32_t canOPEN_CAN_FD = 1024;

constexpr uint32_t canMSG_RTR = 0x0001;
constexpr uint32_t canMSG_STD = 0x0002;
constexpr uint32_t canMSG_EXT = 0x0004;

constexpr uint32_t canFDMSG_FDF = 0x010000;
constexpr uint32_t canFDMSG_BRS = 0x020000;

constexpr uint32_t canIOCTL_SET_TIMER_SCALE = 6;

// canBITRATE_xxx 预设码
constexpr int32_t canBITRATE_1M = -1;
constexpr int32_t canBITRATE_500K = -2;
constexpr int32_t canBITRATE_250K = -3;
constexpr int32_t canBITRATE_125K = -4;
constexpr int32_t canBITRATE_100K = -5;
constexpr int32_t canBITRATE_62K = -6;
constexpr int32_t canBITRATE_50K = -7;
constexpr int32_t canBITRATE_83K = -8;
constexpr int32_t canBITRATE_10K = -9;

// canFD_BITRATE_xxx 预设码
constexpr int32_t canFD_BITRATE_500K_80P = -1000;
constexpr int32_t canFD_BITRATE_1M_80P = -1001;
constexpr int32_t canFD_BITRATE_2M_80P = -1002;
constexpr int32_t canFD_BITRATE_4M_80P = -1003;
constexpr int32_t canFD_BITRATE_8M_60P = -1004;
} // namespace canlib

/// kvBusParamsTq(与 canlib.h 一致,6 个 int)
struct KvBusParamsTq {
    int32_t tq;
    int32_t phase1;
    int32_t phase2;
    int32_t sjw;
    int32_t prop;
    int32_t prescaler;
};

class KvaserApi {
public:
    static KvaserApi& Instance();

    bool Load();
    bool IsLoaded() const { return loaded_; }
    void EnsureLoaded();

    // 库初始化/卸载引用计数:整机装卸一次,多实例共享(对应 .NET InitializeLibrary/UnloadLibrary)
    void InitializeLibrary();
    void UnloadLibrary();

    int32_t OpenChannel(int32_t channel, int32_t flags);
    int32_t SetBusParams(int32_t hnd, int32_t freq, uint32_t tseg1, uint32_t tseg2,
                         uint32_t sjw, uint32_t noSamp, uint32_t syncmode);
    int32_t SetBusParamsTq(int32_t hnd, const KvBusParamsTq& nominal);
    int32_t SetBusParamsFd(int32_t hnd, int32_t freqBrs, uint32_t tseg1Brs,
                           uint32_t tseg2Brs, uint32_t sjwBrs);
    int32_t SetBusParamsFdTq(int32_t hnd, const KvBusParamsTq& nominal,
                             const KvBusParamsTq& data);
    int32_t BusOn(int32_t hnd);
    int32_t BusOff(int32_t hnd);
    int32_t Close(int32_t hnd);
    int32_t Read(int32_t hnd, int32_t* id, void* msg, uint32_t* dlc, uint32_t* flag,
                 uint32_t* time);
    int32_t Write(int32_t hnd, int32_t id, const void* msg, uint32_t dlc, uint32_t flag);
    int32_t SetAcceptanceFilter(int32_t hnd, uint32_t code, uint32_t mask, int32_t ext);
    int32_t FlushReceiveQueue(int32_t hnd);
    int32_t FlushTransmitQueue(int32_t hnd);
    int32_t IoCtl(int32_t hnd, uint32_t func, void* buf, uint32_t buflen);
    /// 设置时间戳最小单位为 us(对应 .NET SetTimeScale)
    int32_t SetTimeScale(int32_t hnd, uint32_t us, uint32_t buflen);

    // ---------------- 工具 ----------------
    static int32_t BuildCanBitrate(KvaserCanBitrate bitrate);
    static int32_t BuildCanFdBitrate(KvaserCanFdBitrate bitrate);
    /// 常见 canStatus 的可读文本(替代 .NET 的 GetEnumDescription)
    static std::string StatusText(int32_t status);

    static const char* DefaultDllPath() {
        return ".\\HwDependency\\Kvaser\\Generic\\Win\\x64\\canlib32.dll";
    }

private:
    KvaserApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};

    std::mutex initLock_;
    int32_t refCount_ = 0;

    using InitializeLibraryFn = void (*)();
    using UnloadLibraryFn = int32_t (*)();
    using OpenChannelFn = int32_t (*)(int32_t, int32_t);
    using SetBusParamsFn = int32_t (*)(int32_t, int32_t, uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t);
    using SetBusParamsTqFn = int32_t (*)(int32_t, const KvBusParamsTq*);
    using SetBusParamsFdFn = int32_t (*)(int32_t, int32_t, uint32_t, uint32_t, uint32_t);
    using SetBusParamsFdTqFn = int32_t (*)(int32_t, const KvBusParamsTq*,
                                           const KvBusParamsTq*);
    using BusOnFn = int32_t (*)(int32_t);
    using ReadFn = int32_t (*)(int32_t, int32_t*, void*, uint32_t*, uint32_t*, uint32_t*);
    using WriteFn = int32_t (*)(int32_t, int32_t, const void*, uint32_t, uint32_t);
    using SetAcceptanceFilterFn = int32_t (*)(int32_t, uint32_t, uint32_t, int32_t);
    using FlushFn = int32_t (*)(int32_t);
    using IoCtlFn = int32_t (*)(int32_t, uint32_t, void*, uint32_t);

    InitializeLibraryFn pInitializeLibrary_ = nullptr;
    UnloadLibraryFn pUnloadLibrary_ = nullptr;
    OpenChannelFn pOpenChannel_ = nullptr;
    SetBusParamsFn pSetBusParams_ = nullptr;
    SetBusParamsTqFn pSetBusParamsTq_ = nullptr;
    SetBusParamsFdFn pSetBusParamsFd_ = nullptr;
    SetBusParamsFdTqFn pSetBusParamsFdTq_ = nullptr;
    BusOnFn pBusOn_ = nullptr;
    BusOnFn pBusOff_ = nullptr;
    BusOnFn pClose_ = nullptr;
    ReadFn pRead_ = nullptr;
    WriteFn pWrite_ = nullptr;
    SetAcceptanceFilterFn pSetAcceptanceFilter_ = nullptr;
    FlushFn pFlushReceiveQueue_ = nullptr;
    FlushFn pFlushTransmitQueue_ = nullptr;
    IoCtlFn pIoCtl_ = nullptr;
};

} // namespace kvaser
} // namespace canhw
