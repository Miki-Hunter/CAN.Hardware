#include "ToomossCanWrapper.h"

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

CanFilterConfig ToomossCanWrapper::BuildFilter() const {
    CanFilterConfig filter{};
    filter.enable = 1;
    filter.filterIndex = config_.filterIndex;
    filter.filterMode = static_cast<uint8_t>(config_.filterMode);
    filter.extFrame = config_.extendedFilter ? 1 : 0;
    filter.idStdExt = config_.filterId & CanFrameDef::CAN_ID_FLAG;
    // IDE 位必须与 ExtFrame 保持一致
    filter.idIde = config_.extendedFilter ? 1u : 0u;
    filter.idRtr = config_.filterRtr;
    filter.maskStdExt = config_.filterMask;
    filter.maskIde = config_.filterMaskIde;
    filter.maskRtr = config_.filterMaskRtr;

    if (!config_.enableFilter) {
        // 接收所有帧:过滤器索引/模式/帧类型归零,验收码/屏蔽码全 0
        filter.filterIndex = 0;
        filter.filterMode = 0;
        filter.extFrame = 0;
        filter.idStdExt = 0;
        filter.idIde = 0;
        filter.idRtr = 0;
        filter.maskStdExt = 0;
        filter.maskIde = 0;
        filter.maskRtr = 0;
    }
    return filter;
}

