#pragma once
// ==================================================================
//  同星 经典 CAN 封装(libTSCAN,事件驱动接收)
//  对应 .NET Entity/ToSunCanWrapper.cs
//  注册键:Tosun + Generic + CAN
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>

#include <canhw/IHardware.h>
#include <canhw/TosunConfig.h>

#include "TosunApi.h"

namespace canhw {
namespace tosun {

class ToSunCanWrapper : public IHardware {
public:
    ToSunCanWrapper() = default;
    ~ToSunCanWrapper() override { Close(); }

    ToSunCanWrapper(const ToSunCanWrapper&) = delete;
    ToSunCanWrapper& operator=(const ToSunCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    /// 驱动线程回调(经 TosunDispatch 按通道路由),内部不可抛异常
    void OnCanEvent(const TLibCan& canData);

    ToSunCANConfig config_;

    // 同星设备句柄,nullptr 表示未打开
    void* handle_ = nullptr;

    canhw::Mutex lock_;
    // 事件回调线程读取,需原子
    std::atomic<bool> isOpen_{false};
};

} // namespace tosun
} // namespace canhw
