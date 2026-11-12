#include "PeakCanWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "PeakFlagParser.h"

namespace canhw {
namespace peak {

using namespace std::chrono_literals;

bool PeakCanWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    auto& api = PeakApi::Instance();
    try {
        // 初始化通道(HwType 仅非即插即用设备使用,默认 0)
        const uint32_t status = api.Initialize(
            config_.canDeviceChannel, static_cast<uint16_t>(config_.canBitrate),
            0 /*HwType*/, config_.ioPort, config_.interrupt);
        if (status != PCAN_ERROR_OK)
            throw std::runtime_error("Initialize can failed(" + api.GetErrorText(status) +
                                     ")");

        // 设置过滤器(PEAK 使用 ID 区间过滤)
        if (config_.enableFilter) {
            const uint8_t mode =
                config_.filterToId > CanFrameDef::CAN_STD_MAX_ID_FLAG
                    ? static_cast<uint8_t>(PCAN_MODE_EXTENDED)
                    : static_cast<uint8_t>(PCAN_MODE_STANDARD);
            const uint32_t fstatus = api.FilterMessages(
                config_.canDeviceChannel, config_.filterFromId, config_.filterToId, mode);
            if (fstatus != PCAN_ERROR_OK)
                throw std::runtime_error("Set frame filter failed(" +
                                         api.GetErrorText(fstatus) + ")");
        }

        // 所有操作成功后,才提交状态
        handle_ = config_.canDeviceChannel;
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&PeakCanWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 回滚已打开的资源
        try {
            api.Uninitialize(config_.canDeviceChannel);
        } catch (...) {
        }
        isOpen_ = false;
        handle_ = PCAN_NONEBUS;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool PeakCanWrapper::Close() {
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    // 在锁外部等待读线程退出,避免死锁
    readThread_.Join();

    canhw::LockGuard lock(lock_);
    if (handle_ != PCAN_NONEBUS) {
        try {
            PeakApi::Instance().Uninitialize(handle_);
        } catch (...) {
        }
        handle_ = PCAN_NONEBUS;
    }
    isOpen_ = false;
    return true;
}

void PeakCanWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (handle_ != PCAN_NONEBUS) PeakApi::Instance().Reset(handle_);
}

void PeakCanWrapper::FlushTransmitBuffer() {
    canhw::LockGuard lock(lock_);
    if (handle_ != PCAN_NONEBUS) PeakApi::Instance().Reset(handle_);
}

void PeakCanWrapper::ReadTaskProc() {
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

bool PeakCanWrapper::Read() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || handle_ == PCAN_NONEBUS) return false;

    auto& api = PeakApi::Instance();

    TPCANMsg canMsg{};
    TPCANTimestamp canTimeStamp{};
    const uint32_t status = api.Read(handle_, &canMsg, &canTimeStamp);
    // 接收队列为空(PCAN_ERROR_QRCVEMPTY)或其他错误时结束本轮读取
    if (status != PCAN_ERROR_OK) return false;

    const uint32_t msgType = canMsg.msgType;
    auto frame = std::make_shared<CANFrame>();

    // 将 EFF/RTR/ERR 标志合成到 ID 高位(SocketCAN 风格)
    int64_t id = canMsg.id;
    if ((msgType & PCAN_MESSAGE_EXTENDED) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
    if ((msgType & PCAN_MESSAGE_RTR) != 0) id |= CanFrameDef::CAN_RTR_FLAG;
    if ((msgType & PCAN_MESSAGE_ERRFRAME) != 0) id |= CanFrameDef::CAN_ERR_FLAG;
    frame->id = id;

    // 钳制 dlc 到经典 CAN 有效范围,防止驱动返回异常值导致越界
    frame->dlc = std::min<int>(canMsg.len, DataLenConverter::MaxCanDataLength);
    frame->flag = static_cast<int>(msgType);
    frame->flags = PeakFlagParser::Parse(msgType);
    // 时间戳合成微秒: micros + 1000*millis + 0x100000000*1000*millis_overflow
    frame->timestamp = canTimeStamp.micros + 1000ULL * canTimeStamp.millis +
                       0x100000000ULL * 1000ULL * canTimeStamp.millisOverflow;
    frame->data.assign(canMsg.data, canMsg.data + frame->dlc);
    frame->dir = 1;

    if (eventHandler) eventHandler(*frame);
    return true;
}

bool PeakCanWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || handle_ == PCAN_NONEBUS) return false;

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

    auto& api = PeakApi::Instance();

    canFrame->dir = 0;
    TPCANMsg canMsg{};
    canMsg.id = CanFrameHelper::GetId(canFrame->id);
    canMsg.len = static_cast<uint8_t>(canFrame->dlc);
    std::memcpy(canMsg.data, canFrame->data.data(), canFrame->dlc);
    canMsg.msgType =
        CanFrameHelper::IsEFF(canFrame->id)
            ? static_cast<uint8_t>(PCAN_MESSAGE_EXTENDED)
            : static_cast<uint8_t>(PCAN_MESSAGE_STANDARD);
    if (CanFrameHelper::IsRTR(canFrame->id)) canMsg.msgType |= PCAN_MESSAGE_RTR;

    const uint32_t status = api.Write(handle_, &canMsg);
    if (status != PCAN_ERROR_OK) return false;

    canFrame->flags = PeakFlagParser::Parse(canMsg.msgType);
    if (eventHandler) eventHandler(*canFrame);
    return true;
}

} // namespace peak
} // namespace canhw
