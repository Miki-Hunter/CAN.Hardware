#pragma once
#include <functional>

#include "Config.h"
#include "Frame.h"

namespace canhw {

/// 统一对外暴露的硬件接口(对应 .NET IHardware,5 个成员)
class IHardware {
public:
    /// 报文收发事件回调(接收与发送回显都会推送)
    using FrameHandler = std::function<void(const FrameBase&)>;

    virtual ~IHardware() = default;

    /// 用于对外推送报文(对应 .NET eventHandler 属性)
    /// 注意:回调在驱动内部锁内、读线程上下文中触发,回调内不要做阻塞/耗时操作。
    FrameHandler eventHandler;

    /// 硬件配置(用于硬件的初始化),对应 .NET Config 属性。
    /// 返回基类引用,使用方 static_cast 到具体配置类型(如 ZlgCANConfig&)。
    virtual BusConfigBase& Config() = 0;

    /// 硬件的 Open
    virtual bool Open() = 0;

    /// 硬件的 Close
    virtual bool Close() = 0;

    /// 报文的发送(frame 需为 CANFrame 派生实例;失败或类型不符返回 false,
    /// 非法参数抛 std::invalid_argument,不做静默失败)
    virtual bool Send(FrameBase& frame) = 0;

    /// 清除接口缓冲区的报文
    virtual void FlushReceiveBuffer() = 0;

    /// 清除发送缓冲区的报文
    virtual void FlushTransmitBuffer() = 0;
};

} // namespace canhw
