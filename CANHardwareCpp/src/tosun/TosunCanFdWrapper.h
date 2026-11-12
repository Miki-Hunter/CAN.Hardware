#pragma once
// ==================================================================
//  同星 CAN FD 封装(libTSCAN,事件驱动接收)
//  对应 .NET Entity/ToSunCanFdWrapper.cs
//  注册键:Tosun + Generic + CANFD
// ==================================================================
#include <atomic>
#include <mutex>

#include <canhw/IHardware.h>
#include <canhw/TosunConfig.h>

#include "TosunApi.h"

namespace canhw {
namespace tosun {

class ToSunCanFdWrapper : public IHardware {
public:
    ToSunCanFdWrapper() = default;
    ~ToSunCanFdWrapper() override { Close(); }

    ToSunCanFdWrapper(const ToSunCanFdWrapper&) = delete;
    ToSunCanFdWrapper& operator=(const ToSunCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void OnCanFdEvent(const TLibCanFd& canFdData);

    ToSunCanFDConfig config_;

    void* handle_ = nullptr;

    std::mutex lock_;
    std::atomic<bool> isOpen_{false};
};

} // namespace tosun
} // namespace canhw
