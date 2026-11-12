#pragma once
// ==================================================================
//  ESD 经典 CAN 封装(ntcan64.dll / canReadT/canWriteT)
//  对应 .NET Entity/EsdCanWrapper.cs
//  特点:阻塞读(200ms 超时)不持锁,避免卡住 Send/Close。
//  注册键:Esd + Generic + CAN
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>

#include <canhw/EsdConfig.h>
#include <canhw/IHardware.h>

#include "EsdApi.h"

namespace canhw {
namespace esd {

class EsdCanWrapper : public IHardware {
public:
    EsdCanWrapper() = default;
    ~EsdCanWrapper() override { Close(); }

    EsdCanWrapper(const EsdCanWrapper&) = delete;
    EsdCanWrapper& operator=(const EsdCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    /// 阻塞读不持锁:canReadT 最长阻塞 200ms,持锁会卡住 Send/Close
    void ReadBlocking();

    EsdCANConfig config_;

    // ntcan 通道句柄,nullptr 表示未打开;读线程无锁访问,用原子
    std::atomic<void*> handle_{nullptr};

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> isOpen_{false};
    canhw::Thread readThread_;

    // canReadT 阻塞超时(ms),保证接收线程能周期性检查退出条件
    static constexpr int32_t RX_TIMEOUT_MS = 200;
};

} // namespace esd
} // namespace canhw