bool ToomossCanWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    int32_t devHandle = 0;
    bool deviceOpened = false;
    std::vector<CanMsg> tempRxBuf;
    auto& api = ToomossApi::Instance();

    try {
        if (!api.OpenDevice(config_.deviceIndex, devHandle))
            throw std::runtime_error("Open device failed");
        deviceOpened = true;

        CanInitConfig initConfig{};
        int32_t ret =
            api.CanGetSpeedArg(devHandle, &initConfig, config_.canBitrate);
        if (ret != CAN_SUCCESS)
            throw std::runtime_error("Get CAN speed arg failed(" +
                                     std::to_string(ret) + ")");

        initConfig.canMode = static_cast<uint8_t>(
            static_cast<int>(config_.mode) |
            (config_.enableA120Ohm ? CAN_MODE_RESISTOR : 0));
        initConfig.canAbom = config_.enableAutoOffline ? 1 : 0;
        initConfig.canNart = config_.disableRetransmission ? 1 : 0;
        initConfig.canRflm = config_.fifoLock ? 1 : 0;
        initConfig.canTxfp = config_.txPriorityByRequest ? 1 : 0;

        ret = api.CanInit(devHandle, config_.channelIndex, &initConfig);
        if (ret != CAN_SUCCESS)
            throw std::runtime_error("Initialize CAN failed(" + std::to_string(ret) +
                                     ")");

        // 经典 CAN 必须配置过滤器,不启用过滤时配置为接收所有帧
        if (config_.enableFilter && config_.filterIndex > 13)
            throw std::invalid_argument("FilterIndex must be 0-13, got " +
                                        std::to_string(config_.filterIndex) + ".");
        const CanFilterConfig filter = BuildFilter();
        ret = api.CanFilterInit(devHandle, config_.channelIndex, &filter);
        if (ret != CAN_SUCCESS)
            throw std::runtime_error("Set CAN filter failed(" + std::to_string(ret) +
                                     ")");

        tempRxBuf.resize(RxBatchSize);

        ret = api.CanStartGetMsg(devHandle, config_.channelIndex);
        if (ret != CAN_SUCCESS)
            throw std::runtime_error("Start CAN receive failed(" + std::to_string(ret) +
                                     ")");

        // 所有操作成功后,才提交状态
        openedDevHandle_ = devHandle;
        openedChannel_ = config_.channelIndex;
        rxBuf_ = std::move(tempRxBuf);
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&ToomossCanWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 回滚已打开的资源
        tempRxBuf.clear();
        rxBuf_.clear();
        if (deviceOpened) {
            try {
                api.CanStopGetMsg(devHandle, config_.channelIndex);
            } catch (...) {
            }
            try {
                api.CanStop(devHandle, config_.channelIndex);
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

bool ToomossCanWrapper::Close() {
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    readThread_.Join();

    canhw::LockGuard lock(lock_);
    if (isOpen_) {
        auto& api = ToomossApi::Instance();
        try {
            api.CanStopGetMsg(openedDevHandle_, openedChannel_);
        } catch (...) {
        }
        try {
            api.CanStop(openedDevHandle_, openedChannel_);
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

void ToomossCanWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) ToomossApi::Instance().CanClearMsg(openedDevHandle_, openedChannel_);
}

void ToomossCanWrapper::FlushTransmitBuffer() {
    // USB2XXX 经典 CAN 没有独立的发送队列清空接口(与 .NET 一致)
}

void ToomossCanWrapper::ReadTaskProc() {
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

bool ToomossCanWrapper::Read() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || rxBuf_.empty()) return false;

    auto& api = ToomossApi::Instance();
    const int32_t receiveNum = api.CanGetMsgWithSize(
        openedDevHandle_, openedChannel_, rxBuf_.data(), RxBatchSize);
    if (receiveNum < 0)
        throw std::runtime_error("Get CAN message failed(" +
                                 std::to_string(receiveNum) + ")");
    if (receiveNum == 0) return false;

    const size_t count = std::min<size_t>(static_cast<size_t>(receiveNum), RxBatchSize);
    for (size_t i = 0; i < count; ++i) {
        const CanMsg& rec = rxBuf_[i];

        // bit7=发送帧标志:发送回显由 Send 成功后自行触发,这里丢弃
        if ((rec.remoteFlag & CAN_MSG_REMOTE_TX) != 0) continue;

        auto frame = std::make_shared<CANFrame>();
        int64_t id = rec.id & CanFrameDef::CAN_ID_FLAG;
        if ((rec.externFlag & ToomossCanFlagParser::ExternFlagExt) != 0)
            id |= CanFrameDef::CAN_EFF_FLAG;
        if ((rec.remoteFlag & CAN_MSG_REMOTE_RTR) != 0)
            id |= CanFrameDef::CAN_RTR_FLAG;
        if ((rec.externFlag & ToomossCanFlagParser::ExternFlagErr) != 0)
            id |= CanFrameDef::CAN_ERR_FLAG;
        frame->id = id;

        frame->dlc = std::min<int>(rec.dataLen, DataLenConverter::MaxCanDataLength);
        frame->flag = rec.remoteFlag | (rec.externFlag << 8);
        frame->flags = ToomossCanFlagParser::Parse(rec.id, rec.remoteFlag,
                                                   rec.externFlag, false);

        // 时间戳单位 100µs,换算为微秒
        const uint64_t timestamp =
            (static_cast<uint64_t>(rec.timeStampHigh) << 32) | rec.timeStamp;
        frame->timestamp = timestamp * 100ULL;

        frame->data.assign(rec.data, rec.data + frame->dlc);
        frame->dir = 1;

        if (eventHandler) eventHandler(*frame);
    }
    return true;
}

bool ToomossCanWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_) return false;

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

    const bool isRTR = CanFrameHelper::IsRTR(canFrame->id);
    if (!isRTR && static_cast<int>(canFrame->data.size()) < canFrame->dlc)
        throw std::invalid_argument("Data length(" +
                                    std::to_string(canFrame->data.size()) +
                                    ") is less than DLC(" +
                                    std::to_string(canFrame->dlc) + ").");

    try {
        auto& api = ToomossApi::Instance();

        canFrame->dir = 0;
        const uint32_t validId = CanFrameHelper::GetId(canFrame->id);
        const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);

        CanMsg sendMsg{};
        sendMsg.id = validId;
        sendMsg.timeStamp = 0;
        sendMsg.remoteFlag = isRTR ? CAN_MSG_REMOTE_RTR : 0;
        sendMsg.externFlag = isExtended ? 1 : 0;
        sendMsg.dataLen = static_cast<uint8_t>(canFrame->dlc);
        std::memset(sendMsg.data, 0, sizeof(sendMsg.data));
        if (!isRTR && canFrame->dlc > 0)
            std::memcpy(sendMsg.data, canFrame->data.data(), canFrame->dlc);
        sendMsg.timeStampHigh = 0;

        const int32_t sentNum =
            api.CanSendMsg(openedDevHandle_, openedChannel_, &sendMsg, 1);
        if (sentNum != 1) return false;

        canFrame->flags = ToomossCanFlagParser::Parse(sendMsg.id, sendMsg.remoteFlag,
                                                      sendMsg.externFlag, true);
        if (eventHandler) eventHandler(*canFrame);
        return true;
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

} // namespace toomoss
} // namespace canhw
