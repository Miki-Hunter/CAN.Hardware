#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace canhw {

// ==================================================================
//  抽象帧与标志位(对应 .NET Interface/ 下的 FrameBase / CANFrame /
//  BusFrameFlags / CanFrameFlags)
// ==================================================================

/// 总线帧解析后的标志位信息基类。
/// 不同总线(CAN/LIN 等)解析结果不同,由各自的子类表达,
/// 通过 FrameBase::flags 对外暴露。
struct BusFrameFlags {
    /// 设备返回的原始 flag 值
    uint32_t rawFlags = 0;

    virtual ~BusFrameFlags() = default;

    /// 该帧是否为错误帧/包含错误
    virtual bool IsError() const = 0;

    /// 人类可读的标志位摘要(如 "0x00000123 [STD | FDF | BRS]")
    virtual std::string ToString() const = 0;
};

/// CAN/CANFD 总线帧解析后的标志位信息。
/// 同一总线不同厂商的解析结果一致,由厂商各自的解析器填充。
struct CanFrameFlags : BusFrameFlags {
    // ---------------- 帧类型 ----------------
    bool isRTR = false;            ///< 远程帧请求
    bool isStandardFrame = false;  ///< 标准帧(11-bit ID)
    bool isExtendedFrame = false;  ///< 扩展帧(29-bit ID)
    bool isWakeup = false;         ///< 唤醒消息
    bool isNerr = false;           ///< NERR 激活

    // ---------------- CAN FD 属性(经典 CAN 恒为 false) ----------------
    bool isCanFd = false;          ///< CAN FD 帧
    bool hasBrs = false;           ///< 数据段使用比特率切换
    bool hasEsi = false;           ///< 发送节点处于错误被动模式

    // ---------------- 收发状态 ----------------
    bool isTxAck = false;          ///< 发送确认(已真正发送到总线)
    bool isTxRequest = false;      ///< 发送请求(已交付 CAN 控制器等待发送)
    bool isLocalEcho = false;      ///< 本地回显(来自同一通道的另一个句柄)
    bool isSingleShot = false;     ///< 单次发送
    bool isTxFailed = false;       ///< 单次发送失败
    bool isArbitrationLost = false;///< 仲裁丢失(仅单次发送失败时有意义)
    bool isDelayMessage = false;   ///< 延迟消息(未发送到总线)

    // ---------------- 错误 ----------------
    bool isErrorFrame = false;     ///< 错误帧
    bool hasHwOverrun = false;     ///< 硬件缓冲区溢出
    bool hasSwOverrun = false;     ///< 软件缓冲区溢出
    bool hasOverrun = false;       ///< 任意溢出(硬件或软件)
    bool hasStuffError = false;    ///< 位填充错误
    bool hasFormError = false;     ///< 帧格式错误
    bool hasCrcError = false;      ///< CRC 校验错误
    bool hasBitError = false;      ///< 位错误(发送与读回电平不一致)
    bool hasBusError = false;      ///< 任意总线错误(填充/格式/CRC/位错误)

    bool IsError() const override {
        return isErrorFrame || hasOverrun || hasBusError;
    }

    std::string ToString() const override;
};

/// 不同硬件、总线协议数据帧的抽象
struct FrameBase {
    int64_t id = 0;
    std::vector<uint8_t> data;

    /// 解析后的标志位信息,主要应用在 Rx 报文中,
    /// 不同总线类型对应不同的子类(CAN/CANFD 为 CanFrameFlags),未解析时为 nullptr。
    /// 由驱动侧填充(接收解析后 / 发送回显前)。
    std::shared_ptr<const BusFrameFlags> flags;

    virtual ~FrameBase() = default;
};

/// CAN / CAN FD 帧
struct CANFrame : FrameBase {
    int dlc = 0;
    int rtr = 0;
    int flag = 0;          ///< 原始标志(厂商语义)
    uint64_t timestamp = 0;///< 时间戳,单位微秒
    int dir = 0;           ///< 0=TX, 1=RX
    bool fdf = false;      ///< FD 帧标志
    bool brs = false;      ///< 比特率切换标志
};

} // namespace canhw
