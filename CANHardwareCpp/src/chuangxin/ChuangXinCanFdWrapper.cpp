#include "ChuangXinCanFdWrapper.h"

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

bool ChuangXinCanFdWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    const uint32_t deviceType = static_cast<uint32_t>(config_.deviceModel);
    const uint32_t deviceIndex = config_.deviceIndex;
    const uint32_t channel = config_.deviceChannel;
    const bool useCanFD = config_.useCanFD;

    void* devHandle = nullptr;
    void* chHandle = nullptr;
    std::vector<ZcanReceiveData> tempRxCanBuf;
    std::vector<ZcanReceiveFdData> tempRxFdBuf;
    auto& api = ChuangXinCanFdApi::Instance();

    try {
        // 打开设备(同一物理设备的两个通道共享一个设备句柄)
        devHandle = api.OpenDevice(deviceType, deviceIndex);
        if (devHandle == nullptr) throw std::runtime_error("Open device failed");

        // CANFD 标准(ISO/BOSCH)
        uint32_t status = api.SetCANFDStandard(
            devHandle, channel, static_cast<uint32_t>(config_.canfdStandard));
        if (status != STATUS_OK)
            throw std::runtime_error("Set canfd standard failed(" +
                                     std::to_string(status) + ")");

        // 波特率(必须在 InitCAN 之前设置)
        if (config_.enableCustomBitrate) {
            if (config_.customBitrateContent.empty())
                throw std::invalid_argument("CustomBitrateContent is empty.");
            status = api.SetBaudRateCustom(devHandle, channel,
                                           config_.customBitrateContent.c_str());
            if (status != STATUS_OK)
                throw std::runtime_error("Set custom bitrate failed(" +
                                         std::to_string(status) + ")");
        } else {
            status = api.SetAbitBaud(
                devHandle, channel,
                ChuangXinCanFdApi::BuildCanBitrate(config_.arbitrationBitrate));
            if (status != STATUS_OK)
                throw std::runtime_error("Set arbitration bitrate failed(" +
                                         std::to_string(status) + ")");
            // 通道始终按 CANFD 类型初始化,数据段波特率一并设置
            status = api.SetDbitBaud(
                devHandle, channel,
                ChuangXinCanFdApi::BuildCanFdBitrate(config_.dataBitrate));
            if (status != STATUS_OK)
                throw std::runtime_error("Set data bitrate failed(" +
                                         std::to_string(status) + ")");
        }

        // 120Ω 终端电阻(官方样例在 InitCAN 之前设置)。
        // 关闭电阻时若设备不支持该属性则忽略;显式开启失败才视为错误。
        status = api.SetResistanceEnable(devHandle, channel,
                                         config_.enableA120Ohm ? 1u : 0u);
        if (status != STATUS_OK && config_.enableA120Ohm)
            throw std::runtime_error("Set internal resistance failed(" +
                                     std::to_string(status) + ")");

        // 初始化通道:USBCANFD 设备统一以 CANFD 类型初始化(官方样例做法),
        // 经典 CAN 帧仍可通过 ZCAN_Transmit/ZCAN_Receive 收发。
        ZcanChannelInitConfig initConfig{};
        initConfig.canType = TYPE_CANFD;
        initConfig.canfd.accCode = 0;
        initConfig.canfd.accMask = 0xFFFFFFFF;
        initConfig.canfd.abitTiming = 0;
        initConfig.canfd.dbitTiming = 0;
        initConfig.canfd.brp = 0;
        initConfig.canfd.filter = 0;
        initConfig.canfd.mode = static_cast<uint8_t>(config_.mode);
        initConfig.canfd.pad = 0;
        initConfig.canfd.reserved = 0;
        chHandle = api.InitCAN(devHandle, channel, &initConfig);
        if (chHandle == nullptr)
            throw std::runtime_error("Initialize can channel failed");

        // 过滤器(通道句柄,InitCAN 之后设置)
        if (config_.enableFilter) {
            status = api.ClearFilter(chHandle);
            if (status != STATUS_OK)
                throw std::runtime_error("Clear filter failed(" +
                                         std::to_string(status) + ")");
            status = api.SetFilterMode(chHandle, config_.extendedFilter ? 1u : 0u);
            if (status != STATUS_OK)
                throw std::runtime_error("Set filter mode failed(" +
                                         std::to_string(status) + ")");
            status = api.SetFilterStartID(chHandle, config_.filterFromId);
            if (status != STATUS_OK)
                throw std::runtime_error("Set filter start id failed(" +
                                         std::to_string(status) + ")");
            status = api.SetFilterEndID(chHandle, config_.filterToId);
            if (status != STATUS_OK)
                throw std::runtime_error("Set filter end id failed(" +
                                         std::to_string(status) + ")");
            status = api.AckFilter(chHandle);
            if (status != STATUS_OK)
                throw std::runtime_error("Ack filter failed(" + std::to_string(status) +
                                         ")");
        } else {
            // 未启用过滤:只清除已有滤波配置(best-effort;InitCAN 后默认接收所有帧)
            api.ClearFilter(chHandle);
        }

        // 分配持久化收发缓冲区
        tempRxCanBuf.resize(RxBatchSize);
        tempRxFdBuf.resize(RxBatchSize);

        // 启动通道
        status = api.StartCAN(chHandle);
        if (status != STATUS_OK)
            throw std::runtime_error("Start can channel failed(" +
                                     std::to_string(status) + ")");

        // 所有操作成功后,才提交状态
        openedDeviceType_ = deviceType;
        openedDeviceIndex_ = deviceIndex;
        openedChannel_ = channel;
        openedChannelHandle_ = chHandle;
        openedUseCanFD_ = useCanFD;
        rxCanBuf_ = std::move(tempRxCanBuf);
        rxFdBuf_ = std::move(tempRxFdBuf);
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&ChuangXinCanFdWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 回滚已打开的资源
        tempRxCanBuf.clear();
        tempRxFdBuf.clear();
        rxCanBuf_.clear();
        rxFdBuf_.clear();
        if (chHandle != nullptr) {
            try {
                api.ResetCAN(chHandle);
            } catch (...) {
            }
        }
        if (devHandle != nullptr) {
            try {
                api.CloseDevice(deviceType, deviceIndex);
            } catch (...) {
            }
        }
        isOpen_ = false;
        openedChannelHandle_ = nullptr;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool ChuangXinCanFdWrapper::Close() {
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    // 在锁外部等待读线程退出,避免死锁
    readThread_.Join();

    canhw::LockGuard lock(lock_);
    auto& api = ChuangXinCanFdApi::Instance();
    if (isOpen_) {
        try {
            api.ResetCAN(openedChannelHandle_);
        } catch (...) {
        }
        try {
            api.CloseDevice(openedDeviceType_, openedDeviceIndex_);
        } catch (...) {
        }
        openedChannelHandle_ = nullptr;
        rxCanBuf_.clear();
        rxFdBuf_.clear();
        isOpen_ = false;
    }
    return true;
}

void ChuangXinCanFdWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) ChuangXinCanFdApi::Instance().ClearBuffer(openedChannelHandle_);
}

