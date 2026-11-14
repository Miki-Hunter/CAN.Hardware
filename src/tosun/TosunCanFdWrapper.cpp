#include "TosunCanFdWrapper.h"

#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "TosunFlagParser.h"

namespace canhw {
namespace tosun {

bool ToSunCanFdWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    auto& api = TosunApi::Instance();
    void* tempHandle = nullptr;

    try {
        // 连接设备(内部完成库初始化,多实例共享设备句柄)
        uint32_t status =
            api.ConnectDevice(nullptr, config_.enableFIFO, config_.enableErrorFrame,
                              config_.enableTurbe, &tempHandle);
        if (status != 0)
            throw std::runtime_error("Connect device failed(" + api.ErrorText(status) +
                                     ")");

        // 设置波特率
        if (config_.enableCustomBitrate) {
            // 寄存器级自定义波特率配置
            status = api.ConfigureCanFdRegs(
                tempHandle, config_.channelIndex, config_.canFdArbitrationBitrate,
                config_.arbitrationPhase1, config_.arbitrationPhase2,
                config_.arbitrationPrescale, config_.arbitrationSjw,
                config_.canFdDataBitrate, config_.dataPhase1, config_.dataPhase2,
                config_.dataPrescale, config_.dataSjw,
                static_cast<int32_t>(config_.controllerType),
                static_cast<int32_t>(config_.controllerMode), config_.enableA120Ohm);
        } else {
            status = api.ConfigCanFdByBaudrate(
                tempHandle, config_.channelIndex, config_.canFdArbitrationBitrate,
                config_.canFdDataBitrate, static_cast<int32_t>(config_.controllerType),
                static_cast<int32_t>(config_.controllerMode),
                config_.enableA120Ohm ? 1u : 0u);
        }
        if (status != 0)
            throw std::runtime_error("Set canfd baudrate failed(" +
                                     api.ErrorText(status) + ")");

        // 设置过滤器(逐 ID 白名单,ID<=0x7FF 为标准帧)
        if (config_.enableFilter) {
            for (uint32_t id : config_.filterIdArray) {
                status = api.AddPassFilter(tempHandle, config_.channelIndex,
                                           static_cast<int32_t>(id),
                                           id <= CanFrameDef::CAN_STD_MAX_ID_FLAG);
                if (status != 0)
                    throw std::runtime_error("Set frame filter failed(" +
                                             api.ErrorText(status) + ")");
            }
        }

        // 注册接收事件回调(按通道路由到本实例)
        const uint8_t channel = config_.channelIndex;
        TosunDispatch::SetCanFdHandler(
            channel, [this, channel](const TLibCanFd& canFdData) {
                OnCanFdEvent(canFdData);
            });
        status = api.RegisterEventCanFd(tempHandle, &TosunDispatch::CanFdThunk);
        if (status != 0) {
            TosunDispatch::ClearCanFdHandler(channel);
            throw std::runtime_error("Register canfd event failed(" +
                                     api.ErrorText(status) + ")");
        }

        // 所有操作成功后,才提交状态
        handle_ = tempHandle;
        isOpen_ = true;
        return true;
    } catch (const std::exception& ex) {
        // 清理已打开的资源
        TosunDispatch::ClearCanFdHandler(config_.channelIndex);
        if (tempHandle != nullptr) {
            try {
                api.DisconnectDevice();
            } catch (...) {
            }
        }
        isOpen_ = false;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool ToSunCanFdWrapper::Close() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_) return true;
    isOpen_ = false;

    auto& api = TosunApi::Instance();
    // 先注销事件回调
    if (handle_ != nullptr) {
        try {
            api.UnregisterEventCanFd(handle_, &TosunDispatch::CanFdThunk);
        } catch (...) {
        }
        TosunDispatch::ClearCanFdHandler(config_.channelIndex);
    }

