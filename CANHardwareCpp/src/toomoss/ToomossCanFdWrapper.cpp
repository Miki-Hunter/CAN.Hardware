#include "ToomossCanFdWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "ToomossFlagParser.h"

namespace canhw {
namespace toomoss {

using namespace std::chrono_literals;

bool ToomossCanFdWrapper::Open() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isOpen_) return true;

    int32_t devHandle = 0;
    bool deviceOpened = false;
    std::vector<CanFdMsg> tempRxBuf;
    auto& api = ToomossApi::Instance();

    try {
        if (!api.OpenDevice(config_.deviceIndex, devHandle))
            throw std::runtime_error("Open device failed");
        deviceOpened = true;

        CanFdInitConfig initConfig{};
        int32_t ret = api.CanFdGetSpeedArg(devHandle, &initConfig,
                                           config_.arbitrationBitrate,
                                           config_.dataBitrate);
        if (ret != CANFD_SUCCESS)
            throw std::runtime_error("Get CANFD speed arg failed(" +
                                     std::to_string(ret) + ")");

        initConfig.mode = static_cast<uint8_t>(config_.mode);
        initConfig.isoCrcEnable = config_.enableISOCRC ? 1 : 0;
        initConfig.retrySend = config_.enableRetrySend ? 1 : 0;
        initConfig.resEnable = config_.enableA120Ohm ? 1 : 0;

        ret = api.CanFdInit(devHandle, config_.channelIndex, &initConfig);
        if (ret != CANFD_SUCCESS)
            throw std::runtime_error("Initialize CANFD failed(" + std::to_string(ret) +
                                     ")");

        // CANFD 不配置过滤器时默认接收所有帧
        if (config_.enableFilter) {
            if (config_.filterIndex > 31)
                throw std::invalid_argument("FilterIndex must be 0-31, got " +
                                            std::to_string(config_.filterIndex) + ".");
            // ID_Mask 始终比较 bit31(IDE),从而只接收标准帧或扩展帧
            const uint32_t ideAccept =
                config_.extendedFilter ? CANFD_IDE_FLAG : 0u;
            const uint32_t ideMask = CANFD_IDE_FLAG;
            CanFdFilterConfig filter{};
            filter.enable = 1;
            filter.index = config_.filterIndex;
            filter.res0 = 0;
            filter.res1 = 0;
            filter.idAccept =
                ideAccept | (config_.filterId & CANFD_ID_MASK);
            filter.idMask = ideMask | (config_.filterMask & CANFD_ID_MASK);
            ret = api.CanFdSetFilter(devHandle, config_.channelIndex, &filter, 1);
            if (ret != CANFD_SUCCESS)
                throw std::runtime_error("Set CANFD filter failed(" +
                                         std::to_string(ret) + ")");
        }

        tempRxBuf.resize(RxBatchSize);

        ret = api.CanFdStartGetMsg(devHandle, config_.channelIndex);
        if (ret != CANFD_SUCCESS)
            throw std::runtime_error("Start CANFD receive failed(" +
                                     std::to_string(ret) + ")");

        // 所有操作成功后,才提交状态
        openedDevHandle_ = devHandle;
        openedChannel_ = config_.channelIndex;
        rxBuf_ = std::move(tempRxBuf);
        isOpen_ = true;
        stop_ = false;
        readThread_ = std::thread(&ToomossCanFdWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 回滚已打开的资源
        tempRxBuf.clear();
        rxBuf_.clear();
        if (deviceOpened) {
            try {
                api.CanFdStopGetMsg(devHandle, config_.channelIndex);
            } catch (...) {
            }
            try {
                api.CanFdStop(devHandle, config_.channelIndex);
            } catch (...) {
            }
            try {
                api.CloseDevice(devHandle);
            } catch (...) {
            }
        }
        isOpen_ = false;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool ToomossCanFdWrapper::Close() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    if (readThread_.joinable()) readThread_.join();

    std::lock_guard<std::mutex> lock(lock_);
    if (isOpen_) {
        auto& api = ToomossApi::Instance();
        try {
            api.CanFdStopGetMsg(openedDevHandle_, openedChannel_);
        } catch (...) {
        }
        try {
            api.CanFdStop(openedDevHandle_, openedChannel_);
        } catch (...) {
        }
        try {
            api.CloseDevice(openedDevHandle_);
        } catch (...) {
        }
        rxBuf_.clear();
        isOpen_ = false;
    }
    return true;
}

void ToomossCanFdWrapper::FlushReceiveBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isOpen_) ToomossApi::Instance().CanFdClearMsg(openedDevHandle_, openedChannel_);
}

void ToomossCanFdWrapper::FlushTransmitBuffer() {
    // USB2XXX CANFD 没有独立的发送队列清空接口(与 .NET 一致)
}

