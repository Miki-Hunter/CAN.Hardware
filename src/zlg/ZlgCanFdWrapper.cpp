#include "ZlgCanFdWrapper.h"

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

bool ZlgCanFdWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    void* tempDevHandle = nullptr;
    void* tempChannelHandle = nullptr;
    std::vector<ReceiveData> tempRxCanBuf;
    std::vector<ReceiveFdData> tempRxFdBuf;
    auto& api = ZlgApi::Instance();

    try {
        const bool useCanFD = config_.useCanFD;
        const std::string chn = std::to_string(config_.deviceChannel);

        // 打开设备(同一物理设备共享句柄,多通道复用)
        tempDevHandle = api.OpenDevice(static_cast<uint32_t>(config_.deviceModel),
                                       config_.deviceIndex, 0);
        if (tempDevHandle == nullptr) throw std::runtime_error("Open device failed");

        uint32_t status = 1;
        if (useCanFD) {
            // CAN FD 标准(ISO/BOSCH),仅 CANFD 模式需要
            status = api.SetValue(
                tempDevHandle, (chn + "/canfd_standard").c_str(),
                std::to_string(static_cast<int>(config_.canfdStandard)).c_str());
            if (status != 1)
                throw std::runtime_error("Set canfd standard failed(" +
                                         std::to_string(status) + ")");
            // 关闭合并接收,保证每帧独立上报、时间戳准确(best-effort,部分固件不支持)
            api.SetValue(tempDevHandle, (chn + "/set_device_recv_merge").c_str(), "0");
        }

        // 设置波特率(InitCAN 之前)
        if (config_.enableCustomBitrate) {
            status = api.SetValue(tempDevHandle, (chn + "/baud_rate_custom").c_str(),
                                  config_.customBitrateContent.c_str());
            if (status != 1)
                throw std::runtime_error("Set custom baud rate failed(" +
                                         std::to_string(status) + ")");
        } else if (useCanFD) {
            const std::string abit =
                std::to_string(ZlgApi::BuildCanBitrate(config_.arbitrationBitrate));
            status = api.SetValue(tempDevHandle,
                                  (chn + "/canfd_abit_baud_rate").c_str(), abit.c_str());
            if (status != 1)
                throw std::runtime_error("Set arbitration bitrate failed(" +
                                         std::to_string(status) + ")");
            const std::string dbit =
                std::to_string(ZlgApi::BuildCanFdBitrate(config_.dataBitrate));
            status = api.SetValue(tempDevHandle,
                                  (chn + "/canfd_dbit_baud_rate").c_str(), dbit.c_str());
            if (status != 1)
                throw std::runtime_error("Set data bitrate failed(" +
                                         std::to_string(status) + ")");
        } else {
            const std::string val =
                std::to_string(ZlgApi::BuildCanBitrate(config_.arbitrationBitrate));
            status = api.SetValue(tempDevHandle, (chn + "/baud_rate").c_str(), val.c_str());
            if (status != 1)
                throw std::runtime_error("Set baud rate failed(" +
                                         std::to_string(status) + ")");
        }

        // 初始化通道(按模式选择 can_type 与对应的联合体配置)
        ChannelInitConfig initConfig{};
        initConfig.canType = useCanFD ? 1u : 0u;
        initConfig.config.can.accCode = 0;
        initConfig.config.can.accMask = 0xFFFFFFFF;
        initConfig.config.can.mode = 0;
        initConfig.config.canfd.accCode = 0;
        initConfig.config.canfd.accMask = 0xFFFFFFFF;
        initConfig.config.canfd.mode = 0;
        tempChannelHandle =
            api.InitCAN(tempDevHandle, config_.deviceChannel, &initConfig);
        if (tempChannelHandle == nullptr) {
            // 学习自 QC2015P_Cpp_Plus 的实机经验:快速断开/重开等场景,
            // 驱动可能需要短暂稳定期后才允许 InitCAN,500ms 后重试一次
            canhw::SleepMs(500);
            tempChannelHandle =
                api.InitCAN(tempDevHandle, config_.deviceChannel, &initConfig);
        }
        if (tempChannelHandle == nullptr)
            throw std::runtime_error("Initialize channel failed");

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

        // 分配持久化收发缓冲区(按当前模式选择结构体布局,FD 结构体更大)
        if (useCanFD) {
            tempRxFdBuf.resize(RxBatchSize);
        } else {
            tempRxCanBuf.resize(RxBatchSize);
        }

        // 启动通道
        status = api.StartCAN(tempChannelHandle);
        if (status != 1)
            throw std::runtime_error("Start channel failed(" + std::to_string(status) +
                                     ")");

        // 所有操作成功后,才提交状态
        devHandle_ = tempDevHandle;
        channelHandle_ = tempChannelHandle;
        openedUseCanFD_ = useCanFD;
        rxCanBuf_ = std::move(tempRxCanBuf);
        rxFdBuf_ = std::move(tempRxFdBuf);
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&ZlgCanFdWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 回滚已打开的资源
        tempRxCanBuf.clear();
        tempRxFdBuf.clear();
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

bool ZlgCanFdWrapper::Close() {
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    // 在锁外部等待读线程退出,避免死锁
    readThread_.Join();

    canhw::LockGuard lock(lock_);
    auto& api = ZlgApi::Instance();

    if (channelHandle_ != nullptr) {
        try {
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
    rxCanBuf_.clear();
    rxFdBuf_.clear();
    isOpen_ = false;
    return true;
}

void ZlgCanFdWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (channelHandle_ != nullptr) ZlgApi::Instance().ClearBuffer(channelHandle_);
}

void ZlgCanFdWrapper::FlushTransmitBuffer() {
    canhw::LockGuard lock(lock_);
    if (channelHandle_ != nullptr) ZlgApi::Instance().ClearBuffer(channelHandle_);
}

void ZlgCanFdWrapper::ReadTaskProc() {
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

bool ZlgCanFdWrapper::Read() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || channelHandle_ == nullptr) return false;

    auto& api = ZlgApi::Instance();

    if (openedUseCanFD_) {
        if (rxFdBuf_.empty()) return false;
        // 学习自 QC2015P_Cpp_Plus:先查接收数量,空队列直接返回(1=CANFD)
        const uint32_t pending = api.GetReceiveNum(channelHandle_, 1);
        if (pending == 0) return false;
        const uint32_t readCount =
            pending == UINT32_MAX
                ? static_cast<uint32_t>(rxFdBuf_.size())
                : std::min<uint32_t>(pending, static_cast<uint32_t>(rxFdBuf_.size()));
        uint32_t receiveNum = api.ReceiveFD(channelHandle_, rxFdBuf_.data(), readCount, 0);
        if (receiveNum == 0) return false;
        if (receiveNum > rxFdBuf_.size())
            receiveNum = static_cast<uint32_t>(rxFdBuf_.size());
        for (uint32_t i = 0; i < receiveNum; ++i) {
            auto frame = ParseFdFrame(rxFdBuf_[i]);
            if (frame && eventHandler) eventHandler(*frame);
        }
        return true;
    }

    if (rxCanBuf_.empty()) return false;
    // 先查接收数量(type:0=经典 CAN)
    const uint32_t pendingCan = api.GetReceiveNum(channelHandle_, 0);
    if (pendingCan == 0) return false;
    const uint32_t readCanCount =
        pendingCan == UINT32_MAX
            ? static_cast<uint32_t>(rxCanBuf_.size())
            : std::min<uint32_t>(pendingCan, static_cast<uint32_t>(rxCanBuf_.size()));
    uint32_t receiveNum = api.Receive(channelHandle_, rxCanBuf_.data(), readCanCount, 0);
    if (receiveNum == 0) return false;
    if (receiveNum > rxCanBuf_.size())
        receiveNum = static_cast<uint32_t>(rxCanBuf_.size());
    for (uint32_t i = 0; i < receiveNum; ++i) {
        auto frame = ParseCanFrame(rxCanBuf_[i]);
        if (frame && eventHandler) eventHandler(*frame);
    }
    return true;
}

std::shared_ptr<CANFrame> ZlgCanFdWrapper::ParseFdFrame(const ReceiveFdData& rec) {
    auto frame = std::make_shared<CANFrame>();
    // ZLG 的 can_id 高位自带 SocketCAN 风格 EFF/RTR/ERR 标志,直接透传
    frame->id = rec.frame.canId;
    // ZLG FD 帧 len 直接是实际字节数(0-64),无需 DLC 编码换算;钳制防止异常值
    frame->dlc = std::min<int>(rec.frame.len, DataLenConverter::MaxCanFdDataLength);
    frame->flag = static_cast<int>(rec.frame.canId);
    frame->flags = ZlgFlagParser::Parse(rec.frame.canId, rec.frame.flags, true);
    // ZLG 时间戳单位为 0.1ms,换算为微秒(与 .NET 版一致)
    frame->timestamp = rec.timestamp * 100;
    // ZLG FD 帧 flags 只有 bit0=BRS/bit1=ESI,无 FDF 位;BRS 置位或长度>8 即判定为 FD 帧
    frame->brs = (rec.frame.flags & 0x01) != 0;
    frame->fdf = frame->brs || frame->dlc > DataLenConverter::MaxCanDataLength;
    frame->data.assign(rec.frame.data, rec.frame.data + frame->dlc);
    frame->dir = 1;
    return frame;
}

std::shared_ptr<CANFrame> ZlgCanFdWrapper::ParseCanFrame(const ReceiveData& rec) {
    auto frame = std::make_shared<CANFrame>();
    frame->id = rec.frame.canId;
    frame->dlc = std::min<int>(rec.frame.dlc, DataLenConverter::MaxCanDataLength);
    frame->flag = static_cast<int>(rec.frame.canId);
    frame->flags = ZlgFlagParser::Parse(rec.frame.canId, 0, false);
    frame->timestamp = rec.timestamp * 100;
    frame->data.assign(rec.frame.data, rec.frame.data + frame->dlc);
    frame->dir = 1;
    return frame;
}

bool ZlgCanFdWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || channelHandle_ == nullptr) return false;

    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    try {
        // FD 模式下 ZCAN_TransmitFD 同时兼容经典 CAN 帧(flags=0 且 len<=8)
        return openedUseCanFD_ ? SendFd(*canFrame) : SendCan(*canFrame);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

bool ZlgCanFdWrapper::SendCan(CANFrame& canFrame) {
    // 发送前校验数据长度合法性(非法参数直接抛异常,不做静默失败)
    if (canFrame.fdf || canFrame.brs)
        throw std::invalid_argument(
            "Classic CAN mode does not support CANFD frame(FDF/BRS).");
    if (!DataLenConverter::IsValidCanLength(canFrame.dlc))
        throw std::invalid_argument("Invalid CAN DLC(" + std::to_string(canFrame.dlc) +
                                    "), valid range: 0-" +
                                    std::to_string(DataLenConverter::MaxCanDataLength) +
                                    ".");
    if (static_cast<int>(canFrame.data.size()) < canFrame.dlc)
        throw std::invalid_argument("Data length(" + std::to_string(canFrame.data.size()) +
                                    ") is less than DLC(" +
                                    std::to_string(canFrame.dlc) + ").");

    auto& api = ZlgApi::Instance();

    canFrame.dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame.id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame.id);
    const bool isRTR = CanFrameHelper::IsRTR(canFrame.id);

    txCanBuf_ = TransmitData{};
    txCanBuf_.frame.canId = ZlgApi::MakeCanId(validId, isExtended, isRTR, false);
    txCanBuf_.frame.dlc = static_cast<uint8_t>(canFrame.dlc);
    std::memcpy(txCanBuf_.frame.data, canFrame.data.data(), canFrame.dlc);
    txCanBuf_.transmitType = 0;

    const uint32_t result = api.Transmit(channelHandle_, &txCanBuf_, 1);
    if (result != 1) return false;

    canFrame.flags = ZlgFlagParser::Parse(txCanBuf_.frame.canId, 0, false);
    if (eventHandler) eventHandler(canFrame);
    return true;
}

