#pragma once
// ==================================================================
//  EbEl(Eberspächer Electronics)FlexCard API(fcbase.dll)动态绑定
//  (对应 .NET BaseLibDll/EbElFcbaseLibDLL.cs)
//  fcbase.dll 随 FlexCard 驱动安装进系统(DLL 搜索路径),用裸 DLL 名解析,
//  9 个导出函数全部桥接(调用约定 StdCall,x64 下统一)。
//  注意:该 API 只有经典 CAN(FlexCard 无 CAN FD);
//  发送是"预配置报文缓冲"模型(fcbCANTransmit 只给缓冲号 + 数据,不带 CAN ID)。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <canhw/EbElConfig.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace ebel {

#ifdef _WIN32
#define CANHW_FC_STDCALL __stdcall
#else
#define CANHW_FC_STDCALL
#endif

/// FlexCard 原生扩展帧标记(bit29),与托管层 SocketCAN 风格 bit31 不同,需双向换算
constexpr uint32_t FC_EXT_FLAG = 0x20000000;

using FcResult = int32_t;
constexpr FcResult FC_OK = 0;

/// fcbase 返回码(仅保留常用值,完整序号与 .NET fcResult 一致)
namespace FcResultText {
constexpr FcResult INTERNAL_ERROR = 99;
constexpr FcResult INVALID_PARAMETER = 102;
constexpr FcResult INVALID_OBJECT_HANDLE = 105;
constexpr FcResult UNKOWN_FLEXCARD_ID = 113;
constexpr FcResult CC_INDEX_NOT_VALID = 124;
} // namespace FcResultText

/// 报文包类型(fcbReceive 链表节点 fcPacket.Type,C 枚举按 int 编组)
enum FcPacketType : int32_t {
    PKT_INFO = 0,
    PKT_FLEXRAY_FRAME = 1,
    PKT_ERROR = 2,
    PKT_STATUS = 3,
    PKT_TRIGGER = 4,
    PKT_TX_ACKNOWLEDGE = 5,
    PKT_NM_VECTOR = 6,
    PKT_NOTIFICATION = 7,
    PKT_TRIGGER_EX = 8,
    PKT_CAN = 9,
    PKT_CAN_ERROR = 10,
};

/// 通信控制器通道号(CC1~CC8),配置里的 channel(0 基)即该枚举值(C 枚举按 int 编组)
enum FcCC : int32_t {
    CC1 = 0, CC2, CC3, CC4, CC5, CC6, CC7, CC8,
};

/// fcCANPacket.FlagsAndDLC 字节位表
namespace DlcFlags {
constexpr uint8_t Direction = 0x10;    ///< 0=接收, 1=发送确认(TxAck)
constexpr uint8_t RemoteFrame = 0x20;
constexpr uint8_t MessageLost = 0x40;
constexpr uint8_t Reserved = 0x80;
} // namespace DlcFlags

/// fcFreeMemory 的内存类型
enum FcMemoryType : int32_t {
    MEM_STRING = 0,
    MEM_INFO = 1,
    MEM_PACKET = 2,
    MEM_INFO_V2 = 3,
    MEM_INFO_HWSW = 4,
};

/// CAN 监控模式(本封装只用 Normal)
enum FcCANMonitoringMode : int32_t {
    MODE_NORMAL = 0,
    MODE_SILENT = 1,
};

/// FlexCard 设备型号(fcbGetEnumFlexCardsV3 枚举结果)
enum FcFlexCardDeviceId : int32_t {
    DEV_NO_DEVICE = 0,
    DEV_FLEXCARD_CYCLONE_II = 5,
    DEV_FLEXCARD_USB = 14,
};

// ---------------- 原生结构(Pack=1,与 .NET 编组一致) ----------------

/// CAN 报文(22 字节):fcbReceive 链表中 pCANPacket 指向的负载
struct FcCanPacket {
    uint32_t id;           ///< 11/29 位 CAN ID,扩展帧用 bit29 标记
    uint32_t timeStamp;    ///< 硬件时间戳(原始 tick,单位未公开,透传不换算)
    uint8_t bufferNumber;
    uint8_t flagsAndDlc;   ///< bit0-3=DLC, bit4=Direction, bit5=RTR, bit6=MessageLost
    int32_t cc;            ///< FcCC(C 枚举按 int 编组)
    uint8_t data[8];
};
static_assert(sizeof(FcCanPacket) == 22, "fcCANPacket layout mismatch");

/// 接收链表节点(x64 20 字节):fcbReceive 返回一串,pNextPacket 链接
struct FcPacket {
    int32_t type;          ///< FcPacketType
    void* pCANPacket;
    void* pNextPacket;
};
static_assert(sizeof(FcPacket) == 20, "fcPacket x64 layout mismatch");

struct FcVersionNumber {
    uint32_t major;
    uint32_t minor;
    uint32_t update;
    uint32_t release;
};

