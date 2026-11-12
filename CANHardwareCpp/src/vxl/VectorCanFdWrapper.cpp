#include "VectorCanFdWrapper.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "VectorFlagParser.h"

namespace canhw {
namespace vxl {

using namespace std::chrono_literals;

void VectorCanFdWrapper::ValidateFdTiming(const char* segment, uint32_t bitrate,
                                          int tseg1, int tseg2, int sjw) {
    if (bitrate == 0)
        throw std::invalid_argument(std::string("Invalid CANFD ") + segment +
                                    " bitrate(0).");
    if (tseg1 < 1 || tseg2 < 1 || tseg1 < tseg2)
        throw std::invalid_argument(
            std::string("Invalid CANFD ") + segment + " timing: tseg1(" +
            std::to_string(tseg1) + ")/tseg2(" + std::to_string(tseg2) +
            ") out of range(tseg1>=tseg2>=1).");
    if (sjw < 1 || sjw > tseg2)
        throw std::invalid_argument(
            std::string("Invalid CANFD ") + segment + " timing: sjw(" +
            std::to_string(sjw) + ") out of range(1~tseg2=" +
            std::to_string(tseg2) + ").");
    const double samplePoint =
        static_cast<double>(1 + tseg1) / (1 + tseg1 + tseg2);
    if (samplePoint < 0.5 || samplePoint > 0.95)
        throw std::invalid_argument(
            std::string("Invalid CANFD ") + segment +
            " timing: sample point(" + std::to_string(static_cast<int>(samplePoint * 100)) +
            "%) out of range(50%~95%).");
}

bool VectorCanFdWrapper::Open() {
    std::lock_guard<std::mutex> lock(lock_);
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

        // CAN FD 使用 V4 接口,队列大小参考 Vector 示例取 16k;
        // permissionMask 回传本应用实际获得的通道权限
        uint64_t permissionMask = accessMask_;
        status = api.OpenPort(&tempPort, appName.c_str(), accessMask_,
                              &permissionMask, 16384, XL_INTERFACE_V4,
                              XL_BUS_TYPE_CAN);
        if (status != SUCCESS)
            throw std::runtime_error("Open port failed(" + VectorApi::StatusText(status) +
                                     ")");

        // 关闭发送回显事件(TX_REQUEST/TX_OK),发送帧由 Send 直接上报
        status = api.CanSetChannelMode(tempPort, accessMask_, 0, 0);
        if (status != SUCCESS)
            throw std::runtime_error("Set channel mode failed(" +
                                     VectorApi::StatusText(status) + ")");

        // CAN FD 波特率设置需要初始化权限(通道被其他应用占用时此处拦截)
        if (accessMask_ != permissionMask)
            throw std::runtime_error(
                std::string("Init access required to initialize CANFD bitrate(") +
                VectorApi::StatusText(ERR_INIT_ACCESS_MISSING) + ")");

        // 标准波特率走内置时序表(BuildFdConf),自定义波特率校验时序合法性后下发
        if (config_.enableCustomBitrate) {
            ValidateFdTiming("arbitration", config_.customArbitrationBitrate,
                             config_.arbitrationTseg1, config_.arbitrationTseg2,
                             config_.arbitrationSjw);
            ValidateFdTiming("data", config_.customDataBitrate, config_.dataTseg1,
                             config_.dataTseg2, config_.dataSjw);
        }
        const XLcanFdConf fdConf =
            config_.enableCustomBitrate
                ? VectorApi::BuildFdConfCustom(
                      config_.customArbitrationBitrate, config_.arbitrationTseg1,
                      config_.arbitrationTseg2, config_.arbitrationSjw,
                      config_.customDataBitrate, config_.dataTseg1, config_.dataTseg2,
                      config_.dataSjw, config_.enableNonIso)
                : VectorApi::BuildFdConf(
                      static_cast<uint32_t>(config_.canFdArbitrationBitrate),
                      static_cast<uint32_t>(config_.canFdDataBitrate),
                      config_.enableNonIso);

        status = api.CanFdSetConfiguration(tempPort, accessMask_, &fdConf);
        if (status != SUCCESS)
            throw std::runtime_error("Set canfd configuration failed(" +
                                     VectorApi::StatusText(status) + ")");

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

        // 请求一次芯片状态(经接收事件上报 busStatus/txErr/rxErr)
        api.CanRequestChipState(tempPort, accessMask_);

        // 所有操作成功后,才提交状态
        portHandle_ = tempPort;
        tempPort = -1;
        isOpen_ = true;
        stop_ = false;
        readThread_ = std::thread(&VectorCanFdWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
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

bool VectorCanFdWrapper::Close() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    if (readThread_.joinable()) readThread_.join();

    std::lock_guard<std::mutex> lock(lock_);
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

void VectorCanFdWrapper::FlushReceiveBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (portHandle_ >= 0) VectorApi::Instance().FlushReceiveQueue(portHandle_);
}

void VectorCanFdWrapper::FlushTransmitBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (portHandle_ >= 0)
        VectorApi::Instance().CanFlushTransmitQueue(portHandle_, accessMask_);
}

void VectorCanFdWrapper::ReadTaskProc() {
    while (!stop_) {
        try {
            bool res = false;
            do {
                if (stop_) break;
                res = Read();
            } while (res && !stop_);

            if (!stop_) std::this_thread::sleep_for(5ms);
        } catch (...) {
            if (!stop_) std::this_thread::sleep_for(10ms);
        }
    }
}

bool VectorCanFdWrapper::Read() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || portHandle_ < 0) return false;

