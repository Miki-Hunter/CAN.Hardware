#pragma once
// ==================================================================
//  Vector XL Driver Library(vxlapi64.dll)动态绑定
//  (对应 .NET BaseLibDll/VectorCanLibDLL.cs)
//  结构体 pack=1,与 vxlapi.h / python-can 布局一致
//  (XLchannelConfig 大小 227、channelMask 偏移 42,见 static_assert)。
// ==================================================================
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include <canhw/VectorConfig.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace vxl {

// ---------------- 状态码 / 标志(对应 .NET 枚举) ----------------

using XLstatus = int16_t;
constexpr XLstatus SUCCESS = 0;
constexpr XLstatus ERR_QUEUE_IS_EMPTY = 10;
constexpr XLstatus ERR_INIT_ACCESS_MISSING = 158;
constexpr XLstatus ERR_CANNOT_OPEN_DRIVER = 201;
constexpr XLstatus ERR_DLL_NOT_FOUND = 203;

constexpr uint32_t XL_BUS_TYPE_CAN = 1;
constexpr uint32_t XL_INTERFACE_V3 = 3;
constexpr uint32_t XL_INTERFACE_V4 = 4;
constexpr uint32_t XL_ACTIVATE_RESET_CLOCK = 8;

constexpr uint32_t XL_CAN_EXT_MSG_ID = 0x80000000;  ///< 扩展帧 ID 标志
constexpr uint32_t XL_CAN_ID_MASK = 0x1FFFFFFF;     ///< ID 有效位掩码
constexpr uint8_t XL_OUTPUT_MODE_NORMAL = 1;

/// 经典 CAN 事件标签(XLevent.tag)
enum XLeventTag : uint8_t {
    XL_NO_COMMAND = 0,
    XL_RECEIVE_MSG = 1,
    XL_CHIP_STATE = 4,
    XL_TRANSCEIVER_INFO = 6,
    XL_TIMER = 8,
    XL_TRANSMIT_MSG = 10,
    XL_SYNC_PULSE = 11,
    XL_APPLICATION_NOTIFICATION = 15,
};

/// CAN FD 事件标签(XLcanRxEvent.tag / XLcanTxEvent.tag)
enum XLcanFdEventTag : uint16_t {
    EV_TAG_RX_OK = 0x0400,
    EV_TAG_RX_ERROR = 0x0401,
    EV_TAG_TX_ERROR = 0x0402,
    EV_TAG_TX_REQUEST = 0x0403,
    EV_TAG_TX_OK = 0x0404,
    EV_TAG_CHIP_STATE = 0x0409,
    EV_TAG_TX_MSG = 0x0440,
};

/// 经典 CAN 报文标志(XLevent.msgFlags)
namespace MsgFlags {
constexpr uint16_t NONE = 0;
constexpr uint16_t ERROR_FRAME = 0x01;
constexpr uint16_t OVERRUN = 0x02;
constexpr uint16_t NERR = 0x04;
constexpr uint16_t WAKEUP = 0x08;
constexpr uint16_t REMOTE_FRAME = 0x10;
constexpr uint16_t TX_COMPLETED = 0x40;
constexpr uint16_t TX_REQUEST = 0x80;
constexpr uint16_t SRR_BIT_DOM = 0x200;
} // namespace MsgFlags

/// CAN FD 发送标志(XLcanTxEvent.flags)
namespace TxMsgFlags {
constexpr uint32_t NONE = 0;
constexpr uint32_t EDL = 0x01;
constexpr uint32_t BRS = 0x02;
constexpr uint32_t RTR = 0x10;
constexpr uint32_t HIGHPRIO = 0x80;
constexpr uint32_t WAKEUP = 0x200;
} // namespace TxMsgFlags

/// CAN FD 接收标志(XLcanRxEvent.msgFlags)
namespace FdRxFlags {
constexpr uint32_t EDL = 0x01;
constexpr uint32_t BRS = 0x02;
constexpr uint32_t ESI = 0x04;
constexpr uint32_t RTR = 0x10;
constexpr uint32_t EF = 0x0200;
constexpr uint32_t ARB_LOST = 0x0400;
constexpr uint32_t WAKEUP = 0x2000;
constexpr uint32_t TE = 0x4000;
} // namespace FdRxFlags

// ---------------- 原生结构(pack=1) ----------------

/// 经典 CAN 自定义波特率参数(XLchipParams)
struct XLchipParams {
    uint32_t bitRate;
    uint8_t sjw;
    uint8_t tseg1;
    uint8_t tseg2;
    uint8_t sam;        ///< 1 或 3
    uint8_t outputMode;
};

/// 经典 CAN 事件(XLevent)
struct XLevent {
    uint8_t tag;
    uint8_t channelIndex;
    uint16_t transId;
    uint16_t portHandle;
    uint8_t flags;
    uint8_t reserved0;
    uint64_t timeStamp;  ///< 单位 ns
    uint32_t id;         ///< bit31=扩展帧标志
    uint16_t msgFlags;
    uint16_t dlc;
    uint64_t reserved1;
    uint64_t data;       ///< 8 字节数据,小端
    uint64_t reserved2;
};

/// CAN FD 配置(XLcanFdConf)
struct XLcanFdConf {
    uint32_t arbitrationBitRate;
    uint32_t sjwAbr;
    uint32_t tseg1Abr;
    uint32_t tseg2Abr;
    uint32_t dataBitRate;
    uint32_t sjwDbr;
    uint32_t tseg1Dbr;
    uint32_t tseg2Dbr;
    uint8_t reserved;
    uint8_t options;     ///< 0x08 = CANFD-BOSCH(non-ISO)
    uint16_t reserved1;
    uint32_t reserved2;
};

/// CAN FD 接收事件(XLcanRxEvent)
struct XLcanRxEvent {
    uint32_t size;       ///< 调用前须置为结构体大小
    uint16_t tag;
    uint16_t channelIndex;
    uint32_t userHandle;
    uint16_t flagsChip;
    uint16_t reserved0;
    uint64_t reserved1;
    uint64_t timeStamp;  ///< 单位 ns
    uint32_t canId;      ///< bit31=扩展帧标志
    uint32_t msgFlags;
    uint32_t crc;
    uint64_t reserved2;
    uint32_t reserved3;
    uint16_t totalBitCnt;
    uint8_t dlc;
    uint8_t reserved4;
    uint32_t reserved5;
    uint8_t data[64];
};

/// CAN FD 发送事件(XLcanTxEvent)
struct XLcanTxEvent {
    uint16_t tag;        ///< EV_TAG_TX_MSG = 0x0440
    uint16_t transId;    ///< 0xFFFF
    uint8_t channelIndex;
    uint8_t reserved0;
    uint8_t reserved1;
    uint8_t reserved2;
    uint32_t id;         ///< bit31=扩展帧标志
    uint32_t flags;      ///< TxMsgFlags
    uint8_t dlc;
    uint8_t reserved3[7];
    uint8_t data[64];
};

/// XL 通道配置(XLchannelConfig,pack=1,大小 227)
struct XLchannelConfig {
    uint8_t name[32];
    uint8_t hwType;
    uint8_t hwIndex;
    uint8_t hwChannel;
    uint16_t transceiverType;
    uint16_t transceiverState;
    uint16_t configError;
    uint8_t channelIndex;
    uint64_t channelMask;
    uint32_t channelCapabilities;
    uint32_t channelBusCapabilities;
    uint8_t isOnBus;
    uint32_t connectedBusType;
    uint32_t busType;
    uint8_t busParams[28];
    uint32_t doNotUse;
    uint32_t driverVersion;
    uint32_t interfaceVersion;
    uint32_t raw_data[10];
    uint32_t serialNumber;
    uint32_t articleNumber;
    uint8_t transceiverName[32];
    uint32_t specialCabFlags;
    uint32_t dominantTimeout;
    uint8_t dominantRecessiveDelay;
    uint8_t recessiveDominantDelay;
    uint8_t connectionInfo;
    uint8_t currentlyAvailableTimestamps;
    uint16_t minimalSupplyVoltage;
    uint16_t maximalSupplyVoltage;
    uint32_t maximalBaudrate;
    uint8_t fpgaCoreCapabilities;
    uint8_t specialDeviceStatus;
    uint16_t channelBusActiveCapabilities;
    uint16_t breakOffset;
    uint16_t delimiterOffset;
    uint32_t reserved[3];
};
static_assert(sizeof(XLchannelConfig) == 227, "XLchannelConfig layout mismatch");
static_assert(offsetof(XLchannelConfig, channelMask) == 42, "channelMask offset");

/// XL 驱动配置(XLdriverConfig,pack=1)
struct XLdriverConfig {
    uint32_t dllVersion;
    uint32_t channelCount;
    uint32_t reserved[10];
    XLchannelConfig channel[64];
};

/// 经典 CAN 总线参数读回结果(诊断/校验用)
struct XLcanBusParamsInfo {
    std::string channelName;
    uint32_t bitRate = 0;
    uint8_t sjw = 0;
    uint8_t tseg1 = 0;
    uint8_t tseg2 = 0;
    uint8_t sam = 0;
    uint8_t outputMode = 0;
    uint8_t canOpMode = 0;
};

class VectorApi {
public:
    static VectorApi& Instance();

    bool Load();
    bool IsLoaded() const { return loaded_; }
    void EnsureLoaded();

    // 驱动打开/关闭(引用计数,多 Wrapper 实例共享)
    int16_t OpenDriver();
    void CloseDriver();

    /// 按硬件类型+硬件通道号在驱动配置中定位通道(须已 OpenDriver)
    bool TryGetChannel(VectorHwType hwType, uint32_t hwChannel,
                       XLchannelConfig& channelConfig);
    /// 枚举指定硬件型号可用 CAN 通道的诊断文本(用于错误提示)
    std::string GetCanChannelsText(VectorHwType hwType);
    /// 按 channelMask 读回通道当前生效的经典 CAN 总线参数(须已 OpenDriver)
    bool TryReadBusParams(uint64_t channelMask, XLcanBusParamsInfo& info);

    int16_t SetApplConfig(const char* appName, uint32_t appChannel, uint64_t hwType,
                          uint32_t hwIndex, uint32_t hwChannel, uint32_t busType);
    int16_t OpenPort(int32_t* portHandle, const char* userName, uint64_t accessMask,
                     uint64_t* permissionMask, uint32_t rxQueueSize,
                     uint32_t interfaceVersion, uint32_t busType);
    int16_t ClosePort(int32_t portHandle);
    int16_t ActivateChannel(int32_t portHandle, uint64_t accessMask, uint32_t busType,
                            uint32_t flags);
    int16_t DeactivateChannel(int32_t portHandle, uint64_t accessMask);
    int16_t CanSetChannelBitrate(int32_t portHandle, uint64_t accessMask,
                                 uint32_t bitrate);
    int16_t CanSetChannelParams(int32_t portHandle, uint64_t accessMask,
                                const XLchipParams* chipParams);
    int16_t CanSetAcceptanceFilter(int32_t portHandle, uint64_t accessMask,
                                   uint32_t code, uint32_t mask, uint32_t idRange);
    int16_t CanSetChannelMode(int32_t portHandle, uint64_t accessMask, int32_t tx,
                              int32_t txrq);
    int16_t CanFdSetConfiguration(int32_t portHandle, uint64_t accessMask,
                                  const XLcanFdConf* fdConf);
    int16_t Receive(int32_t portHandle, uint32_t* eventCount, XLevent* events);
    int16_t CanReceive(int32_t portHandle, XLcanRxEvent* rxEvent);
    int16_t CanTransmit(int32_t portHandle, uint64_t accessMask, uint32_t* messageCount,
                        XLevent* messages);
    int16_t CanTransmitEx(int32_t portHandle, uint64_t accessMask, uint32_t msgCnt,
                          uint32_t* msgCntSent, XLcanTxEvent* messages);
    int16_t FlushReceiveQueue(int32_t portHandle);
    int16_t CanFlushTransmitQueue(int32_t portHandle, uint64_t accessMask);
    int16_t CanRequestChipState(int32_t portHandle, uint64_t accessMask);

    // ---------------- 工具 ----------------

    /// 按标准仲裁段/数据段波特率构建 CAN FD 配置(内置时序表,80MHz 时钟)
    static XLcanFdConf BuildFdConf(uint32_t arbitrationBitrate, uint32_t dataBitrate,
                                   bool nonIso);
    /// 按自定义寄存器参数构建 CAN FD 配置
    static XLcanFdConf BuildFdConfCustom(uint32_t arbitrationBitrate, int arbitrationTseg1,
                                         int arbitrationTseg2, int arbitrationSjw,
                                         uint32_t dataBitrate, int dataTseg1,
                                         int dataTseg2, int dataSjw, bool nonIso);

    /// 状态码可读文本
    static std::string StatusText(int16_t status);

    static const char* DefaultDllPath() {
        return ".\\HwDependency\\Vector\\Generic\\Win\\x64\\vxlapi64.dll";
    }

private:
    VectorApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};

    std::mutex initLock_;
    int32_t refCount_ = 0;

    using XlStatusFn = int16_t (*)();
    using GetDriverConfigFn = int16_t (*)(XLdriverConfig*);
    using SetApplConfigFn = int16_t (*)(const char*, uint32_t, uint64_t, uint32_t,
                                        uint32_t, uint32_t);
    using OpenPortFn = int16_t (*)(int32_t*, const char*, uint64_t, uint64_t*, uint32_t,
                                   uint32_t, uint32_t);
    using ClosePortFn = int16_t (*)(int32_t);
    using ActivateFn = int16_t (*)(int32_t, uint64_t, uint32_t, uint32_t);
    using DeactivateFn = int16_t (*)(int32_t, uint64_t);
    using SetBitrateFn = int16_t (*)(int32_t, uint64_t, uint32_t);
    using SetParamsFn = int16_t (*)(int32_t, uint64_t, const XLchipParams*);
    using SetFilterFn = int16_t (*)(int32_t, uint64_t, uint32_t, uint32_t, uint32_t);
    using SetModeFn = int16_t (*)(int32_t, uint64_t, int32_t, int32_t);
    using FdSetConfFn = int16_t (*)(int32_t, uint64_t, const XLcanFdConf*);
    using ReceiveFn = int16_t (*)(int32_t, uint32_t*, XLevent*);
    using CanReceiveFn = int16_t (*)(int32_t, XLcanRxEvent*);
    using TransmitFn = int16_t (*)(int32_t, uint64_t, uint32_t*, XLevent*);
    using TransmitExFn = int16_t (*)(int32_t, uint64_t, uint32_t, uint32_t*,
                                    XLcanTxEvent*);
    using FlushFn = int16_t (*)(int32_t);
    using FlushTxFn = int16_t (*)(int32_t, uint64_t);
    using ChipStateFn = int16_t (*)(int32_t, uint64_t);

    XlStatusFn pOpenDriver_ = nullptr;
    XlStatusFn pCloseDriver_ = nullptr;
    GetDriverConfigFn pGetDriverConfig_ = nullptr;
    SetApplConfigFn pSetApplConfig_ = nullptr;
    OpenPortFn pOpenPort_ = nullptr;
    ClosePortFn pClosePort_ = nullptr;
    ActivateFn pActivateChannel_ = nullptr;
    DeactivateFn pDeactivateChannel_ = nullptr;
    SetBitrateFn pCanSetChannelBitrate_ = nullptr;
    SetParamsFn pCanSetChannelParams_ = nullptr;
    SetFilterFn pCanSetAcceptanceFilter_ = nullptr;
    SetModeFn pCanSetChannelMode_ = nullptr;
    FdSetConfFn pCanFdSetConfiguration_ = nullptr;
    ReceiveFn pReceive_ = nullptr;
    CanReceiveFn pCanReceive_ = nullptr;
    TransmitFn pCanTransmit_ = nullptr;
    TransmitExFn pCanTransmitEx_ = nullptr;
    FlushFn pFlushReceiveQueue_ = nullptr;
    FlushTxFn pCanFlushTransmitQueue_ = nullptr;
    ChipStateFn pCanRequestChipState_ = nullptr;
};

} // namespace vxl
} // namespace canhw
