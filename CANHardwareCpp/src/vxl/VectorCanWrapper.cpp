#include "VectorCanWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "VectorFlagParser.h"

namespace canhw {
namespace vxl {

using namespace std::chrono_literals;

bool VectorCanWrapper::IsPlausibleTiming(const XLcanBusParamsInfo& p) {
    if (p.tseg1 < 2 || p.tseg2 < 1) return false;
    if (p.sjw < 1 || p.sjw > 4 || p.sjw > p.tseg2 + 1) return false;
    if (p.sam != 1 && p.sam != 3) return false;
    if (p.tseg1 < p.tseg2) return false;
    const double samplePoint =
        static_cast<double>(1 + p.tseg1) / (1 + p.tseg1 + p.tseg2);
    return samplePoint >= 0.5 && samplePoint <= 0.95;
}

bool VectorCanWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    auto& api = VectorApi::Instance();
    int32_t tempPort = -1;
    bool driverOpened = false;

    try {
        // 打开驱动(引用计数,多实例共享)
        int16_t status = api.OpenDriver();
        if (status != SUCCESS)
            throw std::runtime_error("Open driver failed(" +
                                     VectorApi::StatusText(status) + ")");
        driverOpened = true;

        // 按硬件型号+硬件通道号在驱动配置中定位通道,得到 accessMask
        const uint32_t hwChannel = config_.hwChannel;
        XLchannelConfig channelConfig{};
        if (!api.TryGetChannel(config_.hardwareType, hwChannel, channelConfig)) {
            std::string hwTypeText =
                std::to_string(static_cast<uint32_t>(config_.hardwareType));
            throw std::runtime_error(
                "Can not find channel(HwType=" + hwTypeText +
                ", Channel=" + std::to_string(hwChannel) + "). Available: " +
                api.GetCanChannelsText(config_.hardwareType));
        }

        accessMask_ = channelConfig.channelMask;

        const std::string appName = "CAN.Hardware_" + std::to_string(hwChannel);
        status = api.SetApplConfig(appName.c_str(), 0,
                                   static_cast<uint64_t>(config_.hardwareType),
                                   channelConfig.hwIndex, channelConfig.hwChannel,
                                   XL_BUS_TYPE_CAN);
        if (status != SUCCESS)
            throw std::runtime_error("Set appl config failed(" +
                                     VectorApi::StatusText(status) + ")");

        // 打开端口;permissionMask 回传本应用实际获得的通道权限,
        // 若通道已被其他应用(CANoe 等)占用初始化权限,permissionMask 会小于 accessMask
        uint64_t permissionMask = accessMask_;
        status = api.OpenPort(&tempPort, appName.c_str(), accessMask_,
                              &permissionMask, 4096, XL_INTERFACE_V3, XL_BUS_TYPE_CAN);
        if (status != SUCCESS)
            throw std::runtime_error("Open port failed(" + VectorApi::StatusText(status) +
                                     ")");

        // 波特率设置需要初始化权限(通道被其他应用占用时此处拦截)
        if (accessMask_ != permissionMask)
            throw std::runtime_error(std::string("Init access required to initialize CAN bitrate(") +
                                     VectorApi::StatusText(ERR_INIT_ACCESS_MISSING) + ")");

        // 关闭发送回显事件(TX_REQUEST/TX_COMPLETED),发送帧由 Send 直接上报
        status = api.CanSetChannelMode(tempPort, accessMask_, 0, 0);
        if (status != SUCCESS)
            throw std::runtime_error("Set channel mode failed(" +
                                     VectorApi::StatusText(status) + ")");

        if (config_.enableCustomBitrate) {
            XLchipParams chipParams{};
            chipParams.bitRate = config_.customBitrate;
            chipParams.sjw = static_cast<uint8_t>(config_.sjw);
            chipParams.tseg1 = static_cast<uint8_t>(config_.tseg1);
            chipParams.tseg2 = static_cast<uint8_t>(config_.tseg2);
            chipParams.sam = static_cast<uint8_t>(config_.sam);
            chipParams.outputMode = XL_OUTPUT_MODE_NORMAL;
            status = api.CanSetChannelParams(tempPort, accessMask_, &chipParams);
            if (status != SUCCESS)
                throw std::runtime_error("Set custom bus params failed(" +
                                         VectorApi::StatusText(status) + ")");
        } else {
            status = api.CanSetChannelBitrate(
                tempPort, accessMask_, static_cast<uint32_t>(config_.canBitrate));
            if (status != SUCCESS)
                throw std::runtime_error("Set bitrate failed(" +
                                         VectorApi::StatusText(status) + ")");
        }

        // 读回校验:确认驱动中实际生效的波特率与请求一致
        XLcanBusParamsInfo busParams{};
        bool hasBusParams = api.TryReadBusParams(accessMask_, busParams);
        if (hasBusParams && busParams.bitRate != 0) {
            const uint32_t requested =
                config_.enableCustomBitrate
                    ? config_.customBitrate
                    : static_cast<uint32_t>(config_.canBitrate);
            if (busParams.bitRate != requested) {
                throw std::runtime_error(
                    "Bitrate verify failed on '" + busParams.channelName +
                    "': requested=" + std::to_string(requested) +
                    ", effective=" + std::to_string(busParams.bitRate) +
                    "(tseg1=" + std::to_string(busParams.tseg1) +
                    ",tseg2=" + std::to_string(busParams.tseg2) +
                    ",sjw=" + std::to_string(busParams.sjw) + ")");
            }
        }

        // 时序合理性校验:xlCanSetChannelBitrate 只改波特率,不修正 tseg/sjw;
        // 若驱动中该应用的时序参数无效,发出的帧无人 ACK,必须用完整时序重配
        if (hasBusParams && !IsPlausibleTiming(busParams)) {
            XLchipParams fixParams{};
            fixParams.bitRate = config_.enableCustomBitrate
                                    ? config_.customBitrate
                                    : static_cast<uint32_t>(config_.canBitrate);
            fixParams.tseg1 = 12;
            fixParams.tseg2 = 3;
            fixParams.sjw = 2;
            fixParams.sam = 1;
            fixParams.outputMode = XL_OUTPUT_MODE_NORMAL;
            status = api.CanSetChannelParams(tempPort, accessMask_, &fixParams);
            if (status != SUCCESS)
                throw std::runtime_error(
                    "Bus timing on '" + busParams.channelName + "' invalid(tseg1=" +
                    std::to_string(busParams.tseg1) +
                    ",tseg2=" + std::to_string(busParams.tseg2) +
                    ",sjw=" + std::to_string(busParams.sjw) + ") and fix failed(" +
                    VectorApi::StatusText(status) + ")");
        }

        // 设置过滤器(Vector 使用 Code/Mask 掩码过滤)
        if (config_.enableFilter) {
            status = api.CanSetAcceptanceFilter(
                tempPort, accessMask_, config_.filterCode, config_.filterMask,
                config_.filterIsExtended ? 2u : 1u);
            if (status != SUCCESS)
                throw std::runtime_error("Set frame filter failed(" +
                                         VectorApi::StatusText(status) + ")");
        }

        // 激活通道(RESET_CLOCK 复位时间基准)
        status = api.ActivateChannel(tempPort, accessMask_, XL_BUS_TYPE_CAN,
                                     XL_ACTIVATE_RESET_CLOCK);
        if (status != SUCCESS)
            throw std::runtime_error("Activate channel failed(" +
                                     VectorApi::StatusText(status) + ")");

        // 请求一次芯片状态(经接收事件上报 busStatus/txErr/rxErr),便于诊断总线状态
        api.CanRequestChipState(tempPort, accessMask_);

        // 所有操作成功后,才提交状态
        portHandle_ = tempPort;
        tempPort = -1;
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&VectorCanWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 清理已打开的资源: deactivate -> closePort -> CloseDriver
        if (tempPort >= 0) {
            try {
                api.DeactivateChannel(tempPort, accessMask_);
                api.ClosePort(tempPort);
            } catch (...) {
            }
        }
        if (driverOpened) api.CloseDriver();
        portHandle_ = -1;
        isOpen_ = false;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool VectorCanWrapper::Close() {
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    readThread_.Join();

    canhw::LockGuard lock(lock_);
    auto& api = VectorApi::Instance();
    if (portHandle_ >= 0) {
        try {
            api.DeactivateChannel(portHandle_, accessMask_);
            api.ClosePort(portHandle_);
        } catch (...) {
        }
        portHandle_ = -1;
    }
    api.CloseDriver();
    isOpen_ = false;
    return true;
}

void VectorCanWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (portHandle_ >= 0) VectorApi::Instance().FlushReceiveQueue(portHandle_);
}

void VectorCanWrapper::FlushTransmitBuffer() {
    canhw::LockGuard lock(lock_);
    if (portHandle_ >= 0)
        VectorApi::Instance().CanFlushTransmitQueue(portHandle_, accessMask_);
}

void VectorCanWrapper::ReadTaskProc() {
    while (!stop_) {
        try {
            bool res = false;
            do {
                if (stop_) break;
                res = Read();
            } while (res && !stop_);

            if (!stop_) canhw::SleepMs(5);
        } catch (...) {
            if (!stop_) canhw::SleepMs(10);
        }
    }
}

bool VectorCanWrapper::Read() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || portHandle_ < 0) return false;

