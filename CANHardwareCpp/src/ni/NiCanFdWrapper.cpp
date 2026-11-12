#include "NiCanFdWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "NiFlagParser.h"

namespace canhw {
namespace ni {

using namespace std::chrono_literals;

bool NiCanFdWrapper::Open() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isOpen_) return true;

    uint32_t inSession = 0, outSession = 0;
    auto& api = NiApi::Instance();

    try {
        // 基于 :can_fd_brs: 伪数据库创建帧输入/输出流会话(IO Mode = CAN FD+BRS)
        int32_t status = api.CreateSession(
            DB_CAN_FD_BRS, "", "", config_.interfaceName.c_str(),
            NX_MODE_FRAME_IN_STREAM, &inSession);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Create input session failed(" +
                                     NiApi::GetStatusString(status) + ")");

        status = api.CreateSession(DB_CAN_FD_BRS, "", "",
                                   config_.interfaceName.c_str(),
                                   NX_MODE_FRAME_OUT_STREAM, &outSession);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Create output session failed(" +
                                     NiApi::GetStatusString(status) + ")");

        // 两个会话共享同一物理接口,仲裁段/数据段波特率均需分别设置;
        // 数据段与仲裁段字段布局不同,需分别打包
        uint64_t baudrate, fdBaudrate;
        if (config_.enableCustomBitrate) {
            if (config_.arbitrationTq !=
                1 + config_.arbitrationProp + config_.arbitrationPhase1 +
                    config_.arbitrationPhase2)
                throw std::invalid_argument(
                    "Invalid arbitration bit timing: Tq(" +
                    std::to_string(config_.arbitrationTq) +
                    ") must equal 1 + Prop + Phase1 + Phase2(" +
                    std::to_string(1 + config_.arbitrationProp +
                                   config_.arbitrationPhase1 +
                                   config_.arbitrationPhase2) +
                    ").");
            if (config_.dataTq !=
                1 + config_.dataProp + config_.dataPhase1 + config_.dataPhase2)
                throw std::invalid_argument(
                    "Invalid data bit timing: Tq(" + std::to_string(config_.dataTq) +
                    ") must equal 1 + Prop + Phase1 + Phase2(" +
                    std::to_string(1 + config_.dataProp + config_.dataPhase1 +
                                   config_.dataPhase2) +
                    ").");
            baudrate = NiApi::PackCustomBaudRate(
                config_.arbitrationPrescale,
                config_.arbitrationProp + config_.arbitrationPhase1,
                config_.arbitrationPhase2, config_.arbitrationSjw);
            fdBaudrate = NiApi::PackCustomFdDataBaudRate(
                config_.dataPrescale, config_.dataProp + config_.dataPhase1,
                config_.dataPhase2, config_.dataSjw);
        } else {
            baudrate = static_cast<uint64_t>(config_.canFdArbitrationBitrate);
            fdBaudrate = static_cast<uint64_t>(config_.canFdDataBitrate);
        }
        status = api.SetProperty64(inSession, NX_PROP_SESSION_INTF_BAUD_RATE64, 8,
                                   &baudrate);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Set input session baudrate failed(" +
                                     NiApi::GetStatusString(status) + ")");
        status = api.SetProperty64(inSession,
                                   NX_PROP_SESSION_INTF_CAN_FD_BAUD_RATE64, 8,
                                   &fdBaudrate);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Set input session FD baudrate failed(" +
                                     NiApi::GetStatusString(status) + ")");
        status = api.SetProperty64(outSession, NX_PROP_SESSION_INTF_BAUD_RATE64, 8,
                                   &baudrate);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Set output session baudrate failed(" +
                                     NiApi::GetStatusString(status) + ")");
        status = api.SetProperty64(outSession,
                                   NX_PROP_SESSION_INTF_CAN_FD_BAUD_RATE64, 8,
                                   &fdBaudrate);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Set output session FD baudrate failed(" +
                                     NiApi::GetStatusString(status) + ")");

        // 会话默认 AutoStart,首次读写时自动启动接口
        inputSession_ = inSession;
        outputSession_ = outSession;
        isOpen_ = true;
        stop_ = false;
        readThread_ = std::thread(&NiCanFdWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        if (inSession != 0) {
            try {
                api.Clear(inSession);
            } catch (...) {
            }
        }
        if (outSession != 0) {
            try {
                api.Clear(outSession);
            } catch (...) {
            }
        }
        inputSession_ = 0;
        outputSession_ = 0;
        isOpen_ = false;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool NiCanFdWrapper::Close() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    if (readThread_.joinable()) readThread_.join();

    std::lock_guard<std::mutex> lock(lock_);
    auto& api = NiApi::Instance();
    if (inputSession_ != 0) {
        try {
            api.Clear(inputSession_);
        } catch (...) {
        }
        inputSession_ = 0;
    }
    if (outputSession_ != 0) {
        try {
            api.Clear(outputSession_);
        } catch (...) {
        }
        outputSession_ = 0;
    }
    isOpen_ = false;
    return true;
}

void NiCanFdWrapper::FlushReceiveBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (inputSession_ != 0) NiApi::Instance().Flush(inputSession_);
}

void NiCanFdWrapper::FlushTransmitBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (outputSession_ != 0) NiApi::Instance().Flush(outputSession_);
}

