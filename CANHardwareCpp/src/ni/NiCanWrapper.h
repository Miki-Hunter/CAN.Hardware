#pragma once
// ==================================================================
//  NI-XNET 经典 CAN 封装(:memory: 伪数据库 / Frame In-Out Stream)
//  对应 .NET Entity/NiCanWrapper.cs
//  注册键:Ni + Generic + CAN
// ==================================================================
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <canhw/IHardware.h>
#include <canhw/NiConfig.h>

#include "NiApi.h"

namespace canhw {
namespace ni {

class NiCanWrapper : public IHardware {
public:
    NiCanWrapper() = default;
    ~NiCanWrapper() override { Close(); }

    NiCanWrapper(const NiCanWrapper&) = delete;
    NiCanWrapper& operator=(const NiCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    /// 流内可能串联多帧,需逐帧解析
    void ParseFrames(const std::vector<uint8_t>& buffer, uint32_t bytesRead);

    NiCANConfig config_;

    // NI-XNET 会话句柄(0 表示未打开),输入/输出流各一个
    uint32_t inputSession_ = 0;
    uint32_t outputSession_ = 0;

    std::mutex lock_;
    std::atomic<bool> stop_{false};
    std::thread readThread_;
    bool isOpen_ = false;
};

} // namespace ni
} // namespace canhw
