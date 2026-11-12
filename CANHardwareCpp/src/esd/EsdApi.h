#pragma once
// ==================================================================
//  ESD NTCAN API(ntcan64.dll)动态绑定
//  (对应 .NET BaseLibDll/EsdNtcanLibDLL.cs)
//  按系统搜索路径加载 ntcan64.dll(随 esd electronics CAN 接口卡发布),
//  全部函数为 Cdecl;依赖 ESD CAN 驱动,未安装时 EnsureLoaded 抛出带指导信息的异常。
//  进程内加载后不主动卸载。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <canhw/EsdConfig.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace esd {

// ---------------- 常量(与 .NET EsdNtcanLibDLL 一致) ----------------

/// ntcan 原生扩展帧标记(bit29),与托管层 SocketCAN 风格 bit31 不同,需双向换算
constexpr uint32_t NTCAN_EXT_FLAG = 0x20000000;
constexpr uint32_t NTCAN_EXT_ID_MASK = 0x1FFFFFFF;
constexpr uint32_t NTCAN_STD_ID_MASK = 0x7FF;

// BaseMsg.Len 字节位定义
constexpr uint8_t LEN_MASK_DLC = 0x0F;
constexpr uint8_t LEN_FLAG_RTR = 0x10;       ///< 经典帧 RTR / FD 帧 NO_BRS
constexpr uint8_t LEN_FLAG_FD_NO_BRS = 0x10;
constexpr uint8_t LEN_FLAG_NO_DATA = 0x20;
constexpr uint8_t LEN_FLAG_FD = 0x80;

// canSetBaudrateX 的 Mode/Flags
constexpr uint16_t BAUDRATE_MODE_NUM = 4;
constexpr uint16_t BAUDRATE_FLAG_FD = 1;

// canOpen 句柄标志
namespace EsdCanMode {
constexpr uint32_t NONE = 0;
constexpr uint32_t NO_RTR = 0x00000010;
constexpr uint32_t FD = 0x00040000;
} // namespace EsdCanMode

// canSetBaudrate 参数模式(高 4 位模式标志,低 28 位参数)
namespace EsdBaudMode {
constexpr uint32_t USER_BAUDRATE_NUM = 0x20000000;
} // namespace EsdBaudMode

// ntcan 返回码
using EsdCanResult = uint32_t;
constexpr EsdCanResult RESULT_OK = 0;
constexpr EsdCanResult RX_TIMEOUT = 0xE0000000u + 1;
constexpr EsdCanResult HANDLE_FORCED_CLOSE = 0xE0000000u + 20;

// ---------------- 原生结构(Pack=1) ----------------

/// 消息公共头(8 字节)
struct EsdBaseMsg {
    uint32_t id;      ///< 11/29 位 CAN ID,扩展帧用 bit29 标记
    uint8_t len;      ///< bit0-3=DLC, bit4=RTR/NO_BRS, bit5=NO_DATA, bit7=FD
    uint8_t msgLost;  ///< Rx 丢帧计数
    uint8_t reserved;
    uint8_t esi;      ///< Error State Indicator(仅 CAN FD 有效)
};

/// 经典 CAN 消息(24 字节):8 字节数据打包为小端 uint64
struct EsdCanMsg {
    EsdBaseMsg base;
    uint64_t data;       ///< 8 字节 CAN 数据(小端)
    uint64_t timestamp;  ///< 时间戳(µs)
};

/// CAN FD 消息(80 字节)
struct EsdCanFdMsg {
    EsdBaseMsg base;
    uint8_t data[64];
    uint64_t timestamp;  ///< 时间戳(µs)
};

/// TDC(收发延迟补偿)配置,保持默认全 0(关闭)
struct EsdTdcCfg {
    uint8_t tdcMode;
    uint8_t sspOffset;
    int8_t sspShift;
    uint8_t tdcFilter;
};

struct EsdBaudrateCfgNum {
    uint32_t value;   ///< 数值波特率(bps)
    uint32_t reserved;
};