void NiCanFdWrapper::ReadTaskProc() {
    std::vector<uint8_t> buffer(NX_READ_BUFFER_SIZE);
    auto& api = NiApi::Instance();
    while (!stop_) {
        try {
            const uint32_t session = inputSession_;
            if (session == 0) break;

            uint32_t bytesRead = 0;
            const int32_t status =
                api.ReadFrame(session, buffer.data(),
                              static_cast<uint32_t>(buffer.size()),
                              NX_READ_TIMEOUT, &bytesRead);
            if (status != NX_SUCCESS || bytesRead < NX_FRAME_HEADER_SIZE) continue;

            ParseFrames(buffer, bytesRead);
        } catch (...) {
            if (!stop_) std::this_thread::sleep_for(10ms);
        }
    }
}

void NiCanFdWrapper::ParseFrames(const std::vector<uint8_t>& buffer,
                                 uint32_t bytesRead) {
    size_t offset = 0;
    while (offset + NX_FRAME_HEADER_SIZE <= bytesRead) {
        const size_t frameStart = offset;
        uint64_t timestamp = 0;
        uint32_t identifier = 0;
        std::memcpy(&timestamp, &buffer[frameStart], 8);
        std::memcpy(&identifier, &buffer[frameStart + 8], 4);
        const uint8_t frameType = buffer[frameStart + 12];
        const uint8_t frameFlags = buffer[frameStart + 13];
        // 钳制负载长度到 CANFD 有效范围,防止驱动返回异常值导致越界
        const int payloadLength =
            std::min<int>(buffer[frameStart + 15],
                          DataLenConverter::MaxCanFdDataLength);
        const int frameSize = NiApi::GetFrameSize(payloadLength);
        if (frameStart + static_cast<size_t>(frameSize) > bytesRead) break;
        offset = frameStart + static_cast<size_t>(frameSize);

        // 跳过发送回显与非 CAN 帧
        if ((frameFlags & NX_FRAME_FLAG_TRANSMIT_ECHO) != 0) continue;
        if (frameType > NX_FRAME_TYPE_CANFDBRS_DATA) continue;

        int64_t id = identifier & NX_CAN_ID_MASK;
        if ((identifier & NX_CAN_EXT_FLAG) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
        if (frameType == NX_FRAME_TYPE_CAN_REMOTE) id |= CanFrameDef::CAN_RTR_FLAG;
        if (frameType == NX_FRAME_TYPE_CAN_BUS_ERROR) id |= CanFrameDef::CAN_ERR_FLAG;

        auto frame = std::make_shared<CANFrame>();
        frame->id = id;
        frame->dlc = payloadLength;
        frame->flag = (frameFlags << 8) | frameType;
        frame->flags = NiXnetCanFlagParser::Parse(identifier, frameType, frameFlags);
        frame->fdf = (frameType & NX_FRAME_TYPE_CANFD_DATA) != 0;
        frame->brs = (frameType & NX_FRAME_TYPE_CANFDBRS_DATA) ==
                     NX_FRAME_TYPE_CANFDBRS_DATA;
        // XNET 时间戳单位为 100ns,统一换算为 µs
        frame->timestamp = timestamp / 10;
        frame->dir = 1;
        frame->data.assign(buffer.begin() + frameStart + NX_FRAME_HEADER_SIZE,
                           buffer.begin() + frameStart + NX_FRAME_HEADER_SIZE +
                               payloadLength);

        if (eventHandler) eventHandler(*frame);
    }
}

bool NiCanFdWrapper::Send(FrameBase& frameBase) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || outputSession_ == 0) return false;

    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    // 发送前校验数据长度合法性(非法参数直接抛异常,不做静默失败)
    if (canFrame->brs && !canFrame->fdf)
        throw std::invalid_argument("BRS requires FDF(CANFD frame).");
    if (canFrame->fdf) {
        if (!DataLenConverter::IsValidCanFdLength(canFrame->dlc))
            throw std::invalid_argument(
                "Invalid CANFD data length(" + std::to_string(canFrame->dlc) +
                "), valid lengths: 0-8,12,16,20,24,32,48,64.");
    } else if (!DataLenConverter::IsValidCanLength(canFrame->dlc)) {
        throw std::invalid_argument("Invalid CAN DLC(" +
                                    std::to_string(canFrame->dlc) +
                                    "), valid range: 0-" +
                                    std::to_string(DataLenConverter::MaxCanDataLength) +
                                    ".");
    }
    if (static_cast<int>(canFrame->data.size()) < canFrame->dlc)
        throw std::invalid_argument("Data length(" +
                                    std::to_string(canFrame->data.size()) +
                                    ") is less than DLC(" +
                                    std::to_string(canFrame->dlc) + ").");

    try {
        auto& api = NiApi::Instance();

        canFrame->dir = 0;
        uint32_t identifier = CanFrameHelper::GetId(canFrame->id);
        // XNET 扩展帧标记为 bit29(0x20000000)
        if (CanFrameHelper::IsEFF(canFrame->id)) identifier |= NX_CAN_EXT_FLAG;

        // IO Mode=CAN FD+BRS 时:经典帧用 CAN20_Data,FD 帧按 BRS 选择类型
        uint8_t frameType;
        if (CanFrameHelper::IsRTR(canFrame->id))
            frameType = NX_FRAME_TYPE_CAN_REMOTE;
        else if (canFrame->fdf)
            frameType = canFrame->brs ? static_cast<uint8_t>(NX_FRAME_TYPE_CANFDBRS_DATA)
                                      : static_cast<uint8_t>(NX_FRAME_TYPE_CANFD_DATA);
        else
            frameType = NX_FRAME_TYPE_CAN20_DATA;

        const std::vector<uint8_t> buffer =
            NiApi::BuildFrameBytes(identifier, frameType, canFrame->data.data(),
                                   canFrame->dlc);
        const int32_t status = api.WriteFrame(
            outputSession_, buffer.data(), static_cast<uint32_t>(buffer.size()), 0);
        if (status != NX_SUCCESS) return false;

        canFrame->flag = frameType;
        canFrame->flags = NiXnetCanFlagParser::Parse(
            identifier, frameType, NX_FRAME_FLAG_TRANSMIT_ECHO);
        if (eventHandler) eventHandler(*canFrame);
        return true;
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

} // namespace ni
} // namespace canhw
