#pragma once
// ==================================================================
//  Kvaser 经典 CAN 硬件封装(对应 .NET Entity/KvaserCanWrapper.cs)
//  注册键:Kvaser + Generic + CAN
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>

#include <canhw/IHardware.h>
#include <canhw/KvaserConfig.h>

#include "KvaserApi.h"

namespace canhw {
namespace kvaser {

class KvaserCanWrapper : public IHardware {
public:
    KvaserCanWrapper() = default;
    ~KvaserCanWrapper() override { Close(); }

    KvaserCanWrapper(const KvaserCanWrapper&) = delete;
    KvaserCanWrapper& operator=(const KvaserCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    KvaserCANConfig config_;

    // Kvaser handle 不支持多线程操作,由单一锁串行化;
    // -1 表示未打开
    int32_t handle_ = -1;

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    canhw::Thread readThread_;
    bool isOpen_ = false;
};

} // namespace kvaser
} // namespace canhw