    auto& api = VectorApi::Instance();

    XLcanRxEvent rxEvent{};
    rxEvent.size = static_cast<uint32_t>(sizeof(XLcanRxEvent));
    const int16_t status = api.CanReceive(portHandle_, &rxEvent);
    // 接收队列为空(ERR_QUEUE_IS_EMPTY)或其他错误时结束本轮读取
    if (status != SUCCESS) return false;

    // 芯片状态事件:上报 busStatus/txErr/rxErr(位于 canId 低 3 字节)
    if (rxEvent.tag == EV_TAG_CHIP_STATE) {
        auto chipFrame = std::make_shared<CANFrame>();
        chipFrame->id = CanFrameDef::CAN_ERR_FLAG;
        chipFrame->dlc = 3;
        chipFrame->data = {static_cast<uint8_t>(rxEvent.canId & 0xFF),
                           static_cast<uint8_t>((rxEvent.canId >> 8) & 0xFF),
                           static_cast<uint8_t>((rxEvent.canId >> 16) & 0xFF)};
        chipFrame->dir = 1;
        chipFrame->flag = static_cast<int>(FdRxFlags::EF);
        chipFrame->flags = VectorCanFlagParser::ParseFd(FdRxFlags::EF, 0);
        // XL 时间戳单位为 ns,统一转换为 µs
        chipFrame->timestamp = rxEvent.timeStamp / 1000;
        if (eventHandler) eventHandler(*chipFrame);
        return true;
    }
    // 仅处理接收事件,TX_OK/TX_REQUEST 等事件忽略(发送帧已在 Send 中上报)
    if (rxEvent.tag != EV_TAG_RX_OK) return false;