    // 断开设备(库级 + 设备级引用计数,仅最后一个实例真正断开)
    if (handle_ != nullptr) {
        try {
            api.DisconnectDevice();
        } catch (...) {
        }
        handle_ = nullptr;
    }
    return true;
}

void ToSunCanFdWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (handle_ != nullptr)
        TosunApi::Instance().ClearCanFdReceiveBuffers(handle_, config_.channelIndex);
}

void ToSunCanFdWrapper::FlushTransmitBuffer() {
    // libTSCAN 没有清除发送缓冲区的 API,空操作(与 .NET 一致)
}

void ToSunCanFdWrapper::OnCanFdEvent(const TLibCanFd& canFdData) {
    // 回调运行在驱动线程,异常不能向外抛
    try {
        if (!isOpen_) return;
        // 事件回调是设备级的,仅处理本通道的报文
        if (canFdData.idxChn != config_.channelIndex) return;
        // 丢弃 TX 帧(发送回显由 Send 成功后自行触发)
        if ((canFdData.properties & PROP_TX) != 0) return;

        auto frame = std::make_shared<CANFrame>();
        // 将 EFF/RTR/ERR 标志合成到 ID 高位(SocketCAN 风格)
        int64_t id =
            static_cast<uint32_t>(canFdData.identifier) & CanFrameDef::CAN_ID_FLAG;
        if ((canFdData.properties & PROP_EXT) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
        if ((canFdData.properties & PROP_REMOTE) != 0) id |= CanFrameDef::CAN_RTR_FLAG;
        if ((canFdData.properties & PROP_ERROR) != 0) id |= CanFrameDef::CAN_ERR_FLAG;
        frame->id = id;
        // DLC 编码换算为实际数据长度(经典帧 0-8 恒等映射,FD 帧 0-15 映射到 0-64)
        frame->dlc = DataLenConverter::DlcToLength(canFdData.dlc);
        frame->flag = canFdData.properties;
        frame->flags = ToSunCanFlagParser::Parse(canFdData.properties,
                                                 canFdData.fdProperties);
        frame->timestamp = canFdData.timeUs;
        frame->fdf = (canFdData.fdProperties & FD_PROP_FDF) != 0;
        frame->brs = (canFdData.fdProperties & FD_PROP_BRS) != 0;
        frame->data.assign(canFdData.data, canFdData.data + frame->dlc);
        frame->dir = 1;

        if (eventHandler) eventHandler(*frame);
    } catch (...) {
    }
}

bool ToSunCanFdWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || handle_ == nullptr) return false;

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
        auto& api = TosunApi::Instance();

        canFrame->dir = 0;
        const uint32_t validId = CanFrameHelper::GetId(canFrame->id);
        const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);
        const bool isRTR = CanFrameHelper::IsRTR(canFrame->id);

        TLibCanFd canfdMsg{};
        canfdMsg.idxChn = config_.channelIndex;
        canfdMsg.properties = PROP_TX;
        if (isExtended) canfdMsg.properties |= PROP_EXT;
        if (isRTR) canfdMsg.properties |= PROP_REMOTE;
        // 实际数据长度换算为 DLC 编码(0-15)
        canfdMsg.dlc = static_cast<uint8_t>(DataLenConverter::LengthToDlc(canFrame->dlc));
        canfdMsg.fdProperties = 0;
        if (canFrame->fdf) canfdMsg.fdProperties |= FD_PROP_FDF;
        if (canFrame->brs) canfdMsg.fdProperties |= FD_PROP_BRS;
        canfdMsg.identifier = static_cast<int32_t>(validId);
        canfdMsg.timeUs = 0;
        std::memcpy(canfdMsg.data, canFrame->data.data(), canFrame->dlc);

        const uint32_t status = api.TransmitCanFdAsync(handle_, &canfdMsg);
        if (status != 0) return false;

        canFrame->flags = ToSunCanFlagParser::Parse(canfdMsg.properties,
                                                    canfdMsg.fdProperties);
        if (eventHandler) eventHandler(*canFrame);
        return true;
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

} // namespace tosun
} // namespace canhw
