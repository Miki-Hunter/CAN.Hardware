#include "ChuangXinCanWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "ChuangXinFlagParser.h"

namespace canhw {
namespace chuangxin {

using namespace std::chrono_literals;

bool ChuangXinCanWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    const uint32_t deviceType = static_cast<uint32_t>(config_.deviceModel);
    const uint32_t deviceIndex = config_.deviceIndex;
    const uint32_t channel = config_.deviceChannel;
    std::vector<VciCanObj> tempRxBuf;
    auto& api = ChuangXinCanApi::Instance();

    try {
        // 打开设备(同一物理设备的多个通道共享一次 Open)
        if (!api.OpenDevice(deviceType, deviceIndex))
            throw std::runtime_error("Open device failed");

        // 波特率:优先使用自定义 Timing0/Timing1
        uint8_t timing0, timing1;
        if (config_.enableCustomBitrate) {
            timing0 = config_.timing0;
            timing1 = config_.timing1;
        } else {
            ChuangXinCanApi::BuildTiming(config_.canBitrate, timing0, timing1);
        }

        // Filter:1=接收所有帧,2=标准帧滤波,3=扩展帧滤波
        VciInitConfig initConfig{};
        initConfig.accCode = config_.enableFilter ? config_.accCode : 0;
        initConfig.accMask = config_.enableFilter ? config_.accMask : 0xFFFFFFFF;
        initConfig.reserved = 0;
        initConfig.filter =
            config_.enableFilter ? static_cast<uint8_t>(config_.extendedFilter ? 3 : 2)
                                 : static_cast<uint8_t>(1);
        initConfig.timing0 = timing0;
        initConfig.timing1 = timing1;
        initConfig.mode = static_cast<uint8_t>(config_.mode);

        if (api.InitCAN(deviceType, deviceIndex, channel, &initConfig) != 1)
            throw std::runtime_error("Initialize can channel failed");

        // 分配持久化接收缓冲区
        tempRxBuf.resize(RxBatchSize);

        if (api.StartCAN(deviceType, deviceIndex, channel) != 1)
            throw std::runtime_error("Start can channel failed");

        // 所有操作成功后,才提交状态
        openedDeviceType_ = deviceType;
        openedDeviceIndex_ = deviceIndex;
        openedChannel_ = channel;
        rxBuf_ = std::move(tempRxBuf);
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&ChuangXinCanWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 回滚已打开的资源
        tempRxBuf.clear();
        rxBuf_.clear();
        try {
            api.ResetCAN(deviceType, deviceIndex, channel);
        } catch (...) {
        }
        try {
            api.CloseDevice(deviceType, deviceIndex);
        } catch (...) {
        }
        isOpen_ = false;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool ChuangXinCanWrapper::Close() {
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    // 在锁外部等待读线程退出,避免死锁
    readThread_.Join();

    canhw::LockGuard lock(lock_);
    auto& api = ChuangXinCanApi::Instance();
    if (isOpen_) {
        try {
            api.ResetCAN(openedDeviceType_, openedDeviceIndex_, openedChannel_);
        } catch (...) {
        }
        try {
            api.CloseDevice(openedDeviceType_, openedDeviceIndex_);
        } catch (...) {
        }
        rxBuf_.clear();
        isOpen_ = false;
    }
    return true;
}

void ChuangXinCanWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (isOpen_)
        ChuangXinCanApi::Instance().ClearBuffer(openedDeviceType_, openedDeviceIndex_,
                                                openedChannel_);
}

void ChuangXinCanWrapper::FlushTransmitBuffer() {
    canhw::LockGuard lock(lock_);
    if (isOpen_)
        ChuangXinCanApi::Instance().ClearBuffer(openedDeviceType_, openedDeviceIndex_,
                                                openedChannel_);
}

void ChuangXinCanWrapper::ReadTaskProc() {
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

bool ChuangXinCanWrapper::Read() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || rxBuf_.empty()) return false;

    auto& api = ChuangXinCanApi::Instance();
    // 非阻塞批量读取(wait_time=0),单次最多 RxBatchSize 帧
    const uint32_t receiveNum =
        api.Receive(openedDeviceType_, openedDeviceIndex_, openedChannel_,
                    rxBuf_.data(), static_cast<uint32_t>(rxBuf_.size()), 0);

    // 0xFFFFFFFF(-1) 表示设备不存在/USB 掉线,按无数据处理
    if (receiveNum == 0 || receiveNum == 0xFFFFFFFF) return false;

    const size_t count =
        std::min<size_t>(receiveNum, RxBatchSize);
    for (size_t i = 0; i < count; ++i) {
        const VciCanObj& rec = rxBuf_[i];

        auto frame = std::make_shared<CANFrame>();
        // 将 EFF/RTR 标志合成到 ID 高位(SocketCAN 风格)
        int64_t id = rec.id & CanFrameDef::CAN_ID_FLAG;
        if (rec.externFlag != 0) id |= CanFrameDef::CAN_EFF_FLAG;
        if (rec.remoteFlag != 0) id |= CanFrameDef::CAN_RTR_FLAG;
        frame->id = id;

        // 钳制 dlc 到经典 CAN 有效范围
        frame->dlc = std::min<int>(rec.dataLen, DataLenConverter::MaxCanDataLength);
        frame->flag = static_cast<int>(id);
        frame->flags = ChuangXinCanFlagParser::Parse(rec.id, rec.remoteFlag,
                                                     rec.externFlag);
        // ControlCAN 时间戳单位 0.1ms → µs;TimeFlag=0 表示时间戳无效
        frame->timestamp = rec.timeFlag != 0 ? static_cast<uint64_t>(rec.timeStamp) * 100ULL
                                             : 0ULL;
        frame->data.assign(rec.data, rec.data + frame->dlc);
        frame->dir = 1;

        if (eventHandler) eventHandler(*frame);
    }
    return true;
}

bool ChuangXinCanWrapper::Send(FrameBase& frameBase) {
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

    auto& api = ChuangXinCanApi::Instance();

    canFrame->dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame->id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);

    VciCanObj sendObj{};
    sendObj.id = validId;
    sendObj.timeStamp = 0;
    sendObj.timeFlag = 0;
    sendObj.sendType = 0;
    sendObj.remoteFlag = isRTR ? 1 : 0;
    sendObj.externFlag = isExtended ? 1 : 0;
    sendObj.dataLen = static_cast<uint8_t>(canFrame->dlc);
    std::memset(sendObj.data, 0, sizeof(sendObj.data));
    if (!isRTR && canFrame->dlc > 0)
        std::memcpy(sendObj.data, canFrame->data.data(), canFrame->dlc);

    // 返回实际发送帧数,!=1 视为失败
    const uint32_t result =
        api.Transmit(openedDeviceType_, openedDeviceIndex_, openedChannel_, &sendObj, 1);
    if (result != 1) return false;

    canFrame->flags =
        ChuangXinCanFlagParser::Parse(sendObj.id, sendObj.remoteFlag, sendObj.externFlag);
    if (eventHandler) eventHandler(*canFrame);
    return true;
}

} // namespace chuangxin
} // namespace canhw
