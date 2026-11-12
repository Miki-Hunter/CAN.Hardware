#pragma once
// ==================================================================
//  ZLG 经典 CAN 硬件封装(对应 .NET Entity/ZlgUsbCanWrapper.cs)
//  注册键:Zlg + USBCAN + CAN
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>
#include <vector>

#include <canhw/IHardware.h>
#include <canhw/ZlgConfig.h>

#include "ZlgApi.h"

namespace canhw {
namespace zlg {

class ZlgUsbCanWrapper : public IHardware {
public:
    ZlgUsbCanWrapper() = default;
    ~ZlgUsbCanWrapper() override { Close(); }

    ZlgUsbCanWrapper(const ZlgUsbCanWrapper&) = delete;
    ZlgUsbCanWrapper& operator=(const ZlgUsbCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    ZlgCANConfig config_;

    // ZLG 句柄分两层:设备句柄(共享,引用计数) + 通道句柄,nullptr 表示未打开
    void* devHandle_ = nullptr;
    void* channelHandle_ = nullptr;

    // 接收/发送缓冲在 Open 时一次性分配,Close 时释放,避免每帧分配
    static constexpr size_t RxBatchSize = 100;  // 单次调用最多读取的帧数
    std::vector<ReceiveData> rxBuf_;
    TransmitData txBuf_{};

    // 单一锁保护所有状态和硬件操作(对应 .NET 的 object _lock)
    canhw::Mutex lock_;

    // 后台读线程控制(对应 .NET Task + CancellationTokenSource)
    std::atomic<bool> stop_{false};
    canhw::Thread readThread_;
    bool isOpen_ = false;
};

} // namespace zlg
} // namespace canhw
