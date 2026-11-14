#pragma once
// ==================================================================
//  图莫斯 USB2XXX CANFD 封装(CANFD_* API)
//  对应 .NET Entity/ToomossCanFdWrapper.cs
//  同一 CANFD 通道也可收发经典 CAN 帧(FDF=0)。
//  注册键:Toomoss + Generic + CANFD
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>
#include <vector>

#include <canhw/IHardware.h>
#include <canhw/ToomossConfig.h>

#include "ToomossApi.h"

namespace canhw {
namespace toomoss {

class ToomossCanFdWrapper : public IHardware {
public:
    ToomossCanFdWrapper() = default;
    ~ToomossCanFdWrapper() override { Close(); }

    ToomossCanFdWrapper(const ToomossCanFdWrapper&) = delete;
    ToomossCanFdWrapper& operator=(const ToomossCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    bool SendCan(CANFrame& canFrame);
    bool SendFd(CANFrame& canFrame);

    ToomossCanFDConfig config_;

    int32_t openedDevHandle_ = 0;
    uint8_t openedChannel_ = 0;

    static constexpr int RxBatchSize = 100;
    std::vector<CanFdMsg> rxBuf_;
    CanFdMsg txBuf_{};

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    canhw::Thread readThread_;
    bool isOpen_ = false;
};

} // namespace toomoss
} // namespace canhw