void ToomossCanFdWrapper::ReadTaskProc() {
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

bool ToomossCanFdWrapper::Read() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || rxBuf_.empty()) return false;

    auto& api = ToomossApi::Instance();
    const int32_t receiveNum = api.CanFdGetMsg(
        openedDevHandle_, openedChannel_, rxBuf_.data(), RxBatchSize);
    if (receiveNum < 0)
        throw std::runtime_error("Get CANFD message failed(" +
                                 std::to_string(receiveNum) + ")");
    if (receiveNum == 0) return false;

    const size_t count = std::min<size_t>(static_cast<size_t>(receiveNum), RxBatchSize);
    for (size_t i = 0; i < count; ++i) {
        const CanFdMsg& rec = rxBuf_[i];

        // bit7=发送帧标志:发送回显由 Send 成功后自行触发,这里丢弃
        if ((rec.flags & CANFD_FLAG_TX) != 0) continue;

        auto frame = std::make_shared<CANFrame>();
        // 保留 bit31(IDE)/bit30(RTR)/bit28..0(ID),清除 bit29
        frame->id = static_cast<int64_t>(rec.id & 0xDFFFFFFFu);
        const bool isFd = (rec.flags & CANFD_FLAG_FDF) != 0;
        frame->dlc = std::min<int>(rec.dlc, isFd ? DataLenConverter::MaxCanFdDataLength
                                                 : DataLenConverter::MaxCanDataLength);
        frame->flag = rec.flags;
        frame->flags = ToomossCanFdFlagParser::Parse(rec.id, rec.flags, false);
        frame->fdf = isFd;
        frame->brs = (rec.flags & CANFD_FLAG_BRS) != 0;

        // 时间戳单位 10µs,换算为微秒
        const uint64_t timestamp =
            (static_cast<uint64_t>(rec.timeStampHigh) << 32) | rec.timeStamp;
        frame->timestamp = timestamp * 10ULL;

        frame->data.assign(rec.data, rec.data + frame->dlc);
        frame->dir = 1;

        if (eventHandler) eventHandler(*frame);
    }
    return true;
}

bool ToomossCanFdWrapper::Send(FrameBase& frameBase) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_) return false;

    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    try {
        return canFrame->fdf ? SendFd(*canFrame) : SendCan(*canFrame);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

bool ToomossCanFdWrapper::SendCan(CANFrame& canFrame) {
    if (canFrame.brs)
        throw std::invalid_argument(
            "BRS is only valid for CANFD frame(FDF must be set).");
    if (!DataLenConverter::IsValidCanLength(canFrame.dlc))
        throw std::invalid_argument("Invalid CAN DLC(" + std::to_string(canFrame.dlc) +
                                    "), valid range: 0-" +
                                    std::to_string(DataLenConverter::MaxCanDataLength) +
                                    ".");

    const bool isRTR = CanFrameHelper::IsRTR(canFrame.id);
    if (!isRTR && static_cast<int>(canFrame.data.size()) < canFrame.dlc)
        throw std::invalid_argument("Data length(" + std::to_string(canFrame.data.size()) +
                                    ") is less than DLC(" +
                                    std::to_string(canFrame.dlc) + ").");

    canFrame.dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame.id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame.id);

    const uint32_t rawId = (isExtended ? CANFD_IDE_FLAG : 0u) |
                           (isRTR ? CANFD_RTR_FLAG : 0u) |
                           (validId & CANFD_ID_MASK);

    txBuf_ = CanFdMsg{};
    txBuf_.id = rawId;
    txBuf_.dlc = static_cast<uint8_t>(canFrame.dlc);
    txBuf_.flags = 0;
    txBuf_.timeStamp = 0;
    txBuf_.timeStampHigh = 0;
    std::memset(txBuf_.data, 0, sizeof(txBuf_.data));
    if (!isRTR && canFrame.dlc > 0)
        std::memcpy(txBuf_.data, canFrame.data.data(), canFrame.dlc);

    const int32_t sentNum = ToomossApi::Instance().CanFdSendMsg(
        openedDevHandle_, openedChannel_, &txBuf_, 1);
    if (sentNum != 1) return false;

    canFrame.flags = ToomossCanFdFlagParser::Parse(txBuf_.id, txBuf_.flags, true);
    if (eventHandler) eventHandler(canFrame);
    return true;
}

bool ToomossCanFdWrapper::SendFd(CANFrame& canFrame) {
    if (canFrame.brs && !canFrame.fdf)
        throw std::invalid_argument(
            "BRS is only valid for CANFD frame(FDF must be set).");
    if (!DataLenConverter::IsValidCanFdLength(canFrame.dlc))
        throw std::invalid_argument("Invalid CANFD DLC(" + std::to_string(canFrame.dlc) +
                                    "), valid lengths: 0-8,12,16,20,24,32,48,64.");
    if (static_cast<int>(canFrame.data.size()) < canFrame.dlc)
        throw std::invalid_argument("Data length(" + std::to_string(canFrame.data.size()) +
                                    ") is less than DLC(" +
                                    std::to_string(canFrame.dlc) + ").");

    canFrame.dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame.id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame.id);
    const bool isRTR = CanFrameHelper::IsRTR(canFrame.id);

    const uint32_t rawId = (isExtended ? CANFD_IDE_FLAG : 0u) |
                           (isRTR ? CANFD_RTR_FLAG : 0u) |
                           (validId & CANFD_ID_MASK);
    uint8_t flags = 0;
    if (canFrame.fdf) flags |= CANFD_FLAG_FDF;
    if (canFrame.brs) flags |= CANFD_FLAG_BRS;

    txBuf_ = CanFdMsg{};
    txBuf_.id = rawId;
    txBuf_.dlc = static_cast<uint8_t>(canFrame.dlc);  // 实际字节数 0~64
    txBuf_.flags = flags;
    txBuf_.timeStamp = 0;
    txBuf_.timeStampHigh = 0;
    std::memcpy(txBuf_.data, canFrame.data.data(), canFrame.dlc);

    const int32_t sentNum = ToomossApi::Instance().CanFdSendMsg(
        openedDevHandle_, openedChannel_, &txBuf_, 1);
    if (sentNum != 1) return false;

    canFrame.flags = ToomossCanFdFlagParser::Parse(txBuf_.id, txBuf_.flags, true);
    if (eventHandler) eventHandler(canFrame);
    return true;
}

} // namespace toomoss
} // namespace canhw
