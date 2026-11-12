#pragma once
// ==================================================================
//  Intrepid neoVI CAN FD 封装(icsneo40.dll,驱动级等待接收)
//  对应 .NET Entity/IntrepidCanFdWrapper.cs
//  注册键:Intrepid + Generic + CANFD
// ==================================================================
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <canhw/IntrepidConfig.h>
#include <canhw/IHardware.h>

#include "IntrepidApi.h"

namespace canhw {
namespace intrepid {

class IntrepidCanFdWrapper : public IHardware {
public:
    IntrepidCanFdWrapper() = default;
    ~IntrepidCanFdWrapper() override { Close(); }

    IntrepidCanFdWrapper(const IntrepidCanFdWrapper&) = delete;
    IntrepidCanFdWrapper& operator=(const IntrepidCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();

    IntrepidCanFDConfig config_;

    void* handle_ = nullptr;
    int32_t networkId_ = NETID_HSCAN1;

    static constexpr size_t RxBufferSize = 20000;
    static constexpr uint32_t RxWaitTimeoutMs = 200;
    std::vector<SpyMessage> rxMessages_;

    std::mutex lock_;
    std::atomic<bool> stop_{false};
    std::thread readThread_;
    bool isOpen_ = false;
};

} // namespace intrepid
} // namespace canhw
