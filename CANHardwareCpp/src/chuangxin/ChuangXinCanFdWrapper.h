#pragma once
// ==================================================================
//  创芯科技 USBCANFD 封装(ControlCANFD.dll / ZCAN_ API)
//  对应 .NET Entity/ChuangXinCanFdWrapper.cs
//  适用:USBCANFD-200U/100U/MINI;同时支持 CANFD 与经典 CAN,
//  由 ChuangXinCanFDConfig::useCanFD 决定是否收发 FD 帧。
//  注册键:ChuangXin + USBCANFD + CANFD 与 ChuangXin + USBCANFD + CAN
// ==================================================================
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <canhw/ChuangXinConfig.h>
#include <canhw/IHardware.h>

#include "ChuangXinCanFdApi.h"

namespace canhw {
namespace chuangxin {

class ChuangXinCanFdWrapper : public IHardware {
public:
    ChuangXinCanFdWrapper() = default;
    ~ChuangXinCanFdWrapper() override { Close(); }

    ChuangXinCanFdWrapper(const ChuangXinCanFdWrapper&) = delete;
    ChuangXinCanFdWrapper& operator=(const ChuangXinCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    static std::shared_ptr<CANFrame> ParseCanFrame(const ZcanReceiveData& rec);
    static std::shared_ptr<CANFrame> ParseFdFrame(const ZcanReceiveFdData& rec);

    bool SendCan(CANFrame& canFrame);
    bool SendFd(CANFrame& canFrame);

    ChuangXinCanFDConfig config_;

    // Open 时的设备参数/模式快照,会话期间固定使用
    uint32_t openedDeviceType_ = 0;
    uint32_t openedDeviceIndex_ = 0;
    uint32_t openedChannel_ = 0;
    void* openedChannelHandle_ = nullptr;
    bool openedUseCanFD_ = true;

    static constexpr size_t RxBatchSize = 100;
    // 按需分配:经典队列始终分配,FD 队列在 FD 模式下也分配(.NET 两者都分配)
    std::vector<ZcanReceiveData> rxCanBuf_;
    std::vector<ZcanReceiveFdData> rxFdBuf_;
    ZcanTransmitData txCanBuf_{};
    ZcanTransmitFdData txFdBuf_{};

    std::mutex lock_;
    std::atomic<bool> stop_{false};
    std::thread readThread_;
    bool isOpen_ = false;
};

} // namespace chuangxin
} // namespace canhw