bool ZlgCanFdWrapper::SendFd(CANFrame& canFrame) {
    // 发送前校验数据长度合法性(非法参数直接抛异常,不做静默失败)
    if (canFrame.brs && !canFrame.fdf)
        throw std::invalid_argument("BRS is only valid for CANFD frame(FDF must be set).");
    const bool validLength = canFrame.fdf
                                 ? DataLenConverter::IsValidCanFdLength(canFrame.dlc)
                                 : DataLenConverter::IsValidCanLength(canFrame.dlc);
    if (!validLength)
        throw std::invalid_argument(
            canFrame.fdf ? "Invalid CANFD DLC(" + std::to_string(canFrame.dlc) +
                               "), valid lengths: 0-8,12,16,20,24,32,48,64."
                         : "Invalid CAN DLC(" + std::to_string(canFrame.dlc) +
                               "), valid range: 0-" +
                               std::to_string(DataLenConverter::MaxCanDataLength) + ".");
    if (static_cast<int>(canFrame.data.size()) < canFrame.dlc)
        throw std::invalid_argument("Data length(" + std::to_string(canFrame.data.size()) +
                                    ") is less than DLC(" +
                                    std::to_string(canFrame.dlc) + ").");

    auto& api = ZlgApi::Instance();

    canFrame.dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame.id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame.id);
    const bool isRTR = CanFrameHelper::IsRTR(canFrame.id);

    txFdBuf_ = TransmitFdData{};
    txFdBuf_.frame.canId = ZlgApi::MakeCanId(validId, isExtended, isRTR, false);
    // ZLG FD 帧 len 直接是实际字节数(0-64),无需 DLC 编码换算
    txFdBuf_.frame.len = static_cast<uint8_t>(canFrame.dlc);
    // canfd_frame.flags: bit0=BRS, bit1=ESI
    txFdBuf_.frame.flags = canFrame.brs ? 0x01 : 0x00;
    std::memcpy(txFdBuf_.frame.data, canFrame.data.data(), canFrame.dlc);
    txFdBuf_.transmitType = 0;

    const uint32_t result = api.TransmitFD(channelHandle_, &txFdBuf_, 1);
    if (result != 1) return false;

    canFrame.flags = ZlgFlagParser::Parse(txFdBuf_.frame.canId, txFdBuf_.frame.flags,
                                          canFrame.fdf);
    if (eventHandler) eventHandler(canFrame);
    return true;
}

} // namespace zlg
} // namespace canhw
