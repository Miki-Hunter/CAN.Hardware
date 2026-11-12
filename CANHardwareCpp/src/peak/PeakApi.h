#pragma once
// ==================================================================
//  PEAK PCANBasic.dll 的动态绑定(对应 .NET BaseLibDll/PeakCanLibDLL.cs)
//  结构体布局与 PCAN-Basic 官方 C 头一致(自然对齐,字段偏移相同)。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "DynamicLibrary.h"

namespace canhw {
namespace peak {

// ---------------- PCAN-Basic 原生结构体 ----------------

struct TPCANMsg {              // 经典 CAN 报文
    uint32_t id;               // 11/29-bit message identifier
    uint8_t  msgType;          // TPCANMessageType 位组合
    uint8_t  len;              // Data Length Code of the message (0..8)
    uint8_t  data[8];
};

struct TPCANTimestamp {        // 经典 CAN 时间戳
    uint32_t millis;           // milliseconds: 0..2^32-1
    uint16_t millisOverflow;   // Roll-arounds of millis
    uint16_t micros;           // microseconds: 0..999
};

struct TPCANMsgFD {            // CAN FD 报文
    uint32_t id;
    uint8_t  msgType;          // TPCANMessageType 位组合
    uint8_t  dlc;              // Data Length Code of the message (0..15,需换算)
    uint8_t  data[64];
};

// ---------------- 枚举 / 常量 ----------------

/// TPCANMessageType(报文类型位)
enum TPCANMessageType : uint8_t {
    PCAN_MESSAGE_STANDARD = 0x00,  ///< 11-bit identifier
    PCAN_MESSAGE_RTR      = 0x01,  ///< Remote-Transfer-Request 帧
    PCAN_MESSAGE_EXTENDED = 0x02,  ///< 29-bit identifier
    PCAN_MESSAGE_FD       = 0x04,  ///< FD 帧
    PCAN_MESSAGE_BRS      = 0x08,  ///< FD 比特率切换
    PCAN_MESSAGE_ESI      = 0x10,  ///< FD 错误状态指示
    PCAN_MESSAGE_ERRFRAME = 0x40,  ///< 错误帧
    PCAN_MESSAGE_STATUS   = 0x80,  ///< 状态消息
};

/// TPCANStatus(错误码子集,完整语义由 CAN_GetErrorText 给出)
enum TPCANStatus : uint32_t {
    PCAN_ERROR_OK         = 0x00000,
    PCAN_ERROR_XMTFULL    = 0x00001,
    PCAN_ERROR_OVERRUN    = 0x00002,
    PCAN_ERROR_BUSLIGHT   = 0x00004,
    PCAN_ERROR_BUSHEAVY   = 0x00008,
    PCAN_ERROR_BUSOFF     = 0x00010,
    PCAN_ERROR_QRCVEMPTY  = 0x00020,
    PCAN_ERROR_QOVERRUN   = 0x00040,
    PCAN_ERROR_QXMTFULL   = 0x00080,
    PCAN_ERROR_ILLPARAMVAL = 0x08000,
};

/// TPCANMode(过滤器模式,复用 TPCANMessageType 的 STANDARD/EXTENDED 值)
enum TPCANMode : uint8_t {
    PCAN_MODE_STANDARD = PCAN_MESSAGE_STANDARD,  // 0
    PCAN_MODE_EXTENDED = PCAN_MESSAGE_EXTENDED,  // 2
};

constexpr uint16_t PCAN_NONEBUS = 0x00;  ///< 未打开
constexpr uint16_t PCAN_USBBUS1 = 0x51; ///< PCAN-USB 通道 1

// ---------------- DLL 绑定 ----------------

class PeakApi {
public:
    static PeakApi& Instance();

    bool Load();
    bool IsLoaded() const { return loaded_; }
    void EnsureLoaded();

    uint32_t Initialize(uint16_t channel, uint16_t btr0btr1, uint8_t hwType,
                        uint32_t ioPort, uint16_t interrupt);
    uint32_t InitializeFD(uint16_t channel, const char* bitrateFD);
    uint32_t Uninitialize(uint16_t channel);
    uint32_t Reset(uint16_t channel);
    uint32_t Read(uint16_t channel, TPCANMsg* msg, TPCANTimestamp* timestamp);
    uint32_t ReadFD(uint16_t channel, TPCANMsgFD* msg, uint64_t* timestamp);
    uint32_t Write(uint16_t channel, TPCANMsg* msg);
    uint32_t WriteFD(uint16_t channel, TPCANMsgFD* msg);
    uint32_t FilterMessages(uint16_t channel, uint32_t fromId, uint32_t toId,
                            uint8_t mode);

    /// 状态码转可读文本(内部调用 CAN_GetErrorText,Language=0 中性语言)
    std::string GetErrorText(uint32_t status);

    static const char* DefaultDllPath() {
        return ".\\HwDependency\\Peak\\Generic\\Win\\x64\\PCANBasic.dll";
    }

private:
    PeakApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};

    using InitializeFn = uint32_t (*)(uint16_t, uint16_t, uint8_t, uint32_t, uint16_t);
    using InitializeFDFn = uint32_t (*)(uint16_t, const char*);
    using UninitializeFn = uint32_t (*)(uint16_t);
    using ResetFn = uint32_t (*)(uint16_t);
    using ReadFn = uint32_t (*)(uint16_t, TPCANMsg*, TPCANTimestamp*);
    using ReadFDFn = uint32_t (*)(uint16_t, TPCANMsgFD*, uint64_t*);
    using WriteFn = uint32_t (*)(uint16_t, TPCANMsg*);
    using WriteFDFn = uint32_t (*)(uint16_t, TPCANMsgFD*);
    using FilterMessagesFn = uint32_t (*)(uint16_t, uint32_t, uint32_t, uint8_t);
    using GetErrorTextFn = uint32_t (*)(uint32_t, uint16_t, char*);

    InitializeFn pInitialize_ = nullptr;
    InitializeFDFn pInitializeFD_ = nullptr;
    UninitializeFn pUninitialize_ = nullptr;
    ResetFn pReset_ = nullptr;
    ReadFn pRead_ = nullptr;
    ReadFDFn pReadFD_ = nullptr;
    WriteFn pWrite_ = nullptr;
    WriteFDFn pWriteFD_ = nullptr;
    FilterMessagesFn pFilterMessages_ = nullptr;
    GetErrorTextFn pGetErrorText_ = nullptr;
};

} // namespace peak
} // namespace canhw
