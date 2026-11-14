#pragma once
// ==================================================================
//  NI-XNET(nixnet.dll)C API 动态绑定
//  (对应 .NET BaseLibDll/NiXnetLibDLL.cs)
//  nixnet.dll 随 NI-XNET 驱动安装到系统目录,用裸 DLL 名由系统搜索路径解析,
//  调用约定 Cdecl。
//  帧内存布局(nxFrame_t):16 字节帧头 + 8 字节对齐的负载区,
//  基本单元 24 字节,负载超过 8 字节时按 8 字节步进追加。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <canhw/Sync.h>
#include <string>
#include <vector>

#include <canhw/NiConfig.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace ni {

constexpr int NX_SUCCESS = 0;

// 帧布局常量
constexpr int NX_FRAME_HEADER_SIZE = 16;  ///< 时间戳8 + ID4 + Type1 + Flags1 + Info1 + PayloadLength1
constexpr int NX_MAX_PAYLOAD = 64;

// 扩展帧标记:XNET 使用 ID 的 bit29,而非常见的 bit31
constexpr uint32_t NX_CAN_EXT_FLAG = 0x20000000;
constexpr uint32_t NX_CAN_ID_MASK = 0x1FFFFFFF;

// 帧类型(nxFrameType_t,CAN 部分)
constexpr uint8_t NX_FRAME_TYPE_CAN_DATA = 0x00;
constexpr uint8_t NX_FRAME_TYPE_CAN_REMOTE = 0x01;
constexpr uint8_t NX_FRAME_TYPE_CAN_BUS_ERROR = 0x02;
constexpr uint8_t NX_FRAME_TYPE_CAN20_DATA = 0x08;
constexpr uint8_t NX_FRAME_TYPE_CANFD_DATA = 0x10;
constexpr uint8_t NX_FRAME_TYPE_CANFDBRS_DATA = 0x18;

// 帧标志:bit7 为发送回显
constexpr uint8_t NX_FRAME_FLAG_TRANSMIT_ECHO = 0x80;

// nxCreateSession 的 Mode
constexpr uint32_t NX_MODE_FRAME_IN_STREAM = 6;
constexpr uint32_t NX_MODE_FRAME_OUT_STREAM = 9;

// nxSetProperty 属性 ID
constexpr uint32_t NX_PROP_SESSION_INTF_BAUD_RATE64 = 0x09100016;
constexpr uint32_t NX_PROP_SESSION_INTF_CAN_FD_BAUD_RATE64 = 0x09100027;

// 内存伪数据库:::memory: 经典 CAN,::can_fd_brs: CAN FD + BRS
constexpr const char* DB_CAN = ":memory:";
constexpr const char* DB_CAN_FD_BRS = ":can_fd_brs:";

// 接收缓冲区(64KB)
constexpr int NX_READ_BUFFER_SIZE = 0x10000;

// nxReadFrame 单次阻塞超时(秒)
constexpr double NX_READ_TIMEOUT = 0.1;

// NI-XNET CAN 硬件时基 40MHz(1 tq = 25ns)
constexpr int NX_CAN_TQ_NS = 25;

class NiApi {
public:
    static NiApi& Instance();

    void EnsureLoaded();
    bool IsLoaded() const { return loaded_; }

    int32_t CreateSession(const char* databaseName, const char* clusterName,
                          const char* list, const char* interfaceName, uint32_t mode,
                          uint32_t* sessionRef);
    int32_t Clear(uint32_t sessionRef);
    int32_t Flush(uint32_t sessionRef);
    int32_t SetProperty64(uint32_t sessionRef, uint32_t propertyId,
                          uint32_t propertySize, uint64_t* propertyValue);
    int32_t ReadFrame(uint32_t sessionRef, uint8_t* buffer, uint32_t sizeOfBuffer,
                      double timeout, uint32_t* numberOfBytesReturned);
    int32_t WriteFrame(uint32_t sessionRef, const uint8_t* buffer,
                       uint32_t numberOfBytesForFrames, double timeout);

    // ---------------- 工具(对应 .NET NiXnetLibDLL 公用方法) ----------------

    /// 打包经典 CAN / FD 仲裁段高级波特率值
    static uint64_t PackCustomBaudRate(int prescale, int tseg1, int tseg2, int sjw);
    /// 打包 CAN FD 数据段高级波特率值(字段布局与仲裁段不同)
    static uint64_t PackCustomFdDataBaudRate(int prescale, int tseg1, int tseg2,
                                             int sjw);
    /// 一帧在流中占用的字节数
    static int GetFrameSize(int payloadLength);
    /// 按 nxFrame_t 内存布局手工打包一帧
    static std::vector<uint8_t> BuildFrameBytes(uint32_t identifier, uint8_t frameType,
                                                const uint8_t* data, int payloadLength);
    /// nxStatus 码转可读错误描述(非 static:内部需访问已加载的 nxStatusToString)
    std::string GetStatusString(int32_t status);

    static const char* DefaultDllPath() { return "nixnet.dll"; }

private:
    NiApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};
    canhw::Mutex loadLock_;

    using CreateSessionFn = int32_t (*)(const char*, const char*, const char*,
                                        const char*, uint32_t, uint32_t*);
    using SessionFn = int32_t (*)(uint32_t);
    using SetProperty64Fn = int32_t (*)(uint32_t, uint32_t, uint32_t, uint64_t*);
    using ReadFrameFn = int32_t (*)(uint32_t, uint8_t*, uint32_t, double, uint32_t*);
    using WriteFrameFn = int32_t (*)(uint32_t, const uint8_t*, uint32_t, double);
    using StatusToStringFn = void (*)(int32_t, uint8_t*, uint32_t);

    CreateSessionFn pCreateSession_ = nullptr;
    SessionFn pClear_ = nullptr;
    SessionFn pFlush_ = nullptr;
    SetProperty64Fn pSetProperty64_ = nullptr;
    ReadFrameFn pReadFrame_ = nullptr;
    WriteFrameFn pWriteFrame_ = nullptr;
    StatusToStringFn pStatusToString_ = nullptr;
};

} // namespace ni
} // namespace canhw
