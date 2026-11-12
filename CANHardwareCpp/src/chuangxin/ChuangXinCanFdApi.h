#pragma once
// ==================================================================
//  创芯科技 USBCANFD:ControlCANFD.dll(ZCAN_ API)动态绑定
//  (对应 .NET BaseLibDll/ChuangXinCanFdLibDLL.cs,与周立功 zlgcan 接口同源)
//  设备句柄按 (device_type, device_index) 共享,多通道复用,引用计数管理。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include <canhw/ChuangXinConfig.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace chuangxin {

constexpr uint32_t TYPE_CAN = 0;    ///< 通道类型:经典 CAN
constexpr uint32_t TYPE_CANFD = 1;  ///< 通道类型:CAN FD
constexpr uint32_t STATUS_OK = 1;   ///< 成功状态值(1=成功,0=失败)

// ---------------- 与 ControlCANFD 头一致的原生结构体(自然对齐) ----------------

struct CanFrame {          // can_frame
    uint32_t canId;        // SocketCAN 风格:高位含 EFF/RTR/ERR 标志
    uint8_t  dlc;
    uint8_t  pad;
    uint8_t  res0;
    uint8_t  res1;
    uint8_t  data[8];
};

struct CanFdFrame {        // canfd_frame
    uint32_t canId;
    uint8_t  len;          // 0..64,非 DLC 编码
    uint8_t  flags;        // bit0=BRS, bit1=ESI
    uint8_t  res0;
    uint8_t  res1;
    uint8_t  data[64];
};

struct ZcanCanInitConfig { // ZCAN_CAN_INIT_CONFIG
    uint32_t accCode;
    uint32_t accMask;
    uint32_t reserved;
    uint8_t  filter;
    uint8_t  timing0;
    uint8_t  timing1;
    uint8_t  mode;
};

struct ZcanCanFdInitConfig { // ZCAN_CANFD_INIT_CONFIG
    uint32_t accCode;
    uint32_t accMask;
    uint32_t abitTiming;
    uint32_t dbitTiming;
    uint32_t brp;
    uint8_t  filter;
    uint8_t  mode;
    uint16_t pad;
    uint32_t reserved;
};

/// ZCAN_CHANNEL_INIT_CONFIG(can_type 在偏移 0,联合体在偏移 4,与 .NET Explicit 布局一致)
struct ZcanChannelInitConfig {
    uint32_t canType;  // TYPE_CAN / TYPE_CANFD
    union {
        ZcanCanInitConfig can;
        ZcanCanFdInitConfig canfd;
    };
};

struct ZcanTransmitData {     // ZCAN_Transmit_Data
    CanFrame frame;
    uint32_t transmitType;
};

struct ZcanReceiveData {      // ZCAN_Receive_Data
    CanFrame frame;
    uint64_t timestamp;       // µs
};

struct ZcanTransmitFdData {   // ZCAN_TransmitFD_Data
    CanFdFrame frame;
    uint32_t transmitType;
};

struct ZcanReceiveFdData {    // ZCAN_ReceiveFD_Data
    CanFdFrame frame;
    uint64_t timestamp;       // µs
};

class ChuangXinCanFdApi {
public:
    static ChuangXinCanFdApi& Instance();

    bool Load();
    bool IsLoaded() const { return loaded_; }
    void EnsureLoaded();

    /// 打开设备(同一物理设备共享句柄,多通道复用)
    void* OpenDevice(uint32_t deviceType, uint32_t deviceIndex);
    /// 关闭设备(引用计数归零时才真正关闭)
    void CloseDevice(uint32_t deviceType, uint32_t deviceIndex);

