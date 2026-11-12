#pragma once
// ==================================================================
//  Kvaser CAN FD 硬件封装(对应 .NET Entity/KvaserCanFDWrapper.cs)
//  注册键:Kvaser + Generic + CANFD
// ==================================================================
#include <atomic>
#include <mutex>
#include <thread>

#include <canhw/IHardware.h>
#include <canhw/KvaserConfig.h>

#include "KvaserApi.h"

namespace canhw {
namespace kvaser {

class KvaserCanFDWrapper : public IHardware {
public:
    KvaserCanFDWrapper() = default;
    ~KvaserCanFDWrapper() override { Close(); }

    KvaserCanFDWrapper(const KvaserCanFDWrapper&) = delete;
    KvaserCanFDWrapper& operator=(const KvaserCanFDWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    KvaserCanFDConfig config_;

    int32_t handle_ = -1;

    std::mutex lock_;
    std::atomic<bool> stop_{false};
    std::thread readThread_;
    bool isOpen_ = false;
};

} // namespace kvaser
} // namespace canhw
