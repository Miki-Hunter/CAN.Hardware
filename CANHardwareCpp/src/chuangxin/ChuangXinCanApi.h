#pragma once
// ==================================================================
//  创芯科技经典 CAN:ControlCAN.dll(VCI_ API)动态绑定
//  (对应 .NET BaseLibDll/ChuangXinCanLibDLL.cs)
//  VCI 以 (DeviceType, DeviceIndex) 标识设备,无设备句柄;
//  同一物理设备的多个通道共享一次 Open/Close,引用计数管理。
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

/// CAN 信息帧(VCI_CAN_OBJ)。ID 不含标志位,扩展/远程帧由 ExternFlag/RemoteFlag 表示
struct VciCanObj {
    uint32_t id;
    uint32_t timeStamp;  ///< 单位 0.1ms
    uint8_t timeFlag;    ///< 是否使用时间标识
    uint8_t sendType;    ///< 发送标志(保留)
    uint8_t remoteFlag;  ///< 是否远程帧
    uint8_t externFlag;  ///< 是否扩展帧
    uint8_t dataLen;     ///< 数据长度
    uint8_t data[8];
    uint8_t reserved[3];
};

/// 初始化 CAN 的参数(VCI_INIT_CONFIG)
/// Filter:0/1=接收所有帧,2=标准帧滤波,3=扩展帧滤波;Mode:0=正常,1=只听,2=自测
struct VciInitConfig {
    uint32_t accCode;
    uint32_t accMask;
    uint32_t reserved;
    uint8_t filter;
    uint8_t timing0;
    uint8_t timing1;
    uint8_t mode;
};

class ChuangXinCanApi {
public:
    static ChuangXinCanApi& Instance();

    bool Load();
    bool IsLoaded() const { return loaded_; }
    void EnsureLoaded();

    /// 打开设备(同一物理设备的多个通道共享一次 Open,引用计数管理)
    bool OpenDevice(uint32_t deviceType, uint32_t deviceIndex);
    /// 关闭设备(引用计数归零时才真正关闭)
    void CloseDevice(uint32_t deviceType, uint32_t deviceIndex);

    uint32_t InitCAN(uint32_t deviceType, uint32_t deviceIndex, uint32_t canIndex,
                     const VciInitConfig* pInitConfig);
    uint32_t StartCAN(uint32_t deviceType, uint32_t deviceIndex, uint32_t canIndex);
    uint32_t ResetCAN(uint32_t deviceType, uint32_t deviceIndex, uint32_t canIndex);
    uint32_t ClearBuffer(uint32_t deviceType, uint32_t deviceIndex, uint32_t canIndex);
    uint32_t Transmit(uint32_t deviceType, uint32_t deviceIndex, uint32_t canIndex,
                      const VciCanObj* pSend, uint32_t len);
    uint32_t Receive(uint32_t deviceType, uint32_t deviceIndex, uint32_t canIndex,
                     VciCanObj* pReceive, uint32_t len, int waitTime);

    // ---------------- 工具 ----------------

    /// 波特率枚举 -> ControlCAN 的 Timing0/Timing1 寄存器值
    static void BuildTiming(ChuangXinCanBitrate bitrate, uint8_t& timing0,
                            uint8_t& timing1);

    static const char* DefaultDllPath() {
        return ".\\HwDependency\\ChuangXin\\CAN\\Win\\x64\\ControlCAN.dll";
    }

private:
    ChuangXinCanApi() = default;

    DynamicLibrary lib_;
    std::atomic<bool> loaded_{false};

    // (deviceType, deviceIndex) -> 引用计数
    std::mutex deviceLock_;
    std::map<std::pair<uint32_t, uint32_t>, int> deviceRefCounts_;

    using OpenDeviceFn = uint32_t (*)(uint32_t, uint32_t, uint32_t);
    using CloseDeviceFn = uint32_t (*)(uint32_t, uint32_t);
    using InitCanFn = uint32_t (*)(uint32_t, uint32_t, uint32_t, const VciInitConfig*);
    using ChannelFn = uint32_t (*)(uint32_t, uint32_t, uint32_t);
    using TransmitFn = uint32_t (*)(uint32_t, uint32_t, uint32_t, const VciCanObj*,
                                    uint32_t);
    using ReceiveFn = uint32_t (*)(uint32_t, uint32_t, uint32_t, VciCanObj*, uint32_t,
                                   int);

    OpenDeviceFn pOpenDevice_ = nullptr;
    CloseDeviceFn pCloseDevice_ = nullptr;
    InitCanFn pInitCAN_ = nullptr;
    ChannelFn pStartCAN_ = nullptr;
    ChannelFn pResetCAN_ = nullptr;
    ChannelFn pClearBuffer_ = nullptr;
    TransmitFn pTransmit_ = nullptr;
    ReceiveFn pReceive_ = nullptr;
};

} // namespace chuangxin
} // namespace canhw
