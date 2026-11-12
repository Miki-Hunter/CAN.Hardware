#pragma once
// ==================================================================
//  ZLG zlgcan.dll 的动态绑定(对应 .NET BaseLibDll/ZlgCanLibDLL.cs)
//  结构体布局与仓库内官方 HwDependency/Zlg/USBCAN/Win/x64/zlgcan.h 一致。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <canhw/Sync.h>
#include <string>

#include <canhw/Enums.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace zlg {

// ---------------- 与 zlgcan.h 一致的原生结构体(自然对齐) ----------------

struct CanFrame {           // can_frame
    uint32_t canId;         // SocketCAN 风格:高位含 EFF/RTR/ERR 标志
    uint8_t  dlc;           // payload length in byte (0..8)
    uint8_t  pad;
    uint8_t  res0;
    uint8_t  res1;
    uint8_t  data[8];
};

struct CanFdFrame {         // canfd_frame
    uint32_t canId;
    uint8_t  len;           // payload length in byte (0..64),非 DLC 编码
    uint8_t  flags;         // bit0=BRS, bit1=ESI
    uint8_t  res0;
    uint8_t  res1;
    uint8_t  data[64];
};

struct TransmitData {       // ZCAN_Transmit_Data
    CanFrame frame;
    uint32_t transmitType;  // 0=正常发送,1=单次发送,2=自发自收,3=单次自发自收
};

struct ReceiveData {        // ZCAN_Receive_Data
    CanFrame frame;
    uint64_t timestamp;
};

struct TransmitFdData {     // ZCAN_TransmitFD_Data
    CanFdFrame frame;
    uint32_t transmitType;
};

struct ReceiveFdData {      // ZCAN_ReceiveFD_Data
    CanFdFrame frame;
    uint64_t timestamp;
};

struct ChannelInitConfig {  // ZCAN_CHANNEL_INIT_CONFIG
    uint32_t canType;       // 0=经典 CAN, 1=CANFD
    union {
        struct {
            uint32_t accCode;
            uint32_t accMask;
            uint32_t reserved;
            uint8_t  filter;
            uint8_t  timing0;
            uint8_t  timing1;
            uint8_t  mode;
        } can;
        struct {
            uint32_t accCode;
            uint32_t accMask;
            uint32_t abitTiming;
            uint32_t dbitTiming;
            uint32_t brp;
            uint8_t  filter;
            uint8_t  mode;
            uint16_t pad;
            uint32_t reserved;
        } canfd;
    } config;
};

// ---------------- DLL 绑定 ----------------

class ZlgApi {
public:
    static ZlgApi& Instance();

    /// 加载 zlgcan.dll 并解析符号(幂等);失败返回 false
    bool Load();
    bool IsLoaded() const { return loaded_; }
    /// 未加载则尝试加载,失败抛 std::runtime_error(带路径与错误码)
    void EnsureLoaded();

    // 共享设备句柄管理:同一物理设备只需打开一次,多通道复用(引用计数)
    void* OpenDevice(uint32_t deviceType, uint32_t deviceIndex, uint32_t reserved);
    void CloseDevice();

    void* InitCAN(void* deviceHandle, uint32_t canIndex, void* pInitConfig);
    uint32_t SetValue(void* deviceHandle, const char* path, const char* value);
    uint32_t ClearBuffer(void* channelHandle);
    uint32_t ResetCAN(void* channelHandle);
    uint32_t StartCAN(void* channelHandle);
    uint32_t Transmit(void* channelHandle, void* pTransmit, uint32_t len);
    uint32_t TransmitFD(void* channelHandle, void* pTransmit, uint32_t len);
    uint32_t Receive(void* channelHandle, void* pReceive, uint32_t len, int waitTime);
    uint32_t ReceiveFD(void* channelHandle, void* pReceive, uint32_t len, int waitTime);

    // ---------------- 通用工具(对应 .NET ZLGCanLibDLL 公用方法) ----------------

    /// 把真实 ID 合成 SocketCAN 风格高位标志
    static uint32_t MakeCanId(uint32_t id, bool eff, bool rtr, bool err);
    static uint32_t BuildCanBitrate(ZlgCanBitrate bitrate);
    static uint32_t BuildCanFdBitrate(ZlgCanFDBitrate bitrate);

    static const char* DefaultDllPath() {
        return ".\\HwDependency\\Zlg\\USBCAN\\Win\\x64\\zlgcan.dll";
    }

private:
    ZlgApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};

    canhw::Mutex deviceLock_;
    void* sharedDeviceHandle_ = nullptr;
    int deviceRefCount_ = 0;

    using OpenDeviceFn = void* (*)(uint32_t, uint32_t, uint32_t);
    using CloseDeviceFn = uint32_t (*)(void*);
    using InitCANFn = void* (*)(void*, uint32_t, void*);
    using SetValueFn = uint32_t (*)(void*, const char*, const char*);
    using ClearBufferFn = uint32_t (*)(void*);
    using ResetCANFn = uint32_t (*)(void*);
    using StartCANFn = uint32_t (*)(void*);
    using TransmitFn = uint32_t (*)(void*, void*, uint32_t);
    using ReceiveFn = uint32_t (*)(void*, void*, uint32_t, int);

    OpenDeviceFn   pOpenDevice_   = nullptr;
    CloseDeviceFn  pCloseDevice_  = nullptr;
    InitCANFn      pInitCAN_      = nullptr;
    SetValueFn     pSetValue_     = nullptr;
    ClearBufferFn  pClearBuffer_  = nullptr;
    ResetCANFn     pResetCAN_     = nullptr;
    StartCANFn     pStartCAN_     = nullptr;
    TransmitFn     pTransmit_     = nullptr;
    TransmitFn     pTransmitFD_   = nullptr;
    ReceiveFn      pReceive_      = nullptr;
    ReceiveFn      pReceiveFD_    = nullptr;
};

} // namespace zlg
} // namespace canhw