    auto& api = VectorApi::Instance();

    XLevent xlEvent{};
    uint32_t eventCount = 1;
    const int16_t status = api.Receive(portHandle_, &eventCount, &xlEvent);
    // 接收队列为空(ERR_QUEUE_IS_EMPTY)或其他错误时结束本轮读取
    if (status != SUCCESS || eventCount == 0) return false;

    // 芯片状态事件:上报 busStatus/txErr/rxErr(位于 id 低 3 字节)
    if (xlEvent.tag == XL_CHIP_STATE) {
        auto chipFrame = std::make_shared<CANFrame>();
        chipFrame->id = CanFrameDef::CAN_ERR_FLAG;
        chipFrame->dlc = 3;
        chipFrame->data = {static_cast<uint8_t>(xlEvent.id & 0xFF),
                           static_cast<uint8_t>((xlEvent.id >> 8) & 0xFF),
                           static_cast<uint8_t>((xlEvent.id >> 16) & 0xFF)};
        chipFrame->dir = 1;
        chipFrame->flag = static_cast<int>(MsgFlags::ERROR_FRAME);
        chipFrame->flags =
            VectorCanFlagParser::Parse(MsgFlags::ERROR_FRAME, 0);
        // XL 时间戳单位为 ns,统一转换为 µs
        chipFrame->timestamp = xlEvent.timeStamp / 1000;
        if (eventHandler) eventHandler(*chipFrame);
        return true;
    }
    if (xlEvent.tag != XL_RECEIVE_MSG) return false;
    // 过滤发送回显(TX_COMPLETED/TX_REQUEST),发送帧已在 Send 中上报
    if ((xlEvent.msgFlags & (MsgFlags::TX_COMPLETED | MsgFlags::TX_REQUEST)) != 0)
        return false;

