#include "TosunCanWrapper.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "TosunFlagParser.h"

namespace canhw {
namespace tosun {

bool ToSunCanWrapper::Open() {
    std::lock_guard<std::mutex> lock(lock_);
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

        // 设置波特率(波特率直接用 Kbps 数值)
        status = api.ConfigCanByBaudrate(tempHandle, config_.channelIndex,
                                         config_.canBitrate,
                                         config_.enableA120Ohm ? 1u : 0u);
        if (status != 0)
            throw std::runtime_error("Set can baudrate failed(" + api.ErrorText(status) +
                                     ")");

        // 设置过滤器(逐 ID 白名单)
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
        TosunDispatch::SetCanHandler(channel, [this, channel](const TLibCan& canData) {
            OnCanEvent(canData);
        });
        status = api.RegisterEventCan(tempHandle, &TosunDispatch::CanThunk);
        if (status != 0) {
            TosunDispatch::ClearCanHandler(channel);
            throw std::runtime_error("Register can event failed(" +
                                     api.ErrorText(status) + ")");
        }

        // 所有操作成功后,才提交状态
        handle_ = tempHandle;
        isOpen_ = true;
        return true;
    } catch (const std::exception& ex) {
        // 清理已打开的资源
        TosunDispatch::ClearCanHandler(config_.channelIndex);
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

bool ToSunCanWrapper::Close() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_) return true;
    isOpen_ = false;

    auto& api = TosunApi::Instance();
    // 先注销事件回调
    if (handle_ != nullptr) {
        try {
            api.UnregisterEventCan(handle_, &TosunDispatch::CanThunk);
        } catch (...) {
        }
        TosunDispatch::ClearCanHandler(config_.channelIndex);
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

void ToSunCanWrapper::FlushReceiveBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (handle_ != nullptr)
        TosunApi::Instance().ClearCanReceiveBuffers(handle_, config_.channelIndex);
}

void ToSunCanWrapper::FlushTransmitBuffer() {
    // libTSCAN 没有清除发送缓冲区的 API,空操作(与 .NET 一致)
}

void ToSunCanWrapper::OnCanEvent(const TLibCan& canData) {
    // 回调运行在驱动线程,异常不能向外抛
    try {
        if (!isOpen_) return;
        // 事件回调是设备级的,仅处理本通道的报文
        if (canData.idxChn != config_.channelIndex) return;
        // 丢弃 TX 帧(发送回显由 Send 成功后自行触发)
        if ((canData.properties & PROP_TX) != 0) return;

        auto frame = std::make_shared<CANFrame>();
        // 将 EFF/RTR/ERR 标志合成到 ID 高位(SocketCAN 风格)
        int64_t id = static_cast<uint32_t>(canData.identifier) & CanFrameDef::CAN_ID_FLAG;
        if ((canData.properties & PROP_EXT) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
        if ((canData.properties & PROP_REMOTE) != 0) id |= CanFrameDef::CAN_RTR_FLAG;
        if ((canData.properties & PROP_ERROR) != 0) id |= CanFrameDef::CAN_ERR_FLAG;
        frame->id = id;
        // 钳制 dlc 到经典 CAN 有效范围
        frame->dlc = std::min<int>(canData.dlc, DataLenConverter::MaxCanDataLength);
        frame->flag = canData.properties;
        frame->flags = ToSunCanFlagParser::Parse(canData.properties);
        frame->timestamp = canData.timeUs;
        frame->data.assign(canData.data, canData.data + frame->dlc);
        frame->dir = 1;

        if (eventHandler) eventHandler(*frame);
    } catch (...) {
    }
}

bool ToSunCanWrapper::Send(FrameBase& frameBase) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || handle_ == nullptr) return false;

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
        auto& api = TosunApi::Instance();

        canFrame->dir = 0;
        const uint32_t validId = CanFrameHelper::GetId(canFrame->id);
        const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);
        const bool isRTR = CanFrameHelper::IsRTR(canFrame->id);

        TLibCan canMsg{};
        canMsg.idxChn = config_.channelIndex;
        canMsg.properties = PROP_TX;
        if (isExtended) canMsg.properties |= PROP_EXT;
        if (isRTR) canMsg.properties |= PROP_REMOTE;
        canMsg.dlc = static_cast<uint8_t>(canFrame->dlc);
        canMsg.reserved = 0;
        canMsg.identifier = static_cast<int32_t>(validId);
        canMsg.timeUs = 0;
        std::memset(canMsg.data, 0, sizeof(canMsg.data));
        std::memcpy(canMsg.data, canFrame->data.data(), canFrame->dlc);

        const uint32_t status = api.TransmitCanAsync(handle_, &canMsg);
        if (status != 0) return false;

        canFrame->flags = ToSunCanFlagParser::Parse(canMsg.properties);
        if (eventHandler) eventHandler(*canFrame);
        return true;
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

} // namespace tosun
} // namespace canhw