void ChuangXinCanFdWrapper::FlushTransmitBuffer() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) ChuangXinCanFdApi::Instance().ClearBuffer(openedChannelHandle_);
}

void ChuangXinCanFdWrapper::ReadTaskProc() {
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

bool ChuangXinCanFdWrapper::Read() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_) return false;

    auto& api = ChuangXinCanFdApi::Instance();
    bool hasData = false;

    // 经典 CAN 队列(非阻塞批量读取)
    if (!rxCanBuf_.empty()) {
        const uint32_t receiveNum = api.Receive(
            openedChannelHandle_, rxCanBuf_.data(),
            static_cast<uint32_t>(rxCanBuf_.size()), 0);
        // 0xFFFFFFFF(-1) 表示设备不存在/USB 掉线,按无数据处理
        if (receiveNum != 0xFFFFFFFF) {
            if (receiveNum > 0) hasData = true;
            const size_t count = std::min<size_t>(receiveNum, RxBatchSize);
            for (size_t i = 0; i < count; ++i) {
                auto frame = ParseCanFrame(rxCanBuf_[i]);
                if (frame && eventHandler) eventHandler(*frame);
            }
        }
    }

    // CAN FD 队列(仅 FD 模式读取)
    if (openedUseCanFD_ && !rxFdBuf_.empty()) {
        const uint32_t receiveNum = api.ReceiveFD(
            openedChannelHandle_, rxFdBuf_.data(),
            static_cast<uint32_t>(rxFdBuf_.size()), 0);
        if (receiveNum != 0xFFFFFFFF) {
            if (receiveNum > 0) hasData = true;
            const size_t count = std::min<size_t>(receiveNum, RxBatchSize);
            for (size_t i = 0; i < count; ++i) {
                auto frame = ParseFdFrame(rxFdBuf_[i]);
                if (frame && eventHandler) eventHandler(*frame);
            }
        }
    }

    return hasData;
}

