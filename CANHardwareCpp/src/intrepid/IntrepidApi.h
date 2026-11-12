#pragma once
// ==================================================================
//  Intrepid neoVI API(icsneo40.dll)动态绑定
//  (对应 .NET BaseLibDll/IntrepidIcsneoLibDLL.cs)
//  icsneo40.dll 随 Intrepid 驱动(Vehicle Spy 3 / Intrepid Product
//  Drivers / neoAPI)安装到系统目录,用裸 DLL 名由系统搜索路径解析,
//  调用约定 StdCall(x64 下统一)。
//  收发消息结构 icsSpyMessage(x64 布局 72 字节):8 字节内联数据场 +
//  ExtraDataPtr 外挂指针场,CAN FD 超过 8 字节走 icsneoTxMessagesEx。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <canhw/Sync.h>
#include <string>

#include <canhw/IntrepidConfig.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace intrepid {

#ifdef _WIN32
#define CANHW_IC_STDCALL __stdcall
#else
#define CANHW_IC_STDCALL
#endif

// ---------------- NetworkIDs(icsnVC40.h 的 NETID_*,仅保留 CAN 封装用到的) ----------------
constexpr int32_t NETID_DEVICE = 0;
constexpr int32_t NETID_HSCAN1 = 1;
constexpr int32_t NETID_HSCAN2 = 42;
constexpr int32_t NETID_HSCAN3 = 44;
constexpr int32_t NETID_HSCAN4 = 61;
constexpr int32_t NETID_HSCAN5 = 62;
constexpr int32_t NETID_HSCAN6 = 96;
constexpr int32_t NETID_HSCAN7 = 97;

/// 通道号(0 基) -> 设备网络 ID(HSCAN1~HSCAN7)
inline int32_t ChannelToNetworkId(uint32_t channel) {
    static constexpr int32_t kChannelMap[7] = {
        NETID_HSCAN1, NETID_HSCAN2, NETID_HSCAN3, NETID_HSCAN4,
        NETID_HSCAN5, NETID_HSCAN6, NETID_HSCAN7};
    return channel < 7 ? kChannelMap[channel] : -1;
}

// ---------------- StatusBits(icsSpyMessage.StatusBitField) ----------------
namespace StatusBits {
constexpr uint32_t NONE = 0;
constexpr uint32_t GLOBAL_ERR = 0x01;
constexpr uint32_t TX_MSG = 0x02;              ///< 发送(回显)消息
constexpr uint32_t XTD_FRAME = 0x04;           ///< 扩展帧(29-bit)
constexpr uint32_t REMOTE_FRAME = 0x08;        ///< 远程帧
constexpr uint32_t CRC_ERROR = 0x10;
constexpr uint32_t CAN_ERROR_PASSIVE = 0x20;
constexpr uint32_t INCOMPLETE_FRAME = 0x40;
constexpr uint32_t LOST_ARBITRATION = 0x80;
constexpr uint32_t UNDEFINED_ERROR = 0x100;
constexpr uint32_t CAN_BUS_OFF = 0x200;
constexpr uint32_t CAN_ERROR_WARNING = 0x400;
constexpr uint32_t BUS_SHORTED_PLUS = 0x800;
constexpr uint32_t BUS_SHORTED_GND = 0x1000;
constexpr uint32_t CHECKSUM_ERROR = 0x2000;
constexpr uint32_t BAD_MESSAGE_BIT_TIME_ERROR = 0x4000;
constexpr uint32_t IFR_DATA = 0x8000;
constexpr uint32_t COMM_IN_OVERFLOW = 0x10000;  ///< 接收溢出
constexpr uint32_t COMM_OUT_OVERFLOW = 0x20000;
constexpr uint32_t COMM_MISC_ERROR = 0x40000;
constexpr uint32_t BREAK = 0x80000;
constexpr uint32_t INIT_MESSAGE = 0x20000000;
constexpr uint32_t HIGH_SPEED = 0x40000000;
constexpr uint32_t EXTENDED = 0x80000000;       ///< 置位时 StatusBitField3 有效
} // namespace StatusBits

