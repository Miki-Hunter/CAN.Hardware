#pragma once
// ==================================================================
//  Intrepid neoVI 经典 CAN 封装(icsneo40.dll,驱动级等待接收)
//  对应 .NET Entity/IntrepidCanWrapper.cs
//  注册键:Intrepid + Generic + CAN
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>
#include <vector>

#include <canhw/IntrepidConfig.h>
#include <canhw/IHardware.h>

#include "IntrepidApi.h"

namespace canhw {
namespace intrepid {

class IntrepidCanWrapper : public IHardware {
public:
    IntrepidCanWrapper() = default;
    ~IntrepidCanWrapper() override { Close(); }

    IntrepidCanWrapper(const IntrepidCanWrapper&) = delete;
    IntrepidCanWrapper& operator=(const IntrepidCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();

    IntrepidCANConfig config_;

    // 设备句柄(nullptr 表示未打开)
    void* handle_ = nullptr;
    // 通道号(0 基) -> 设备网络 ID(HSCAN1~HSCAN7)
    int32_t networkId_ = NETID_HSCAN1;

    // 单次批量取消息的上限(缓冲区在 Open 时一次性分配)
    static constexpr size_t RxBufferSize = 20000;
    // 接收等待超时(ms),驱动级等待不占 CPU
    static constexpr uint32_t RxWaitTimeoutMs = 200;
    std::vector<SpyMessage> rxMessages_;

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    canhw::Thread readThread_;
    bool isOpen_ = false;
};

} // namespace intrepid
} // namespace canhw
