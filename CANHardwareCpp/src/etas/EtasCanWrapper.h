#pragma once
// ==================================================================
//  ETAS 经典 CAN 封装(BOA V2 / OCI,回调驱动接收,无轮询线程)
//  对应 .NET Entity/EtasCanWrapper.cs
//  注册键:Etas + Generic + CAN
// ==================================================================
#include <atomic>
#include <mutex>

#include <canhw/EtasConfig.h>
#include <canhw/IHardware.h>

#include "EtasBoaApi.h"

namespace canhw {
namespace etas {

class EtasCanWrapper : public IHardware {
public:
    EtasCanWrapper() = default;
    ~EtasCanWrapper() override { Close(); }

    EtasCanWrapper(const EtasCanWrapper&) = delete;
    EtasCanWrapper& operator=(const EtasCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    /// 解析硬件 URI:配置了 interfaceUri 直接使用,否则按 channelIndex 从 CSI 枚举结果中取
    std::string ResolveUri();
    /// OCI 接收回调(驱动线程),经 userData 定位本实例
    static void RxThunk(void* userData, void* msg);
    void OnRxMsg(void* msg);

    std::shared_ptr<CANFrame> Convert(const EtasCanRxMsg& rx) const;
    std::shared_ptr<CANFrame> Convert(const EtasCanFdRxMsg& rx) const;
    /// 硬件 tick 换算为微秒(拆商余计算避免 tick×1e6 溢出)
    uint64_t TicksToMicros(int64_t ticks) const;
    static int64_t BuildId(uint32_t frameID, uint16_t flags);

    EtasCANConfig config_;

    // BOA 句柄(UNDEFINED 表示未打开):控制器 + 发送队列 + 接收队列
    int32_t ctrlHandle_ = UNDEFINED;
    int32_t txHandle_ = UNDEFINED;
    int32_t rxHandle_ = UNDEFINED;

    // 硬件定时器频率(tick/秒),Open 时查询;0 表示查询失败,时间戳保持 0
    uint32_t tickFrequency_ = 0;

    std::mutex lock_;
    // 会被驱动回调线程读取,需原子
    std::atomic<bool> isOpen_{false};
};

} // namespace etas
} // namespace canhw