// StatusBitField2(仅保留 CAN 相关位)
namespace StatusBits2 {
constexpr uint32_t NONE = 0;
constexpr uint32_t ERROR_FRAME = 0x20000;  ///< CAN 错误帧
} // namespace StatusBits2

// StatusBitField3(CAN FD 属性位)
namespace StatusBits3 {
constexpr uint32_t NONE = 0;
constexpr uint32_t CANFD_ESI = 0x01;  ///< 发送节点错误被动
constexpr uint32_t CANFD_IDE = 0x02;
constexpr uint32_t CANFD_RTR = 0x04;
constexpr uint32_t CANFD_FDF = 0x08;  ///< CAN FD 帧
constexpr uint32_t CANFD_BRS = 0x10;  ///< 数据段速率切换
} // namespace StatusBits3

// ---------------- 消息协议 ----------------
namespace Protocol {
constexpr uint8_t CUSTOM = 0;
constexpr uint8_t CAN = 1;
constexpr uint8_t LIN = 12;
constexpr uint8_t CANFD = 30;
} // namespace Protocol

// ---------------- 原生结构(自然对齐,与 icsnVC40.h 一致) ----------------

/// 设备类型(FindDevices 过滤器,0 表示全部)
using DeviceTypes = uint32_t;

/// NeoDevice
struct NeoDevice {
    uint32_t deviceType;
    int32_t handle;
    int32_t numberOfClients;
    int32_t serialNumber;
    int32_t maxAllowedClients;
};

/// NeoDeviceEx
struct NeoDeviceEx {
    NeoDevice neoDevice;
    uint32_t firmwareMajor;
    uint32_t firmwareMinor;
    uint32_t status;  ///< bit1=CoreMini Running, bit2=In BootLoader
    uint32_t options;
    void* pAvailWIFINetwork;
    void* pWIFIInterfaceInfo;
    int32_t isEthernetDevice;
    uint8_t macAddress[6];
    uint16_t hardwareRev;
    uint16_t revReserved;
    uint32_t reserved[6];
};

/// OptionsNeoEx
struct OptionsNeoEx {
    int32_t canOptions;
    int32_t reserved[15];
};

/// icsSpyMessage(x64 布局 72 字节,与 icsnVC40.h 的 _icsSpyMessage 字段一一对应)
struct SpyMessage {
    uint32_t statusBitField;    ///< StatusBits
    uint32_t statusBitField2;   ///< StatusBits2
    int32_t timeHardware;       ///< 硬件时间戳低位(×1.6µs)
    int32_t timeHardware2;      ///< 硬件时间戳高位(×0.1048576s)
    int32_t timeSystem;
    int32_t timeSystem2;
    uint8_t timeStampHardwareID;
    uint8_t timeStampSystemID;
    uint8_t networkID;
    uint8_t nodeID;
    uint8_t protocol;           ///< Protocol
    uint8_t messagePieceID;
    uint8_t extraDataPtrEnabled;///< 置 1 时 iExtraDataPtr 指向外挂数据场
    uint8_t numberBytesHeader;
    uint8_t numberBytesData;    ///< 数据字节数(直接是长度,非 DLC 编码)
    uint8_t networkID2;         ///< 扩展网络 ID(NetworkID + NetworkID2*100)
    int16_t descriptionID;
    uint32_t arbIDOrHeader;     ///< 29-bit CAN 标识符
    uint64_t data;              ///< 内联 8 字节数据场(小端:byte[0] 为 LSB)
    uint32_t statusBitField3;   ///< StatusBits3
    int32_t statusBitField4;
    void* iExtraDataPtr;        ///< CAN FD >8 字节时的外挂数据指针
    uint8_t miscData;
    uint8_t reserved0;
    uint8_t reserved1;
    uint8_t reserved2;
};
static_assert(sizeof(SpyMessage) == 72, "SpyMessage x64 layout mismatch");

