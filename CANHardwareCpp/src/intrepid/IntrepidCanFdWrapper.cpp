#include "IntrepidCanFdWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "IntrepidFlagParser.h"

namespace canhw {
namespace intrepid {

using namespace std::chrono_literals;

bool IntrepidCanFdWrapper::Open() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isOpen_) return true;

    void* tempHandle = nullptr;
    auto& api = IntrepidApi::Instance();

    try {
        // 通道号(0 基) -> 网络 ID(HSCAN1~HSCAN7)
        networkId_ = ChannelToNetworkId(config_.channel);
        if (networkId_ < 0)
            throw std::invalid_argument(
                "Invalid channel(" + std::to_string(config_.channel) +
                "), valid range: 0-6(HSCAN1~HSCAN7).");

        // 查找第一台已连接设备并打开(bConfigRead=1)
        tempHandle = api.OpenFirstDevice();

        // 设置仲裁段波特率(枚举/自定义,值均为 bps 直填)
        const int32_t arbBitrate =
            config_.enableCustomBitrate
                ? config_.customArbitrationBitrate
                : static_cast<int32_t>(config_.canFdArbitrationBitrate);
        int32_t result = api.SetBitRate(tempHandle, arbBitrate, networkId_);
        if (result != 1)
            throw std::runtime_error("Set arbitration bitrate " +
                                     std::to_string(arbBitrate) + " failed(" +
                                     std::to_string(result) + ")");

        // 设置数据段波特率
        const int32_t dataBitrate =
            config_.enableCustomBitrate
                ? config_.customDataBitrate
                : static_cast<int32_t>(config_.canFdDataBitrate);
        result = api.SetFDBitRate(tempHandle, dataBitrate, networkId_);
        if (result != 1)
            throw std::runtime_error("Set FD data bitrate " +
                                     std::to_string(dataBitrate) + " failed(" +
                                     std::to_string(result) + ")");

        // 所有操作成功后,才提交状态
        rxMessages_.assign(RxBufferSize, SpyMessage{});
        handle_ = tempHandle;
        isOpen_ = true;
        stop_ = false;
        readThread_ = std::thread(&IntrepidCanFdWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        if (tempHandle != nullptr) {
            try {
                int32_t errors = 0;
                api.ClosePort(tempHandle, &errors);
            } catch (...) {
            }
        }
        rxMessages_.clear();
        isOpen_ = false;
        handle_ = nullptr;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool IntrepidCanFdWrapper::Close() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    if (readThread_.joinable()) readThread_.join();

    std::lock_guard<std::mutex> lock(lock_);
    if (handle_ != nullptr) {
        try {
            int32_t errors = 0;
            IntrepidApi::Instance().ClosePort(handle_, &errors);
        } catch (...) {
        }
        handle_ = nullptr;
    }
    rxMessages_.clear();
    isOpen_ = false;
    return true;
}

void IntrepidCanFdWrapper::FlushReceiveBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    // neoVI 无专用清空函数,循环取空驱动接收队列即可
    if (handle_ == nullptr || rxMessages_.empty()) return;
    auto& api = IntrepidApi::Instance();
    int32_t numMsgs = 0, numErrors = 0;
    do {
        numMsgs = 0;
        if (api.GetMessages(handle_, rxMessages_.data(), &numMsgs, &numErrors) != 1)
            break;
    } while (numMsgs > 0);
}

void IntrepidCanFdWrapper::FlushTransmitBuffer() {
    // neoVI 发送直接交付设备,无用户态发送队列可清空(与 .NET 一致)
}

void IntrepidCanFdWrapper::ReadTaskProc() {
    auto& api = IntrepidApi::Instance();
    while (!stop_) {
        try {
            if (rxMessages_.empty() || handle_ == nullptr) break;

            // 驱动级等待(最长 200ms),超时无消息则进入下一轮
            if (api.WaitForRxMessagesWithTimeOut(handle_, RxWaitTimeoutMs) == 0)
                continue;

            int32_t numMsgs = 0, numErrors = 0;
            if (api.GetMessages(handle_, rxMessages_.data(), &numMsgs, &numErrors) != 1)
                continue;

            for (int32_t i = 0; i < numMsgs; ++i) {
                const SpyMessage& msg = rxMessages_[static_cast<size_t>(i)];
                // 过滤:非本网络 / 发送回显 / 非 CAN 协议消息
                if (msg.networkID != static_cast<uint8_t>(networkId_) ||
                    (msg.statusBitField & StatusBits::TX_MSG) != 0 ||
                    (msg.protocol != Protocol::CAN && msg.protocol != Protocol::CANFD))
                    continue;

                int64_t id = msg.arbIDOrHeader & CanFrameDef::CAN_ID_FLAG;
                if ((msg.statusBitField & StatusBits::XTD_FRAME) != 0)
                    id |= CanFrameDef::CAN_EFF_FLAG;
                if ((msg.statusBitField & StatusBits::REMOTE_FRAME) != 0)
                    id |= CanFrameDef::CAN_RTR_FLAG;

                // 数据场:>8 字节走外挂指针,<=8 字节取内联 uint64(小端)
                // 钳制到 CANFD 有效范围,防止驱动返回异常值导致越界
                const int length = std::min<int>(msg.numberBytesData,
                                                 DataLenConverter::MaxCanFdDataLength);
                auto frame = std::make_shared<CANFrame>();
                frame->id = id;
                frame->dlc = length;
                frame->fdf = (msg.statusBitField3 & StatusBits3::CANFD_FDF) != 0;
                frame->brs = (msg.statusBitField3 & StatusBits3::CANFD_BRS) != 0;
                frame->flag = static_cast<int>(msg.statusBitField);
                frame->flags = IntrepidCanFlagParser::Parse(
                    msg.statusBitField, msg.statusBitField2, msg.statusBitField3);
                frame->timestamp = IntrepidApi::GetTimestampUs(
                    msg.timeHardware, msg.timeHardware2);
                frame->dir = 1;
                if (msg.extraDataPtrEnabled != 0 && msg.iExtraDataPtr != nullptr) {
                    const auto* src = static_cast<const uint8_t*>(msg.iExtraDataPtr);
                    frame->data.assign(src, src + length);
                } else {
                    const auto* raw = reinterpret_cast<const uint8_t*>(&msg.data);
                    frame->data.assign(raw, raw + length);
                }

                if (eventHandler) eventHandler(*frame);
            }
        } catch (...) {
            if (!stop_) std::this_thread::sleep_for(10ms);
        }
    }
}

bool IntrepidCanFdWrapper::Send(FrameBase& frameBase) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || handle_ == nullptr) return false;

    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    // 发送前校验数据长度合法性(非法参数直接抛异常,不做静默失败)
    if (canFrame->brs && !canFrame->fdf)
        throw std::invalid_argument(
            "BRS is only valid for CANFD frame(FDF must be set).");
    if (canFrame->fdf && CanFrameHelper::IsRTR(canFrame->id))
        throw std::invalid_argument("CANFD frame does not support RTR.");
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
        auto& api = IntrepidApi::Instance();

        canFrame->dir = 0;
        const uint32_t validId = CanFrameHelper::GetId(canFrame->id);
        const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);
        const bool isRtr = CanFrameHelper::IsRTR(canFrame->id);

        // SocketCAN 高位标志 -> icsSpyMessage 状态位;CAN FD 属性在 StatusBitField3
        uint32_t statusBitField =
            isExtended ? StatusBits::XTD_FRAME : StatusBits::NONE;
        if (isRtr) statusBitField |= StatusBits::REMOTE_FRAME;
        uint32_t statusBitField3 = 0;
        if (canFrame->fdf) statusBitField3 |= StatusBits3::CANFD_FDF;
        if (canFrame->brs) statusBitField3 |= StatusBits3::CANFD_BRS;

        SpyMessage msg{};
        msg.statusBitField = statusBitField;
        msg.arbIDOrHeader = validId;
        msg.numberBytesData = static_cast<uint8_t>(canFrame->dlc);

        int32_t result;
        if (!canFrame->fdf) {
            // 经典帧:内联 8 字节,常规发送
            uint64_t inlineData = 0;
            std::memcpy(&inlineData, canFrame->data.data(), canFrame->dlc);
            msg.data = inlineData;
            result = api.TxMessages(handle_, &msg, networkId_, 1);
        } else {
            // FD 帧:Protocol 置 CANFD;<=8 字节内联发送,>8 字节走 ExtraDataPtr 外挂指针
            msg.protocol = Protocol::CANFD;
            msg.statusBitField3 = statusBitField3;
            if (canFrame->dlc <= DataLenConverter::MaxCanDataLength) {
                uint64_t inlineData = 0;
                std::memcpy(&inlineData, canFrame->data.data(), canFrame->dlc);
                msg.data = inlineData;
                result = api.TxMessages(handle_, &msg, networkId_, 1);
            } else {
                std::vector<uint8_t> dataPtr(
                    canFrame->data.begin(), canFrame->data.begin() + canFrame->dlc);
                msg.extraDataPtrEnabled = 1;
                msg.iExtraDataPtr = dataPtr.data();
                uint32_t numberTxed = 0;
                result = api.TxMessagesEx(handle_, &msg, networkId_, 1, &numberTxed, 0);
                msg.iExtraDataPtr = nullptr;  // 出作用域前摘除指针
            }
        }
        if (result != 1) return false;

        canFrame->flag = static_cast<int>(statusBitField);
        canFrame->flags = IntrepidCanFlagParser::Parse(
            statusBitField | StatusBits::TX_MSG, 0, statusBitField3);
        if (eventHandler) eventHandler(*canFrame);
        return true;
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

} // namespace intrepid
} // namespace canhw
