#pragma once
// ==================================================================
//  图莫斯 USB2XXX 经典 CAN 封装(CAN_* API)
//  对应 .NET Entity/ToomossCanWrapper.cs
//  注册键:Toomoss + Generic + CAN
// ==================================================================
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <canhw/IHardware.h>
#include <canhw/ToomossConfig.h>

#include "ToomossApi.h"

namespace canhw {
namespace toomoss {

class ToomossCanWrapper : public IHardware {
public:
    ToomossCanWrapper() = default;
    ~ToomossCanWrapper() override { Close(); }

    ToomossCanWrapper(const ToomossCanWrapper&) = delete;
    ToomossCanWrapper& operator=(const ToomossCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();
    CanFilterConfig BuildFilter() const;

    ToomossCANConfig config_;

    int32_t openedDevHandle_ = 0;
    uint8_t openedChannel_ = 0;

    static constexpr int RxBatchSize = 100;
    std::vector<CanMsg> rxBuf_;

    std::mutex lock_;
    std::atomic<bool> stop_{false};
    std::thread readThread_;
    bool isOpen_ = false;
};

} // namespace toomoss
} // namespace canhw
