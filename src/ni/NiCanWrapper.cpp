#include "NiCanWrapper.h"

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

bool NiCanWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    uint32_t inSession = 0, outSession = 0;
    auto& api = NiApi::Instance();

    try {
        // 基于 :memory: 伪数据库创建帧输入/输出流会话
        int32_t status = api.CreateSession(DB_CAN, "", "", config_.interfaceName.c_str(),
                                           NX_MODE_FRAME_IN_STREAM, &inSession);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Create input session failed(" +
                                     NiApi::Instance().GetStatusString(status) + ")");

        status = api.CreateSession(DB_CAN, "", "", config_.interfaceName.c_str(),
                                   NX_MODE_FRAME_OUT_STREAM, &outSession);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Create output session failed(" +
                                     NiApi::Instance().GetStatusString(status) + ")");

        // 两个会话共享同一物理接口,波特率需分别设置;
        // 自定义模式打包高级位时序值(tqNs=Prescale*25, TSEG1=Prop+Phase1, TSEG2=Phase2)
        uint64_t baudrate;
        if (config_.enableCustomBitrate) {
            if (config_.tq !=
                1 + config_.prop + config_.phase1 + config_.phase2)
                throw std::invalid_argument(
                    "Invalid bit timing: Tq(" + std::to_string(config_.tq) +
                    ") must equal 1 + Prop + Phase1 + Phase2(" +
                    std::to_string(1 + config_.prop + config_.phase1 +
                                   config_.phase2) +
                    ").");
            baudrate = NiApi::PackCustomBaudRate(
                config_.prescale, config_.prop + config_.phase1, config_.phase2,
                config_.sjw);
        } else {
            baudrate = static_cast<uint64_t>(config_.canBitrate);
        }
        status = api.SetProperty64(inSession, NX_PROP_SESSION_INTF_BAUD_RATE64, 8,
                                   &baudrate);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Set input session baudrate failed(" +
                                     NiApi::Instance().GetStatusString(status) + ")");
        status = api.SetProperty64(outSession, NX_PROP_SESSION_INTF_BAUD_RATE64, 8,
                                   &baudrate);
        if (status != NX_SUCCESS)
            throw std::runtime_error("Set output session baudrate failed(" +
                                     NiApi::Instance().GetStatusString(status) + ")");

        // 会话默认 AutoStart,首次读写时自动启动接口,无需显式 nxStart
        inputSession_ = inSession;
        outputSession_ = outSession;
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&NiCanWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 清理已创建的会话
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

bool NiCanWrapper::Close() {
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    // 在锁外部等待读线程退出(nxReadFrame 0.1s 超时),避免死锁
    readThread_.Join();

    canhw::LockGuard lock(lock_);
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

void NiCanWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (inputSession_ != 0) NiApi::Instance().Flush(inputSession_);
}

void NiCanWrapper::FlushTransmitBuffer() {
    canhw::LockGuard lock(lock_);
    if (outputSession_ != 0) NiApi::Instance().Flush(outputSession_);
}

void NiCanWrapper::ReadTaskProc() {
    std::vector<uint8_t> buffer(NX_READ_BUFFER_SIZE);
    auto& api = NiApi::Instance();
    while (!stop_) {
        try {
            const uint32_t session = inputSession_;
            if (session == 0) break;

            // nxReadFrame 阻塞至超时(0.1s),流内可能串联多帧,需逐帧解析
            uint32_t bytesRead = 0;
            const int32_t status =
                api.ReadFrame(session, buffer.data(),
                              static_cast<uint32_t>(buffer.size()),
                              NX_READ_TIMEOUT, &bytesRead);
            if (status != NX_SUCCESS || bytesRead < NX_FRAME_HEADER_SIZE) continue;

            ParseFrames(buffer, bytesRead);
        } catch (...) {
            if (!stop_) canhw::SleepMs(10);
        }
    }
}

void NiCanWrapper::ParseFrames(const std::vector<uint8_t>& buffer,
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
        const int payloadLength = buffer[frameStart + 15];
        const int frameSize = NiApi::GetFrameSize(payloadLength);
        if (frameStart + static_cast<size_t>(frameSize) > bytesRead) break;
        offset = frameStart + static_cast<size_t>(frameSize);

        // 跳过发送回显与非 CAN 帧(延迟/触发等特殊帧 Type >= 0xE0)
        if ((frameFlags & NX_FRAME_FLAG_TRANSMIT_ECHO) != 0) continue;
        if (frameType > NX_FRAME_TYPE_CANFDBRS_DATA) continue;

        // 将 EFF/RTR/ERR 标志合成到 ID 高位(SocketCAN 风格),XNET 扩展帧标记为 bit29
        int64_t id = identifier & NX_CAN_ID_MASK;
        if ((identifier & NX_CAN_EXT_FLAG) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
        if (frameType == NX_FRAME_TYPE_CAN_REMOTE) id |= CanFrameDef::CAN_RTR_FLAG;
        if (frameType == NX_FRAME_TYPE_CAN_BUS_ERROR) id |= CanFrameDef::CAN_ERR_FLAG;

        auto frame = std::make_shared<CANFrame>();
        frame->id = id;
        // 钳制 dlc 到经典 CAN 有效范围
        frame->dlc = std::min<int>(payloadLength,
                                   DataLenConverter::MaxCanDataLength);
        frame->flag = (frameFlags << 8) | frameType;
        frame->flags = NiXnetCanFlagParser::Parse(identifier, frameType, frameFlags);
        // XNET 时间戳单位为 100ns,统一换算为 µs
        frame->timestamp = timestamp / 10;
        frame->dir = 1;
        frame->data.assign(buffer.begin() + frameStart + NX_FRAME_HEADER_SIZE,
                           buffer.begin() + frameStart + NX_FRAME_HEADER_SIZE +
                               frame->dlc);

        if (eventHandler) eventHandler(*frame);
    }
}

bool NiCanWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || outputSession_ == 0) return false;

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
        auto& api = NiApi::Instance();

        canFrame->dir = 0;
        uint32_t identifier = CanFrameHelper::GetId(canFrame->id);
        // XNET 扩展帧标记为 bit29(0x20000000)
        if (CanFrameHelper::IsEFF(canFrame->id)) identifier |= NX_CAN_EXT_FLAG;
        const uint8_t frameType =
            CanFrameHelper::IsRTR(canFrame->id)
                ? static_cast<uint8_t>(NX_FRAME_TYPE_CAN_REMOTE)
                : static_cast<uint8_t>(NX_FRAME_TYPE_CAN_DATA);

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