struct FcInfoSw {
    FcVersionNumber versionBaseDll;
    FcVersionNumber versionDeviceDriver;
    uint32_t licenseFlags;
    uint32_t reserved[4];
};

/// 各类总线控制器数量
struct FcNumberCc {
    uint8_t flexRay;
    uint8_t can;
    uint8_t lin;
    uint8_t most;
    uint8_t flexRaySelfSync;
    uint8_t reserved[3];
};

struct FcInfoHw {
    uint64_t serial;
    int32_t deviceId;  ///< FcFlexCardDeviceId
    FcVersionNumber versionFirmware;
    FcVersionNumber versionHardware;
    FcNumberCc supportedCCs;
    FcNumberCc licensedCCs;
    FcNumberCc useableCCs;
    void* pVersionCC;
    uint32_t reserved[8];
};

/// 设备枚举链表节点:fcbGetEnumFlexCardsV3 返回一串,pNext 链接
struct FcInfoHwSw {
    uint32_t flexCardId;
    uint32_t userDefinedCardId;
    FcInfoSw infoSoftware;
    FcInfoHw infoHardware;
    uint32_t busy;
    void* pNext;
    uint32_t reserved[2];
};

/// CAN 位时序配置(36 字节):波特率只能靠"分频器 + 位段"手工组
struct FcCANCcConfig {
    uint16_t baudRatePrescaler;
    uint16_t synchronizationJumpWidth;
    uint16_t timeSegment1;
    uint16_t timeSegment2;
    uint32_t enableAutomaticRetransmission;  ///< 1=使能自动重发
    uint32_t reserved[6];
};
static_assert(sizeof(FcCANCcConfig) == 36, "fcCANCcConfig layout mismatch");

class EbElApi {
public:
    static EbElApi& Instance();

    void EnsureLoaded();
    bool IsLoaded() const { return loaded_; }

    /// 枚举本机 FlexCard 设备,返回第一块带 CAN 控制器的卡 ID(0 表示未找到)。
    /// 驱动返回 fcInfoHwSw 链表,用完必须 fcFreeMemory(InfoHwSw) 归还。
    uint32_t FindFirstCanCard();
    FcResult Open(uint32_t flexCardId, void** handle);
    FcResult Close(void* handle);
    FcResult MonitoringStart(void* handle, int32_t cc);
    FcResult MonitoringStop(void* handle, int32_t cc);
    /// fcbCANTransmit:缓冲式发送(只给缓冲号 + 数据,CAN ID 由报文缓冲的既有配置决定)
    FcResult Transmit(void* handle, int32_t cc, uint32_t bufferNumber,
                      const uint8_t* data, uint8_t len);
    /// fcbReceive:取一串报文包(链表),无报文时 pPacket 为 nullptr;
    /// 取得后由调用方 FreePacket 归还
    FcResult Receive(void* handle, void** pPacket);
    /// 归还 fcbReceive 取得的报文包链表
    void FreePacket(void* pPacket);
    FcResult SetCcConfiguration(void* handle, int32_t cc, const FcCANCcConfig& cfg);

    static std::string ResultText(FcResult result);

    static const char* DefaultDllPath() { return "fcbase.dll"; }

private:
    EbElApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};
    std::mutex loadLock_;

    using GetEnumFlexCardsV3Fn = FcResult CANHW_FC_STDCALL (*)(void**, uint8_t);
    using FreeMemoryFn = FcResult CANHW_FC_STDCALL (*)(int32_t, void*);
    using OpenFn = FcResult CANHW_FC_STDCALL (*)(void**, uint32_t);
    using CloseFn = FcResult CANHW_FC_STDCALL (*)(void*);
    using MonitoringStartFn = FcResult CANHW_FC_STDCALL (*)(void*, int32_t, uint8_t,
                                                            int32_t);
    using MonitoringStopFn = FcResult CANHW_FC_STDCALL (*)(void*, int32_t);
    using TransmitFn = FcResult CANHW_FC_STDCALL (*)(void*, int32_t, uint32_t,
                                                     const uint8_t*, uint8_t, uint8_t);
    using ReceiveFn = FcResult CANHW_FC_STDCALL (*)(void*, void**);
    using SetCcConfigurationFn = FcResult CANHW_FC_STDCALL (*)(void*, int32_t,
                                                              FcCANCcConfig);

    GetEnumFlexCardsV3Fn pGetEnumFlexCardsV3_ = nullptr;
    FreeMemoryFn pFreeMemory_ = nullptr;
    OpenFn pOpen_ = nullptr;
    CloseFn pClose_ = nullptr;
    MonitoringStartFn pMonitoringStart_ = nullptr;
    MonitoringStopFn pMonitoringStop_ = nullptr;
    TransmitFn pTransmit_ = nullptr;
    ReceiveFn pReceive_ = nullptr;
    SetCcConfigurationFn pSetCcConfiguration_ = nullptr;
};

} // namespace ebel
} // namespace canhw