    // DLC 编码换算为实际数据长度,并钳制到 CANFD 有效范围
    const int len = std::min<int>(DataLenConverter::DlcToLength(rxEvent.dlc),
                                  DataLenConverter::MaxCanFdDataLength);
    auto frame = std::make_shared<CANFrame>();
    // 将 EFF/RTR/ERR 标志合成到 ID 高位(SocketCAN 风格)
    int64_t id = rxEvent.canId & XL_CAN_ID_MASK;
    if ((rxEvent.canId & XL_CAN_EXT_MSG_ID) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
    if ((rxEvent.msgFlags & FdRxFlags::RTR) != 0) id |= CanFrameDef::CAN_RTR_FLAG;
    if ((rxEvent.msgFlags & FdRxFlags::EF) != 0) id |= CanFrameDef::CAN_ERR_FLAG;
    frame->id = id;
    frame->dlc = len;
    frame->dir = 1;
    frame->flag = static_cast<int>(rxEvent.msgFlags);
    frame->flags = VectorCanFlagParser::ParseFd(rxEvent.msgFlags, rxEvent.canId);
    frame->fdf = (rxEvent.msgFlags & FdRxFlags::EDL) != 0;
    frame->brs = (rxEvent.msgFlags & FdRxFlags::BRS) != 0;
    // XL 时间戳单位为 ns,统一转换为 µs
    frame->timestamp = rxEvent.timeStamp / 1000;
    frame->data.assign(rxEvent.data, rxEvent.data + len);

    if (eventHandler) eventHandler(*frame);
    return true;
}

bool VectorCanFdWrapper::Send(FrameBase& frameBase) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || portHandle_ < 0) return false;

    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    // 发送前校验数据长度合法性(非法参数直接抛异常,不做静默失败)
    if (canFrame->brs && !canFrame->fdf)
        throw std::invalid_argument(
            "BRS is only valid for CANFD frame(FDF must be set).");
    const bool validLength = canFrame->fdf
                                 ? DataLenConverter::IsValidCanFdLength(canFrame->dlc)
                                 : DataLenConverter::IsValidCanLength(canFrame->dlc);
    if (!validLength)
        throw std::invalid_argument(
            canFrame->fdf ? "Invalid CANFD DLC(" + std::to_string(canFrame->dlc) +
                               "), valid lengths: 0-8,12,16,20,24,32,48,64."
                         : "Invalid CAN DLC(" + std::to_string(canFrame->dlc) +
                               "), valid range: 0-" +
                               std::to_string(DataLenConverter::MaxCanDataLength) + ".");
    if (static_cast<int>(canFrame->data.size()) < canFrame->dlc)
        throw std::invalid_argument("Data length(" + std::to_string(canFrame->data.size()) +
                                    ") is less than DLC(" +
                                    std::to_string(canFrame->dlc) + ").");

    try {
        auto& api = VectorApi::Instance();

        canFrame->dir = 0;
        uint32_t validId = CanFrameHelper::GetId(canFrame->id);
        const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);
        const bool isRtr = CanFrameHelper::IsRTR(canFrame->id);
        if (isExtended) validId |= XL_CAN_EXT_MSG_ID;

        uint32_t txFlags = TxMsgFlags::NONE;
        if (canFrame->fdf) txFlags |= TxMsgFlags::EDL;
        if (canFrame->brs) txFlags |= TxMsgFlags::BRS;
        if (isRtr) txFlags |= TxMsgFlags::RTR;

        XLcanTxEvent txEvent{};
        txEvent.tag = EV_TAG_TX_MSG;
        txEvent.transId = 0xFFFF;
        // 发送通路由 xlCanTransmitEx 的 accessMask 决定,channelIndex 保持 0
        txEvent.id = validId;
        txEvent.flags = txFlags;
        // 实际数据长度换算为 DLC 编码(0-15)
        txEvent.dlc = static_cast<uint8_t>(DataLenConverter::LengthToDlc(canFrame->dlc));
        std::memcpy(txEvent.data, canFrame->data.data(), canFrame->dlc);

        uint32_t msgCntSent = 0;
        const int16_t status = api.CanTransmitEx(portHandle_, accessMask_, 1,
                                                 &msgCntSent, &txEvent);
        if (status != SUCCESS || msgCntSent != 1) return false;

        // 发送成功后回显,标记发送确认
        canFrame->flags = VectorCanFlagParser::ParseFd(txFlags, validId, true);
        if (eventHandler) eventHandler(*canFrame);
        return true;
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

} // namespace vxl
} // namespace canhw
