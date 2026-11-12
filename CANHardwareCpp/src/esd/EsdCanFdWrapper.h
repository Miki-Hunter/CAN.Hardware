#pragma once
// ==================================================================
//  ESD CAN FD 封装(ntcan64.dll / canReadX/canWriteX)
//  对应 .NET Entity/EsdCanFdWrapper.cs
//  特点:阻塞读(200ms 超时)不持锁;FD 通道同时兼容经典帧。
//  注册键:Esd + Generic + CANFD
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>

#include <canhw/EsdConfig.h>
#include <canhw/IHardware.h>

#include "EsdApi.h"

namespace canhw {
namespace esd {

class EsdCanFdWrapper : public IHardware {
public:
    EsdCanFdWrapper() = default;
    ~EsdCanFdWrapper() override { Close(); }

    EsdCanFdWrapper(const EsdCanFdWrapper&) = delete;
    EsdCanFdWrapper& operator=(const EsdCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    /// 阻塞读不持锁:canReadX 最长阻塞 200ms,持锁会卡住 Send/Close
    void ReadBlocking();

    EsdCanFDConfig config_;

    std::atomic<void*> handle_{nullptr};

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> isOpen_{false};
    canhw::Thread readThread_;

    static constexpr int32_t RX_TIMEOUT_MS = 200;
};

} // namespace esd
} // namespace canhw