    void* InitCAN(void* deviceHandle, uint32_t canIndex,
                  const ZcanChannelInitConfig* pInitConfig);
    uint32_t StartCAN(void* channelHandle);
    uint32_t ResetCAN(void* channelHandle);
    uint32_t ClearBuffer(void* channelHandle);
    uint32_t Transmit(void* channelHandle, const void* pTransmit, uint32_t len);
    uint32_t TransmitFD(void* channelHandle, const void* pTransmit, uint32_t len);
    uint32_t Receive(void* channelHandle, void* pReceive, uint32_t len, int waitTime);
    uint32_t ReceiveFD(void* channelHandle, void* pReceive, uint32_t len, int waitTime);
    uint32_t SetAbitBaud(void* deviceHandle, uint32_t canIndex, uint32_t abitBaud);
    uint32_t SetDbitBaud(void* deviceHandle, uint32_t canIndex, uint32_t dbitBaud);
    uint32_t SetCANFDStandard(void* deviceHandle, uint32_t canIndex, uint32_t standard);
    uint32_t SetResistanceEnable(void* deviceHandle, uint32_t canIndex, uint32_t enable);
    uint32_t SetBaudRateCustom(void* deviceHandle, uint32_t canIndex,
                               const char* rateCustom);
    uint32_t ClearFilter(void* channelHandle);
    uint32_t SetFilterMode(void* channelHandle, uint32_t mode);
    uint32_t SetFilterStartID(void* channelHandle, uint32_t startId);
    uint32_t SetFilterEndID(void* channelHandle, uint32_t endId);
    uint32_t AckFilter(void* channelHandle);

    // ---------------- 工具 ----------------

    /// 把真实 ID 合成 SocketCAN 风格高位标志(与 .NET MakeCanId 一致)
    static uint32_t MakeCanId(uint32_t id, bool eff, bool rtr, bool err);
    static uint32_t BuildCanBitrate(ChuangXinCanBitrate bitrate);
    static uint32_t BuildCanFdBitrate(ChuangXinCanFdBitrate bitrate);

    static const char* DefaultDllPath() {
        return ".\\HwDependency\\ChuangXin\\CANFD\\Win\\x64\\ControlCANFD.dll";
    }

private:
    ChuangXinCanFdApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};

    // (deviceType, deviceIndex) -> 共享设备句柄 + 引用计数
    struct DeviceEntry {
        void* handle;
        int refCount;
    };
    std::mutex deviceLock_;
    std::map<std::pair<uint32_t, uint32_t>, DeviceEntry> devices_;

    using OpenDeviceFn = void* (*)(uint32_t, uint32_t, uint32_t);
    using CloseDeviceFn = uint32_t (*)(void*);
    using InitCanFn = void* (*)(void*, uint32_t, const ZcanChannelInitConfig*);
    using ChannelFn = uint32_t (*)(void*);
    using TransmitFn = uint32_t (*)(void*, const void*, uint32_t);
    using ReceiveFn = uint32_t (*)(void*, void*, uint32_t, int);
    using DeviceUintFn = uint32_t (*)(void*, uint32_t, uint32_t);
    using DeviceStrFn = uint32_t (*)(void*, uint32_t, const char*);
    using ChannelUintFn = uint32_t (*)(void*, uint32_t);

    OpenDeviceFn pOpenDevice_ = nullptr;
    CloseDeviceFn pCloseDevice_ = nullptr;
    InitCanFn pInitCAN_ = nullptr;
    ChannelFn pStartCAN_ = nullptr;
    ChannelFn pResetCAN_ = nullptr;
    ChannelFn pClearBuffer_ = nullptr;
    TransmitFn pTransmit_ = nullptr;
    TransmitFn pTransmitFD_ = nullptr;
    ReceiveFn pReceive_ = nullptr;
    ReceiveFn pReceiveFD_ = nullptr;
    DeviceUintFn pSetAbitBaud_ = nullptr;
    DeviceUintFn pSetDbitBaud_ = nullptr;
    DeviceUintFn pSetCANFDStandard_ = nullptr;
    DeviceUintFn pSetResistanceEnable_ = nullptr;
    DeviceStrFn pSetBaudRateCustom_ = nullptr;
    ChannelFn pClearFilter_ = nullptr;
    ChannelUintFn pSetFilterMode_ = nullptr;
    ChannelUintFn pSetFilterStartID_ = nullptr;
    ChannelUintFn pSetFilterEndID_ = nullptr;
    ChannelFn pAckFilter_ = nullptr;
};

} // namespace chuangxin
} // namespace canhw
