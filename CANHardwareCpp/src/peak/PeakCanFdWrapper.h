#pragma once
// ==================================================================
//  PEAK CAN FD 硬件封装(对应 .NET Entity/PeakCanFdWrapper.cs)
//  注册键:Peak + Generic + CANFD
// ==================================================================
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <canhw/IHardware.h>
#include <canhw/PeakConfig.h>

#include "PeakApi.h"

namespace canhw {
namespace peak {

class PeakCanFdWrapper : public IHardware {
public:
    PeakCanFdWrapper() = default;
    ~PeakCanFdWrapper() override { Close(); }

    PeakCanFdWrapper(const PeakCanFdWrapper&) = delete;
    PeakCanFdWrapper& operator=(const PeakCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    /// 按 PCAN-Basic InitializeFD 文档把位时序参数拼成波特率字符串
    std::string BuildCanFdBitrate() const;

    PeakCanFDConfig config_;

    uint16_t handle_ = PCAN_NONEBUS;

    std::mutex lock_;
    std::atomic<bool> stop_{false};
    std::thread readThread_;
    bool isOpen_ = false;
};

} // namespace peak
} // namespace canhw
