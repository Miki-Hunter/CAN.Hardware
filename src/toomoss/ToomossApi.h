#pragma once
// ==================================================================
//  图莫斯(Toomoss)USB2XXX:USB2XXX.dll 动态绑定
//  (对应 .NET BaseLibDll/ToomossLibDLL.cs)
//  同一 USB2XXX.dll 同时提供经典 CAN(CAN_*)与 CANFD(CANFD_*)两套接口。
//  设备经 USB_ScanDevice 扫描,同一物理设备多通道共享一次 Open,引用计数。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <map>
#include <canhw/Sync.h>
#include <string>
#include <vector>

#include <canhw/ToomossConfig.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace toomoss {

constexpr int CAN_SUCCESS = 0;
constexpr int CANFD_SUCCESS = 0;

constexpr uint8_t CAN_MSG_REMOTE_RTR = 0x01;
constexpr uint8_t CAN_MSG_REMOTE_TX = 0x80;
constexpr uint8_t CAN_MODE_RESISTOR = 0x80;

constexpr uint32_t CANFD_IDE_FLAG = 0x80000000;
constexpr uint32_t CANFD_RTR_FLAG = 0x40000000;
constexpr uint32_t CANFD_ID_MASK = 0x1FFFFFFF;

constexpr uint8_t CANFD_FLAG_BRS = 0x01;
constexpr uint8_t CANFD_FLAG_ESI = 0x02;
constexpr uint8_t CANFD_FLAG_FDF = 0x04;
constexpr uint8_t CANFD_FLAG_TX = 0x80;

// ---------------- 原生结构(自然对齐,与 USB2XXX 头一致) ----------------

/// 经典 CAN 报文(CAN_MSG)
struct CanMsg {
    uint32_t id;          ///< 裸 ID
    uint32_t timeStamp;   ///< 时间戳低 32 位,单位 100µs
    uint8_t remoteFlag;   ///< bit0=RTR, bit6..5=通道号, bit7=发送帧标志
    uint8_t externFlag;   ///< 是否扩展帧(0x01=EXT, 0x80=ERR)
    uint8_t dataLen;      ///< 数据长度(<=8)
    uint8_t data[8];
    uint8_t timeStampHigh;///< 时间戳高位,单位 100µs
};

/// 经典 CAN 初始化配置(CAN_INIT_CONFIG)
struct CanInitConfig {
    uint32_t canBrp;  ///< 1~1024
    uint8_t canSjw;   ///< 1~4
    uint8_t canBs1;   ///< 1~16
    uint8_t canBs2;   ///< 1~8
    uint8_t canMode;  ///< 0正常/1环回/2静默/3静默环回,bit7=内部120Ω
    uint8_t canAbom;  ///< 自动离线管理
    uint8_t canNart;  ///< 0使能重传,1禁止重传
    uint8_t canRflm;  ///< 0覆盖旧报文,1丢弃新报文
    uint8_t canTxfp;  ///< 0 ID优先级,1请求顺序优先级
};

/// 经典 CAN 过滤器配置(CAN_FILTER_CONFIG)
struct CanFilterConfig {
    uint8_t enable;
    uint8_t filterIndex;  ///< 0~13
    uint8_t filterMode;   ///< 0屏蔽位模式,1标识符列表模式
    uint8_t extFrame;     ///< 1=扩展帧,0=标准帧
    uint32_t idStdExt;
    uint32_t idIde;
    uint32_t idRtr;
    uint32_t maskStdExt;  ///< 对应位为 1 则需要比较
    uint32_t maskIde;
    uint32_t maskRtr;
};

/// CANFD 报文(CANFD_MSG,Pack=1)
struct CanFdMsg {
    uint32_t id;            ///< bit31=IDE, bit30=RTR, bit28..0=ID
    uint8_t dlc;            ///< 实际字节数 0~64
    uint8_t flags;          ///< bit0=BRS, bit1=ESI, bit2=FDF, bit6..5=通道, bit7=发送帧标志
    uint8_t res0;
    uint8_t timeStampHigh;  ///< 时间戳高位,单位 10µs
    uint32_t timeStamp;     ///< 时间戳低 32 位,单位 10µs
    uint8_t data[64];
};

/// CANFD 初始化配置(CANFD_INIT_CONFIG,Pack=1)
struct CanFdInitConfig {
    uint8_t mode;         ///< 0正常,1自发自收
    uint8_t isoCrcEnable; ///< 0禁止ISO CRC,1使能
    uint8_t retrySend;    ///< 0禁止重发,1无限制重发
    uint8_t resEnable;    ///< 0不接入内部120Ω,1接入
    uint8_t nbtBrp;
    uint8_t nbtSeg1;
    uint8_t nbtSeg2;
    uint8_t nbtSjw;
    uint8_t dbtBrp;
    uint8_t dbtSeg1;
    uint8_t dbtSeg2;
    uint8_t dbtSjw;
    uint8_t res0[8];
};

/// CANFD 过滤器配置(CANFD_FILTER_CONFIG)
struct CanFdFilterConfig {
    uint8_t enable;
    uint8_t index;  ///< 0~31
    uint8_t res0;
    uint8_t res1;
    uint32_t idAccept;  ///< bit28..0=ID, bit31=IDE
    uint32_t idMask;    ///< 对应位为 1 则需要比较
};

class ToomossApi {
public:
    static ToomossApi& Instance();