/// canSetBaudrateX 的数值模式参数(24 字节):仲裁段 + 数据段直接填 bps
struct EsdBaudrateXNum {
    uint16_t mode;    ///< BAUDRATE_MODE_NUM
    uint16_t flags;   ///< BAUDRATE_FLAG_FD
    EsdTdcCfg tdc;
    EsdBaudrateCfgNum arb;
    EsdBaudrateCfgNum data;
};

class EsdApi {
public:
    static EsdApi& Instance();

    /// 定位并加载 ntcan64.dll,解析全部导出函数;重复调用为空操作
    void EnsureLoaded();
    bool IsLoaded() const { return loaded_; }

    EsdCanResult CanOpen(int32_t channel, uint32_t flags, int32_t txQueueSize,
                         int32_t rxQueueSize, int32_t txTimeout, int32_t rxTimeout,
                         void** handle);
    EsdCanResult CanClose(void* handle);
    /// canReadT:阻塞读取(超时由 canOpen 的 rxTimeout 决定),len 入参为缓冲条数,出参实际读到条数
    EsdCanResult CanRead(void* handle, EsdCanMsg* msg, int32_t* len);
    EsdCanResult CanWrite(void* handle, EsdCanMsg* msg, int32_t* len);
    /// canReadX:阻塞读取 FD 通道消息(经典帧与 FD 帧均可)
    EsdCanResult CanReadX(void* handle, EsdCanFdMsg* msg, int32_t* len);
    /// canWriteX:FD 通道发送(经典帧不带 LEN_FLAG_FD 位)
    EsdCanResult CanWriteX(void* handle, EsdCanFdMsg* msg, int32_t* len);
    EsdCanResult CanSetBaudrate(void* handle, uint32_t baud);
    EsdCanResult CanSetBaudrateX(void* handle, const EsdBaudrateXNum* config);
    /// canIdRegionAdd:使能一段 ID 接收范围, idCount 为 in/out
    EsdCanResult CanIdRegionAdd(void* handle, int32_t idStart, int32_t* idCount);

    // ---------------- 工具 ----------------

    /// 托管层 SocketCAN 高位标志 -> ntcan 原生 ID(bit29=EFF,RTR 在 Len)
    static uint32_t BuildNtcanId(int64_t id);
    /// ntcan 原生 ID + Len -> 合成 SocketCAN 高位标志的 ID
    static int64_t BuildManagedId(uint32_t ntcanId, uint8_t len);

    /// 结果码文本
    static std::string ResultText(EsdCanResult result);

    static const char* DefaultDllPath() { return "ntcan64.dll"; }

private:
    EsdApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};
    std::mutex loadLock_;

    using OpenFn = EsdCanResult (*)(int32_t, uint32_t, int32_t, int32_t, int32_t,
                                    int32_t, void**);
    using CloseFn = EsdCanResult (*)(void*);
    using ReadTFn = EsdCanResult (*)(void*, EsdCanMsg*, int32_t*, void*);
    using WriteTFn = EsdCanResult (*)(void*, EsdCanMsg*, int32_t*, void*);
    using ReadXFn = EsdCanResult (*)(void*, EsdCanFdMsg*, int32_t*, void*);
    using WriteXFn = EsdCanResult (*)(void*, EsdCanFdMsg*, int32_t*, void*);
    using SetBaudrateFn = EsdCanResult (*)(void*, uint32_t);
    using SetBaudrateXFn = EsdCanResult (*)(void*, const EsdBaudrateXNum*);
    using IdRegionAddFn = EsdCanResult (*)(void*, int32_t, int32_t*);

    OpenFn pOpen_ = nullptr;
    CloseFn pClose_ = nullptr;
    ReadTFn pReadT_ = nullptr;
    WriteTFn pWriteT_ = nullptr;
    ReadXFn pReadX_ = nullptr;
    WriteXFn pWriteX_ = nullptr;
    SetBaudrateFn pSetBaudrate_ = nullptr;
    SetBaudrateXFn pSetBaudrateX_ = nullptr;
    IdRegionAddFn pIdRegionAdd_ = nullptr;
};

} // namespace esd
} // namespace canhw