class IntrepidApi {
public:
    static IntrepidApi& Instance();

    void EnsureLoaded();
    bool IsLoaded() const { return loaded_; }

    int32_t FindDevices(NeoDeviceEx* pNeoDevice, int32_t* pNumDevices,
                        uint32_t deviceTypes, uint32_t numDeviceTypes,
                        OptionsNeoEx* pOptions, uint32_t reserved);
    int32_t OpenNeoDevice(NeoDevice* pNeoDevice, void** hObject,
                          uint8_t* bNetworkIDs, int32_t bConfigRead,
                          int32_t bSyncToPC);
    int32_t ClosePort(void* hObject, int32_t* pNumberOfErrors);
    int32_t SetBitRate(void* hObject, int32_t bitRate, int32_t networkId);
    int32_t SetFDBitRate(void* hObject, int32_t bitRate, int32_t networkId);
    int32_t GetMessages(void* hObject, SpyMessage* pMsg, int32_t* pNumberOfMessages,
                        int32_t* pNumberOfErrors);
    int32_t TxMessages(void* hObject, const SpyMessage* pMsg, int32_t networkId,
                       int32_t numMessages);
    int32_t TxMessagesEx(void* hObject, const SpyMessage* pMsg, int32_t networkId,
                         int32_t numMessages, uint32_t* numTxed, uint32_t reserved);
    int32_t WaitForRxMessagesWithTimeOut(void* hObject, uint32_t timeoutMs);

    /// 查找第一台已连接设备并打开(bConfigRead=1,DLL 先读出设备配置)
    void* OpenFirstDevice();

    /// 硬件时间戳(双 32 位)换算为微秒
    static uint64_t GetTimestampUs(int32_t timeHardware, int32_t timeHardware2);

    static const char* DefaultDllPath() { return "icsneo40.dll"; }

private:
    IntrepidApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};
    canhw::Mutex loadLock_;

    using FindDevicesFn = int32_t CANHW_IC_STDCALL (*)(NeoDeviceEx*, int32_t*, uint32_t,
                                                       uint32_t, OptionsNeoEx*, uint32_t);
    using OpenNeoDeviceFn = int32_t CANHW_IC_STDCALL (*)(NeoDevice*, void**, uint8_t*,
                                                         int32_t, int32_t);
    using ClosePortFn = int32_t CANHW_IC_STDCALL (*)(void*, int32_t*);
    using SetBitRateFn = int32_t CANHW_IC_STDCALL (*)(void*, int32_t, int32_t);
    using GetMessagesFn = int32_t CANHW_IC_STDCALL (*)(void*, SpyMessage*, int32_t*,
                                                       int32_t*);
    using TxMessagesFn = int32_t CANHW_IC_STDCALL (*)(void*, const SpyMessage*,
                                                      int32_t, int32_t);
    using TxMessagesExFn = int32_t CANHW_IC_STDCALL (*)(void*, const SpyMessage*,
                                                        int32_t, int32_t, uint32_t*,
                                                        uint32_t);
    using WaitForRxFn = int32_t CANHW_IC_STDCALL (*)(void*, uint32_t);

    FindDevicesFn pFindDevices_ = nullptr;
    OpenNeoDeviceFn pOpenNeoDevice_ = nullptr;
    ClosePortFn pClosePort_ = nullptr;
    SetBitRateFn pSetBitRate_ = nullptr;
    SetBitRateFn pSetFDBitRate_ = nullptr;
    GetMessagesFn pGetMessages_ = nullptr;
    TxMessagesFn pTxMessages_ = nullptr;
    TxMessagesExFn pTxMessagesEx_ = nullptr;
    WaitForRxFn pWaitForRx_ = nullptr;
};

} // namespace intrepid
} // namespace canhw