    bool Load();
    bool IsLoaded() const { return loaded_; }
    void EnsureLoaded();

    /// 扫描并打开设备(同一物理设备的多通道共享一次 Open)
    bool OpenDevice(uint32_t deviceIndex, int32_t& devHandle);
    /// 关闭设备(引用计数归零时才真正 USB_CloseDevice)
    void CloseDevice(int32_t devHandle);

    // ---- 经典 CAN(CAN_*) ----
    int32_t CanGetSpeedArg(int32_t devHandle, CanInitConfig* config, uint32_t speedBps);
    int32_t CanInit(int32_t devHandle, uint8_t channel, const CanInitConfig* config);
    int32_t CanFilterInit(int32_t devHandle, uint8_t channel,
                          const CanFilterConfig* filter);
    int32_t CanStartGetMsg(int32_t devHandle, uint8_t channel);
    int32_t CanStopGetMsg(int32_t devHandle, uint8_t channel);
    int32_t CanStop(int32_t devHandle, uint8_t channel);
    int32_t CanClearMsg(int32_t devHandle, uint8_t channel);
    int32_t CanSendMsg(int32_t devHandle, uint8_t channel, const CanMsg* msg,
                       uint32_t num);
    int32_t CanGetMsgWithSize(int32_t devHandle, uint8_t channel, void* buffer,
                              int32_t bufferSize);

    // ---- CAN FD(CANFD_*) ----
    int32_t CanFdGetSpeedArg(int32_t devHandle, CanFdInitConfig* config,
                             uint32_t speedNbt, uint32_t speedDbt);
    int32_t CanFdInit(int32_t devHandle, uint8_t channel,
                      const CanFdInitConfig* config);
    int32_t CanFdStartGetMsg(int32_t devHandle, uint8_t channel);
    int32_t CanFdStopGetMsg(int32_t devHandle, uint8_t channel);
    int32_t CanFdStop(int32_t devHandle, uint8_t channel);
    int32_t CanFdClearMsg(int32_t devHandle, uint8_t channel);
    int32_t CanFdSendMsg(int32_t devHandle, uint8_t channel, const void* msg,
                         uint32_t num);
    int32_t CanFdGetMsg(int32_t devHandle, uint8_t channel, void* buffer,
                        int32_t bufferSize);
    int32_t CanFdSetFilter(int32_t devHandle, uint8_t channel,
                           const CanFdFilterConfig* filter, uint8_t len);

    static const char* DefaultDllPath() {
        return ".\\HwDependency\\Toomoss\\Generic\\Win\\x64\\USB2XXX.dll";
    }

private:
    ToomossApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};

    // USB_ScanDevice 一次最多扫描的设备数量
    static constexpr size_t MaxDevices = 64;

    canhw::Mutex deviceLock_;
    std::vector<int32_t> scannedHandles_;
    std::map<int32_t, int> refCounts_;

    using ScanFn = int32_t (*)(int32_t*);
    using OpenFn = uint8_t (*)(int32_t);
    using CloseFn = uint8_t (*)(int32_t);
    using SpeedArgFn = int32_t (*)(int32_t, CanInitConfig*, uint32_t);
    using InitFn = int32_t (*)(int32_t, uint8_t, const CanInitConfig*);
    using FilterInitFn = int32_t (*)(int32_t, uint8_t, const CanFilterConfig*);
    using ChannelFn = int32_t (*)(int32_t, uint8_t);
    using SendFn = int32_t (*)(int32_t, uint8_t, const CanMsg*, uint32_t);
    using GetMsgFn = int32_t (*)(int32_t, uint8_t, void*, int32_t);
    using FdSpeedArgFn = int32_t (*)(int32_t, CanFdInitConfig*, uint32_t, uint32_t);
    using FdInitFn = int32_t (*)(int32_t, uint8_t, const CanFdInitConfig*);
    using FdSendFn = int32_t (*)(int32_t, uint8_t, const void*, uint32_t);
    using FdSetFilterFn = int32_t (*)(int32_t, uint8_t, const CanFdFilterConfig*,
                                     uint8_t);

    ScanFn pScan_ = nullptr;
    OpenFn pUsbOpen_ = nullptr;
    CloseFn pUsbClose_ = nullptr;
    SpeedArgFn pCanSpeedArg_ = nullptr;
    InitFn pCanInit_ = nullptr;
    FilterInitFn pCanFilterInit_ = nullptr;
    ChannelFn pCanStartGetMsg_ = nullptr;
    ChannelFn pCanStopGetMsg_ = nullptr;
    ChannelFn pCanStop_ = nullptr;
    ChannelFn pCanClearMsg_ = nullptr;
    SendFn pCanSendMsg_ = nullptr;
    GetMsgFn pCanGetMsg_ = nullptr;
    FdSpeedArgFn pFdSpeedArg_ = nullptr;
    FdInitFn pFdInit_ = nullptr;
    ChannelFn pFdStartGetMsg_ = nullptr;
    ChannelFn pFdStopGetMsg_ = nullptr;
    ChannelFn pFdStop_ = nullptr;
    ChannelFn pFdClearMsg_ = nullptr;
    FdSendFn pFdSendMsg_ = nullptr;
    GetMsgFn pFdGetMsg_ = nullptr;
    FdSetFilterFn pFdSetFilter_ = nullptr;
};

} // namespace toomoss
} // namespace canhw
