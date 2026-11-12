#pragma once
// ==================================================================
//  Vector 经典 CAN 封装(vxlapi64.dll)
//  对应 .NET Entity/VectorCanWrapper.cs
//  注册键:Vector + Generic + CAN
// ==================================================================
#include <atomic>
#include <mutex>
#include <thread>

#include <canhw/IHardware.h>
#include <canhw/VectorConfig.h>

#include "VectorApi.h"

namespace canhw {
namespace vxl {

class VectorCanWrapper : public IHardware {
public:
    VectorCanWrapper() = default;
    ~VectorCanWrapper() override { Close(); }

    VectorCanWrapper(const VectorCanWrapper&) = delete;
    VectorCanWrapper& operator=(const VectorCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    /// 时序参数是否合理:采样点 50%~95%,tseg1>=tseg2,sjw 1~4 且不超过 tseg2+1
    static bool IsPlausibleTiming(const XLcanBusParamsInfo& p);

    VectorCanConfig config_;

    // Vector 端口句柄,-1 表示未打开
    int32_t portHandle_ = -1;
    uint64_t accessMask_ = 0;

    std::mutex lock_;
    std::atomic<bool> stop_{false};
    std::thread readThread_;
    bool isOpen_ = false;
};

} // namespace vxl
} // namespace canhw
