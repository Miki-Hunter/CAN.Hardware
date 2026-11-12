#pragma once
// ==================================================================
//  NI-XNET CAN FD 封装(:can_fd_brs: 伪数据库 / Frame In-Out Stream)
//  对应 .NET Entity/NiCanFdWrapper.cs
//  注册键:Ni + Generic + CANFD
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>
#include <vector>

#include <canhw/IHardware.h>
#include <canhw/NiConfig.h>

#include "NiApi.h"

namespace canhw {
namespace ni {

class NiCanFdWrapper : public IHardware {
public:
    NiCanFdWrapper() = default;
    ~NiCanFdWrapper() override { Close(); }

    NiCanFdWrapper(const NiCanFdWrapper&) = delete;
    NiCanFdWrapper& operator=(const NiCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    void ParseFrames(const std::vector<uint8_t>& buffer, uint32_t bytesRead);

    NiCanFDConfig config_;

    uint32_t inputSession_ = 0;
    uint32_t outputSession_ = 0;

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    canhw::Thread readThread_;
    bool isOpen_ = false;
};

} // namespace ni
} // namespace canhw
