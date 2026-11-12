#include "PeakCanFdWrapper.h"

#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "PeakFlagParser.h"

namespace canhw {
namespace peak {

using namespace std::chrono_literals;

std::string PeakCanFdWrapper::BuildCanFdBitrate() const {
    // 见 PCAN-Basic InitializeFD 文档:
    // 参数与值用 '=' 分隔,参数对之间用 ',' 分隔,
    // 必填:f_clock, nom_brp/nom_sjw/nom_tseg1/nom_tseg2,
    //       data_brp/data_sjw/data_tseg1/data_tseg2
    std::ostringstream os;
    os << "f_clock=" << config_.clock
       << ",nom_brp=" << config_.arbitrationBrp
       << ",nom_tseg1=" << config_.arbitrationTseg1
       << ",nom_tseg2=" << config_.arbitrationTseg2
       << ",nom_sjw=" << config_.arbitrationSjw
       << ",data_brp=" << config_.dataBrp
       << ",data_tseg1=" << config_.dataTseg1
       << ",data_tseg2=" << config_.dataTseg2
       << ",data_sjw=" << config_.dataSjw;
    return os.str();
}

bool PeakCanFdWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    auto& api = PeakApi::Instance();
    try {
        // 初始化 FD 通道
        const std::string bitrateFD = BuildCanFdBitrate();
        const uint32_t status = api.InitializeFD(config_.canFdDeviceChannel,
                                                 bitrateFD.c_str());
        if (status != PCAN_ERROR_OK)
            throw std::runtime_error("Initialize fd failed(" + api.GetErrorText(status) +
                                     ")");

        // 设置过滤器(PEAK 使用 ID 区间过滤)
        if (config_.enableFilter) {
            const uint8_t mode =
                config_.filterToId > CanFrameDef::CAN_STD_MAX_ID_FLAG
                    ? static_cast<uint8_t>(PCAN_MODE_EXTENDED)
                    : static_cast<uint8_t>(PCAN_MODE_STANDARD);
            const uint32_t fstatus = api.FilterMessages(
                config_.canFdDeviceChannel, config_.filterFromId, config_.filterToId,
                mode);
            if (fstatus != PCAN_ERROR_OK)
                throw std::runtime_error("Set frame filter failed(" +
                                         api.GetErrorText(fstatus) + ")");
        }

        // 所有操作成功后,才提交状态
        handle_ = config_.canFdDeviceChannel;
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&PeakCanFdWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        try {
            api.Uninitialize(config_.canFdDeviceChannel);
        } catch (...) {
        }
        isOpen_ = false;
        handle_ = PCAN_NONEBUS;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool PeakCanFdWrapper::Close() {
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

void PeakCanFdWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (handle_ != PCAN_NONEBUS) PeakApi::Instance().Reset(handle_);
}

void PeakCanFdWrapper::FlushTransmitBuffer() {
    canhw::LockGuard lock(lock_);
    if (handle_ != PCAN_NONEBUS) PeakApi::Instance().Reset(handle_);
}

void PeakCanFdWrapper::ReadTaskProc() {
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

bool PeakCanFdWrapper::Read() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || handle_ == PCAN_NONEBUS) return false;

    auto& api = PeakApi::Instance();

    TPCANMsgFD canfdMsg{};
    uint64_t timestampFd = 0;  // TPCANTimestampFD 单位为微秒
    const uint32_t status = api.ReadFD(handle_, &canfdMsg, &timestampFd);
    // 接收队列为空(PCAN_ERROR_QRCVEMPTY)或其他错误时结束本轮读取
    if (status != PCAN_ERROR_OK) return false;

    const uint32_t msgType = canfdMsg.msgType;
    auto frame = std::make_shared<CANFrame>();

    // 将 EFF/RTR/ERR 标志合成到 ID 高位(SocketCAN 风格)
    int64_t id = canfdMsg.id;
    if ((msgType & PCAN_MESSAGE_EXTENDED) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
    if ((msgType & PCAN_MESSAGE_RTR) != 0) id |= CanFrameDef::CAN_RTR_FLAG;
    if ((msgType & PCAN_MESSAGE_ERRFRAME) != 0) id |= CanFrameDef::CAN_ERR_FLAG;
    frame->id = id;

    // DLC 编码换算为实际数据长度
    frame->dlc = DataLenConverter::DlcToLength(canfdMsg.dlc);
    frame->flag = static_cast<int>(msgType);
    frame->flags = PeakFlagParser::Parse(msgType);
    frame->timestamp = timestampFd;
    frame->fdf = (msgType & PCAN_MESSAGE_FD) != 0;
    frame->brs = (msgType & PCAN_MESSAGE_BRS) != 0;
    frame->data.assign(canfdMsg.data, canfdMsg.data + frame->dlc);
    frame->dir = 1;

    if (eventHandler) eventHandler(*frame);
    return true;
}

bool PeakCanFdWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || handle_ == PCAN_NONEBUS) return false;

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

    auto& api = PeakApi::Instance();

    canFrame->dir = 0;
    TPCANMsgFD canfdMsg{};
    canfdMsg.id = CanFrameHelper::GetId(canFrame->id);
    // 实际数据长度换算为 DLC 编码(0-15)
    canfdMsg.dlc = static_cast<uint8_t>(DataLenConverter::LengthToDlc(canFrame->dlc));
    std::memcpy(canfdMsg.data, canFrame->data.data(), canFrame->dlc);
    canfdMsg.msgType =
        CanFrameHelper::IsEFF(canFrame->id)
            ? static_cast<uint8_t>(PCAN_MESSAGE_EXTENDED)
            : static_cast<uint8_t>(PCAN_MESSAGE_STANDARD);
    if (CanFrameHelper::IsRTR(canFrame->id)) canfdMsg.msgType |= PCAN_MESSAGE_RTR;
    if (canFrame->fdf) canfdMsg.msgType |= PCAN_MESSAGE_FD;
    if (canFrame->brs) canfdMsg.msgType |= PCAN_MESSAGE_BRS;

    const uint32_t status = api.WriteFD(handle_, &canfdMsg);
    if (status != PCAN_ERROR_OK) return false;

    canFrame->flags = PeakFlagParser::Parse(canfdMsg.msgType);
    if (eventHandler) eventHandler(*canFrame);
    return true;
}

} // namespace peak
} // namespace canhw
