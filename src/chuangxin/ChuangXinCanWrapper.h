#pragma once
// ==================================================================
//  创芯科技经典 CAN 封装(ControlCAN.dll / VCI_ API)
//  对应 .NET Entity/ChuangXinCanWrapper.cs
//  适用:USBCAN-2A/2C、CANalyst-II 至尊版/顶配/顶配Pro、OBD 版
//  注册键:ChuangXin + USBCAN + CAN
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>
#include <vector>

#include <canhw/ChuangXinConfig.h>
#include <canhw/IHardware.h>

#include "ChuangXinCanApi.h"

namespace canhw {
namespace chuangxin {

class ChuangXinCanWrapper : public IHardware {
public:
    ChuangXinCanWrapper() = default;
    ~ChuangXinCanWrapper() override { Close(); }

    ChuangXinCanWrapper(const ChuangXinCanWrapper&) = delete;
    ChuangXinCanWrapper& operator=(const ChuangXinCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    bool Read();

    ChuangXinCANConfig config_;

    // Open 时的设备参数快照,会话期间固定使用
    uint32_t openedDeviceType_ = 0;
    uint32_t openedDeviceIndex_ = 0;
    uint32_t openedChannel_ = 0;

    // 接收缓冲区在 Open 时一次性分配,Close 时释放
    static constexpr size_t RxBatchSize = 100;  // 单次最多读取的帧数
    std::vector<VciCanObj> rxBuf_;

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    canhw::Thread readThread_;
    bool isOpen_ = false;
};

} // namespace chuangxin
} // namespace canhw
