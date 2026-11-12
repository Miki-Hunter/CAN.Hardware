#include "ZlgUsbCanWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "ZlgFlagParser.h"

namespace canhw {
namespace zlg {

using namespace std::chrono_literals;

bool ZlgUsbCanWrapper::Open() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isOpen_) return true;

    void* tempDevHandle = nullptr;
    void* tempChannelHandle = nullptr;
    std::vector<ReceiveData> tempRxBuf;
    auto& api = ZlgApi::Instance();

    try {
        // 打开设备(同一物理设备共享句柄,多通道复用)
        // ZLG 返回值约定 1=成功(STATUS_OK),0=失败;OpenDevice/InitCAN 返回句柄,nullptr=失败
        tempDevHandle = api.OpenDevice(static_cast<uint32_t>(config_.deviceModel),
                                       config_.deviceIndex, 0);
        if (tempDevHandle == nullptr) throw std::runtime_error("Open device failed");

        // 设置波特率(InitCAN 之前)
        const std::string chn = std::to_string(config_.deviceChannel);
        uint32_t status;
        if (config_.enableCustomBitrate) {
            status = api.SetValue(tempDevHandle, (chn + "/baud_rate_custom").c_str(),
                                  config_.customBitrateContent.c_str());
            if (status != 1)
                throw std::runtime_error("Set custom baud rate failed(" +
                                         std::to_string(status) + ")");
        } else {
            const std::string val =
                std::to_string(ZlgApi::BuildCanBitrate(config_.canBitrate));
            status = api.SetValue(tempDevHandle, (chn + "/baud_rate").c_str(), val.c_str());
            if (status != 1)
                throw std::runtime_error("Set baud rate failed(" + std::to_string(status) +
                                         ")");
        }

        // 初始化通道(经典 CAN)
        ChannelInitConfig initConfig{};
        initConfig.canType = 0;
        initConfig.config.can.accCode = 0;
        initConfig.config.can.accMask = 0xFFFFFFFF;
        initConfig.config.can.mode = 0;
        tempChannelHandle =
            api.InitCAN(tempDevHandle, config_.deviceChannel, &initConfig);
        if (tempChannelHandle == nullptr)
            throw std::runtime_error("Initialize can channel failed");

        // 终端电阻(InitCAN 之后设置)
        status = api.SetValue(tempDevHandle, (chn + "/initenal_resistance").c_str(),
                              config_.enableA120Ohm ? "1" : "0");
        if (status != 1)
            throw std::runtime_error("Set internal resistance failed(" +
                                     std::to_string(status) + ")");

        // 设置过滤器(ZLG 使用 ID 区间过滤)
        if (config_.enableFilter) {
            status = api.SetValue(tempDevHandle, (chn + "/filter_mode").c_str(),
                                  config_.filterToId > CanFrameDef::CAN_STD_MAX_ID_FLAG
                                      ? "1"
                                      : "0");
            if (status != 1)
                throw std::runtime_error("Set filter mode failed(" +
                                         std::to_string(status) + ")");
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%02X", config_.filterFromId);
            status = api.SetValue(tempDevHandle, (chn + "/filter_start").c_str(), buf);
            if (status != 1)
                throw std::runtime_error("Set filter start failed(" +
                                         std::to_string(status) + ")");
            std::snprintf(buf, sizeof(buf), "0x%02X", config_.filterToId);
            status = api.SetValue(tempDevHandle, (chn + "/filter_end").c_str(), buf);
            if (status != 1)
                throw std::runtime_error(
                    "Set filter end failed(" + std::to_string(status) + ")");
            status = api.SetValue(tempDevHandle, (chn + "/filter_ack").c_str(), "0");
            if (status != 1)
                throw std::runtime_error("Set filter ack failed(" +
                                         std::to_string(status) + ")");
        } else {
            status = api.SetValue(tempDevHandle, (chn + "/filter_clear").c_str(), "0");
            if (status != 1)
                throw std::runtime_error("Clear filter failed(" +
                                         std::to_string(status) + ")");
        }

        // 分配持久化接收缓冲区
        tempRxBuf.resize(RxBatchSize);

        // 启动通道
        status = api.StartCAN(tempChannelHandle);
        if (status != 1)
            throw std::runtime_error("Start can failed(" + std::to_string(status) + ")");

        // 所有操作成功后,才提交状态
        devHandle_ = tempDevHandle;
        channelHandle_ = tempChannelHandle;
        rxBuf_ = std::move(tempRxBuf);
        isOpen_ = true;
        stop_ = false;
        readThread_ = std::thread(&ZlgUsbCanWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 回滚已打开的资源
        tempRxBuf.clear();
        if (tempChannelHandle != nullptr) {
            try {
                api.ResetCAN(tempChannelHandle);
            } catch (...) {
            }
        }
        if (tempDevHandle != nullptr) {
            try {
                api.CloseDevice();
            } catch (...) {
            }
        }
        isOpen_ = false;
        devHandle_ = nullptr;
        channelHandle_ = nullptr;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool ZlgUsbCanWrapper::Close() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!isOpen_) return true;
        // 先置停止标志
        stop_ = true;
    }

