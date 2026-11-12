#pragma once
// ==================================================================
//  ZLG CAN FD 硬件封装(对应 .NET Entity/ZlgCanFdWrapper.cs)
//  USBCANFD-200U 同时支持 CAN 与 CANFD,由 ZlgCanFDConfig::useCanFD 决定工作模式
//  注册键:Zlg + USBCANFD200U + CANFD 以及 Zlg + USBCANFD200U + CAN
// ==================================================================
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <canhw/IHardware.h>
#include <canhw/ZlgConfig.h>

#include "ZlgApi.h"

namespace canhw {
namespace zlg {

class ZlgCanFdWrapper : public IHardware {
public:
    ZlgCanFdWrapper() = default;
    ~ZlgCanFdWrapper() override { Close(); }

    ZlgCanFdWrapper(const ZlgCanFdWrapper&) = delete;
    ZlgCanFdWrapper& operator=(const ZlgCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    static std::shared_ptr<CANFrame> ParseFdFrame(const ReceiveFdData& rec);
    static std::shared_ptr<CANFrame> ParseCanFrame(const ReceiveData& rec);

    bool SendCan(CANFrame& canFrame);
    bool SendFd(CANFrame& canFrame);

    ZlgCanFDConfig config_;

    // Open 时的模式快照,会话期间收发 API 与缓冲区布局必须与其保持一致
    bool openedUseCanFD_ = true;

    void* devHandle_ = nullptr;
    void* channelHandle_ = nullptr;

    static constexpr size_t RxBatchSize = 100;
    // 按打开模式二选一分配(与 .NET 版按模式选择结构体布局一致)
    std::vector<ReceiveData> rxCanBuf_;
    std::vector<ReceiveFdData> rxFdBuf_;
    TransmitData txCanBuf_{};
    TransmitFdData txFdBuf_{};

    std::mutex lock_;
    std::atomic<bool> stop_{false};
    std::thread readThread_;
    bool isOpen_ = false;
};

} // namespace zlg
} // namespace canhw
