#pragma once
// ==================================================================
//  PEAK 经典 CAN 硬件封装(对应 .NET Entity/PeakCanWrapper.cs)
//  注册键:Peak + Generic + CAN
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>

#include <canhw/IHardware.h>
#include <canhw/PeakConfig.h>

#include "PeakApi.h"

namespace canhw {
namespace peak {

class PeakCanWrapper : public IHardware {
public:
    PeakCanWrapper() = default;
    ~PeakCanWrapper() override { Close(); }

    PeakCanWrapper(const PeakCanWrapper&) = delete;
    PeakCanWrapper& operator=(const PeakCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    PeakCANConfig config_;

    // PEAK 通道句柄(TPCANHandle),PCAN_NONEBUS(0) 表示未打开
    uint16_t handle_ = PCAN_NONEBUS;

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    canhw::Thread readThread_;
    bool isOpen_ = false;
};

} // namespace peak
} // namespace canhw
