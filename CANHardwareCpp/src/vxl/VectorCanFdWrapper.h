#pragma once
// ==================================================================
//  Vector CAN FD 封装(vxlapi64.dll,V4 接口)
//  对应 .NET Entity/VectorCanFdWrapper.cs
//  注册键:Vector + Generic + CANFD
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>

#include <canhw/IHardware.h>
#include <canhw/VectorConfig.h>

#include "VectorApi.h"

namespace canhw {
namespace vxl {

class VectorCanFdWrapper : public IHardware {
public:
    VectorCanFdWrapper() = default;
    ~VectorCanFdWrapper() override { Close(); }

    VectorCanFdWrapper(const VectorCanFdWrapper&) = delete;
    VectorCanFdWrapper& operator=(const VectorCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    /// 自定义 FD 时序参数合法性校验:采样点 50%~95%,tseg1>=tseg2,sjw>=1 且不超过 tseg2
    static void ValidateFdTiming(const char* segment, uint32_t bitrate, int tseg1,
                                 int tseg2, int sjw);

    VectorCanFDConfig config_;

    int32_t portHandle_ = -1;
    uint64_t accessMask_ = 0;

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    canhw::Thread readThread_;
    bool isOpen_ = false;
};

} // namespace vxl
} // namespace canhw