std::shared_ptr<CANFrame> ChuangXinCanFdWrapper::ParseCanFrame(
    const ZcanReceiveData& rec) {
    auto frame = std::make_shared<CANFrame>();
    // ZCAN 的 can_id 高位自带 SocketCAN 风格 EFF/RTR/ERR 标志,直接透传
    frame->id = rec.frame.canId;
    frame->dlc = std::min<int>(rec.frame.dlc, DataLenConverter::MaxCanDataLength);
    frame->flag = static_cast<int>(rec.frame.canId);
    frame->flags =
        ChuangXinCanFdFlagParser::Parse(rec.frame.canId, 0, false);
    // ControlCANFD 时间戳单位为微秒
    frame->timestamp = rec.timestamp;
    frame->data.assign(rec.frame.data, rec.frame.data + frame->dlc);
    frame->dir = 1;
    return frame;
}

std::shared_ptr<CANFrame> ChuangXinCanFdWrapper::ParseFdFrame(
    const ZcanReceiveFdData& rec) {
    auto frame = std::make_shared<CANFrame>();
    frame->id = rec.frame.canId;
    // CANFD 的 len 直接是实际字节数(0-64),无需 DLC 换算;钳制防止异常值
    frame->dlc = std::min<int>(rec.frame.len, DataLenConverter::MaxCanFdDataLength);
    frame->flag = static_cast<int>(rec.frame.canId);
    frame->flags =
        ChuangXinCanFdFlagParser::Parse(rec.frame.canId, rec.frame.flags, true);
    frame->timestamp = rec.timestamp;
    // 收到 CANFD 队列的帧即为 FD 帧;BRS/ESI 从 flags 解析
    frame->fdf = true;
    frame->brs = (rec.frame.flags & 0x01) != 0;
    frame->data.assign(rec.frame.data, rec.frame.data + frame->dlc);
    frame->dir = 1;
    return frame;
}

bool ChuangXinCanFdWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || openedChannelHandle_ == nullptr) return false;

    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    try {
        return canFrame->fdf ? SendFd(*canFrame) : SendCan(*canFrame);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

bool ChuangXinCanFdWrapper::SendCan(CANFrame& canFrame) {
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

    auto& api = ChuangXinCanFdApi::Instance();

    canFrame.dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame.id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame.id);

    txCanBuf_ = ZcanTransmitData{};
    txCanBuf_.frame.canId =
        ChuangXinCanFdApi::MakeCanId(validId, isExtended, isRTR, false);
    txCanBuf_.frame.dlc = static_cast<uint8_t>(canFrame.dlc);
    std::memset(txCanBuf_.frame.data, 0, sizeof(txCanBuf_.frame.data));
    if (!isRTR && canFrame.dlc > 0)
        std::memcpy(txCanBuf_.frame.data, canFrame.data.data(), canFrame.dlc);
    txCanBuf_.transmitType = 0;

    const uint32_t result = api.Transmit(openedChannelHandle_, &txCanBuf_, 1);
    if (result != 1) return false;

    canFrame.flags = ChuangXinCanFdFlagParser::Parse(txCanBuf_.frame.canId, 0, false);
    if (eventHandler) eventHandler(canFrame);
    return true;
}

bool ChuangXinCanFdWrapper::SendFd(CANFrame& canFrame) {
    if (!openedUseCanFD_)
        throw std::invalid_argument(
            "Classic CAN mode does not support CANFD frame(FDF).");
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

    auto& api = ChuangXinCanFdApi::Instance();

    canFrame.dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame.id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame.id);
    const bool isRTR = CanFrameHelper::IsRTR(canFrame.id);

    txFdBuf_ = ZcanTransmitFdData{};
    txFdBuf_.frame.canId =
        ChuangXinCanFdApi::MakeCanId(validId, isExtended, isRTR, false);
    // CANFD 的 len 直接是实际字节数(0-64)
    txFdBuf_.frame.len = static_cast<uint8_t>(canFrame.dlc);
    // canfd_frame.flags: bit0=BRS, bit1=ESI
    txFdBuf_.frame.flags = canFrame.brs ? 0x01 : 0x00;
    std::memcpy(txFdBuf_.frame.data, canFrame.data.data(), canFrame.dlc);
    txFdBuf_.transmitType = 0;

    const uint32_t result = api.TransmitFD(openedChannelHandle_, &txFdBuf_, 1);
    if (result != 1) return false;

    canFrame.flags = ChuangXinCanFdFlagParser::Parse(txFdBuf_.frame.canId,
                                                     txFdBuf_.frame.flags, true);
    if (eventHandler) eventHandler(canFrame);
    return true;
}

} // namespace chuangxin
} // namespace canhw