    // 在锁外部等待读线程退出,避免死锁
    if (readThread_.joinable()) readThread_.join();

    std::lock_guard<std::mutex> lock(lock_);
    auto& api = ZlgApi::Instance();

    // 关闭硬件
    if (channelHandle_ != nullptr) {
        try {
            // 清除设备上可能残留的定时发送配置(best-effort,不阻塞关闭流程)
            if (devHandle_ != nullptr)
                api.SetValue(devHandle_,
                             (std::to_string(config_.deviceChannel) + "/clear_auto_send")
                                 .c_str(),
                             "0");
            api.ResetCAN(channelHandle_);
        } catch (...) {
        }
        channelHandle_ = nullptr;
    }
    if (devHandle_ != nullptr) {
        try {
            api.CloseDevice();
        } catch (...) {
        }
        devHandle_ = nullptr;
    }
    rxBuf_.clear();
    isOpen_ = false;
    return true;
}

void ZlgUsbCanWrapper::FlushReceiveBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (channelHandle_ != nullptr) ZlgApi::Instance().ClearBuffer(channelHandle_);
}

void ZlgUsbCanWrapper::FlushTransmitBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (channelHandle_ != nullptr) ZlgApi::Instance().ClearBuffer(channelHandle_);
}

void ZlgUsbCanWrapper::ReadTaskProc() {
    while (!stop_) {
        try {
            // 连续读取直到无数据
            bool res = false;
            do {
                if (stop_) break;
                res = Read();
            } while (res && !stop_);

            if (!stop_) std::this_thread::sleep_for(5ms);
        } catch (...) {
            // 出错稍等后继续
            if (!stop_) std::this_thread::sleep_for(10ms);
        }
    }
}

bool ZlgUsbCanWrapper::Read() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || channelHandle_ == nullptr || rxBuf_.empty()) return false;

    auto& api = ZlgApi::Instance();
    // 非阻塞批量读取(wait_time=0),单次最多 RxBatchSize 帧
    uint32_t receiveNum = api.Receive(channelHandle_, rxBuf_.data(),
                                      static_cast<uint32_t>(rxBuf_.size()), 0);
    if (receiveNum == 0) return false;
    if (receiveNum > rxBuf_.size()) receiveNum = static_cast<uint32_t>(rxBuf_.size());

    for (uint32_t i = 0; i < receiveNum; ++i) {
        const ReceiveData& rec = rxBuf_[i];

        auto frame = std::make_shared<CANFrame>();
        // ZLG 的 can_id 高位自带 SocketCAN 风格 EFF/RTR/ERR 标志,直接透传
        frame->id = rec.frame.canId;
        // 钳制 dlc 到经典 CAN 有效范围,防止驱动返回异常值导致越界
        frame->dlc = std::min<int>(rec.frame.dlc, DataLenConverter::MaxCanDataLength);
        frame->flag = static_cast<int>(rec.frame.canId);
        frame->flags = ZlgFlagParser::Parse(rec.frame.canId, 0, false);
        // ZLG 时间戳单位为 0.1ms,换算为微秒(与 .NET 版一致)
        frame->timestamp = rec.timestamp * 100;
        frame->data.assign(rec.frame.data, rec.frame.data + frame->dlc);
        frame->dir = 1;

        if (eventHandler) eventHandler(*frame);
    }
    return true;
}

bool ZlgUsbCanWrapper::Send(FrameBase& frameBase) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || channelHandle_ == nullptr) return false;

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

    auto& api = ZlgApi::Instance();

    canFrame->dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame->id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);
    const bool isRTR = CanFrameHelper::IsRTR(canFrame->id);

    txBuf_ = TransmitData{};
    txBuf_.frame.canId = ZlgApi::MakeCanId(validId, isExtended, isRTR, false);
    txBuf_.frame.dlc = static_cast<uint8_t>(canFrame->dlc);
    std::memcpy(txBuf_.frame.data, canFrame->data.data(), canFrame->dlc);
    txBuf_.transmitType = 0;

    // 返回实际发送帧数,!=1 视为失败
    const uint32_t result = api.Transmit(channelHandle_, &txBuf_, 1);
    if (result != 1) return false;

    canFrame->flags = ZlgFlagParser::Parse(txBuf_.frame.canId, 0, false);
    if (eventHandler) eventHandler(*canFrame);
    return true;
}

} // namespace zlg
} // namespace canhw