    // 钳制 dlc 到经典 CAN 有效范围
    const int len =
        std::min<int>(xlEvent.dlc, DataLenConverter::MaxCanDataLength);
    auto frame = std::make_shared<CANFrame>();
    // 将 EFF/RTR/ERR 标志合成到 ID 高位(SocketCAN 风格)
    int64_t id = xlEvent.id & XL_CAN_ID_MASK;
    if ((xlEvent.id & XL_CAN_EXT_MSG_ID) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
    if ((xlEvent.msgFlags & MsgFlags::REMOTE_FRAME) != 0)
        id |= CanFrameDef::CAN_RTR_FLAG;
    if ((xlEvent.msgFlags & MsgFlags::ERROR_FRAME) != 0)
        id |= CanFrameDef::CAN_ERR_FLAG;
    frame->id = id;
    frame->dlc = len;
    frame->dir = 1;
    frame->flag = xlEvent.msgFlags;
    frame->flags = VectorCanFlagParser::Parse(xlEvent.msgFlags, xlEvent.id);
    // XL 时间戳单位为 ns,统一转换为 µs
    frame->timestamp = xlEvent.timeStamp / 1000;
    // XLevent.data 为 8 字节小端
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&xlEvent.data);
    frame->data.assign(raw, raw + len);

    if (eventHandler) eventHandler(*frame);
    return true;
}

bool VectorCanWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || portHandle_ < 0) return false;

    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    // 发送前校验数据长度合法性(非法参数直接抛异常,不做静默失败)
    if (canFrame->fdf || canFrame->brs)
        throw std::invalid_argument(
            "Classic CAN channel does not support CANFD frame(FDF/BRS).");
    if (!DataLenConverter::IsValidCanLength(canFrame->dlc))
        throw std::invalid_argument("Invalid CAN DLC(" + std::to_string(canFrame->dlc) +
                                    "), valid range: 0-" +
                                    std::to_string(DataLenConverter::MaxCanDataLength) +
                                    ".");
    if (static_cast<int>(canFrame->data.size()) < canFrame->dlc)
        throw std::invalid_argument("Data length(" +
                                    std::to_string(canFrame->data.size()) +
                                    ") is less than DLC(" +
                                    std::to_string(canFrame->dlc) + ").");

    try {
        auto& api = VectorApi::Instance();

        canFrame->dir = 0;
        uint32_t validId = CanFrameHelper::GetId(canFrame->id);
        const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);
        const bool isRtr = CanFrameHelper::IsRTR(canFrame->id);
        if (isExtended) validId |= XL_CAN_EXT_MSG_ID;

        XLevent xlEvent{};
        xlEvent.tag = XL_TRANSMIT_MSG;
        // 发送通路由 xlCanTransmit 的 accessMask 决定,事件结构体的 channelIndex 保持 0
        // (与 Vector 官方 xlCANdemo 及 python-can 一致)
        xlEvent.id = validId;
        xlEvent.msgFlags = isRtr ? MsgFlags::REMOTE_FRAME : MsgFlags::NONE;
        xlEvent.dlc = static_cast<uint16_t>(canFrame->dlc);
        uint64_t dataValue = 0;
        std::memcpy(&dataValue, canFrame->data.data(), canFrame->dlc);
        xlEvent.data = dataValue;

        uint32_t msgCnt = 1;
        const int16_t status =
            api.CanTransmit(portHandle_, accessMask_, &msgCnt, &xlEvent);
        if (status != SUCCESS || msgCnt != 1) return false;

        // 发送成功后回显,附加 TX_COMPLETED 标志
        canFrame->flags = VectorCanFlagParser::Parse(
            xlEvent.msgFlags | MsgFlags::TX_COMPLETED, validId);
        if (eventHandler) eventHandler(*canFrame);
        return true;
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

} // namespace vxl
} // namespace canhw
