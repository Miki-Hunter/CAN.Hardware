#include "KvaserCanFDWrapper.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "KvaserFlagParser.h"

namespace canhw {
namespace kvaser {

using namespace std::chrono_literals;
using namespace canlib;

bool KvaserCanFDWrapper::Open() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isOpen_) return true;

    auto& api = KvaserApi::Instance();
    int32_t tempHandle = -1;
    try {
        api.InitializeLibrary();
        tempHandle = api.OpenChannel(static_cast<int32_t>(config_.canFdDeviceChannel),
                                     canOPEN_CAN_FD | canOPEN_EXCLUSIVE);
        if (tempHandle < 0)
            throw std::runtime_error("Open channel failed(" +
                                     std::to_string(tempHandle) + ", " +
                                     KvaserApi::StatusText(tempHandle) + ")");

        // 设置波特率
        if (config_.enableCustomBitrate) {
            const KvBusParamsTq arbit{config_.arbitrationTq, config_.arbitrationPhase1,
                                      config_.arbitrationPhase2, config_.arbitrationSjw,
                                      config_.arbitrationProp, config_.arbitrationPrescale};
            const KvBusParamsTq data{config_.dataTq,     config_.dataPhase1,
                                     config_.dataPhase2, config_.dataSjw,
                                     config_.dataProp,   config_.dataPrescale};
            const int32_t st = api.SetBusParamsFdTq(tempHandle, arbit, data);
            if (st != canOK)
                throw std::runtime_error("SetCustomBusParams failed(" +
                                         KvaserApi::StatusText(st) + ")");
        } else {
            int32_t st = api.SetBusParams(
                tempHandle,
                KvaserApi::BuildCanBitrate(config_.canFdArbitrationBitrate), 0, 0, 0, 0,
                0);
            if (st != canOK)
                throw std::runtime_error("Set arbitration bitrate failed(" +
                                         KvaserApi::StatusText(st) + ")");
            st = api.SetBusParamsFd(tempHandle,
                                    KvaserApi::BuildCanFdBitrate(config_.canFdDataBitrate),
                                    0, 0, 0);
            if (st != canOK)
                throw std::runtime_error("Set data bitrate failed(" +
                                         KvaserApi::StatusText(st) + ")");
        }

        // 启动总线
        int32_t st = api.BusOn(tempHandle);
        if (st != canOK)
            throw std::runtime_error("Open bus on failed(" + KvaserApi::StatusText(st) +
                                     ")");

        // 设置过滤器
        if (config_.enableFilter) {
            st = api.SetAcceptanceFilter(tempHandle, config_.filterCode,
                                         config_.filterMask,
                                         config_.filterIsExtended ? 1 : 0);
            if (st != canOK)
                throw std::runtime_error("Set frame filter failed(" +
                                         KvaserApi::StatusText(st) + ")");
        } else {
            st = api.SetAcceptanceFilter(tempHandle, 0, 0, 0);
            if (st != canOK)
                throw std::runtime_error("Initial can filter failed(" +
                                         KvaserApi::StatusText(st) + ")");
        }

        // 设置时间戳最小单位为 1 微秒
        st = api.SetTimeScale(tempHandle, 1, 4);
        if (st != canOK)
            throw std::runtime_error("set can scale failed(" + KvaserApi::StatusText(st) +
                                     ")");

        handle_ = tempHandle;
        isOpen_ = true;
        stop_ = false;
        readThread_ = std::thread(&KvaserCanFDWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        if (tempHandle >= 0) {
            try {
                api.BusOff(tempHandle);
                api.Close(tempHandle);
            } catch (...) {
            }
        }
        isOpen_ = false;
        handle_ = -1;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool KvaserCanFDWrapper::Close() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    if (readThread_.joinable()) readThread_.join();

    std::lock_guard<std::mutex> lock(lock_);
    auto& api = KvaserApi::Instance();
    if (handle_ >= 0) {
        try {
            api.BusOff(handle_);
            api.Close(handle_);
        } catch (...) {
        }
        api.UnloadLibrary();
        handle_ = -1;
    }
    isOpen_ = false;
    return true;
}

void KvaserCanFDWrapper::FlushReceiveBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (handle_ >= 0) KvaserApi::Instance().FlushReceiveQueue(handle_);
}

void KvaserCanFDWrapper::FlushTransmitBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    if (handle_ >= 0) KvaserApi::Instance().FlushTransmitQueue(handle_);
}

void KvaserCanFDWrapper::ReadTaskProc() {
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

bool KvaserCanFDWrapper::Read() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || handle_ < 0) return false;

    auto& api = KvaserApi::Instance();

    uint8_t buffer[64] = {};
    int32_t id = 0;
    uint32_t dlc = 0, flag = 0, time = 0;
    const int32_t st = api.Read(handle_, &id, buffer, &dlc, &flag, &time);
    // 钳制 dlc 到 CANFD 有效范围,防止驱动返回异常值导致越界
    dlc = std::min<uint32_t>(dlc, DataLenConverter::MaxCanFdDataLength);

    if (st != canOK || id == 0) return false;

    auto frame = std::make_shared<CANFrame>();
    frame->id = CanFrameHelper::GetId(id);
    frame->dlc = static_cast<int>(dlc);
    frame->flag = static_cast<int>(flag);
    frame->flags = KvaserCanFlagParser::Parse(flag);
    frame->timestamp = time;  // TimeScale=1µs
    frame->data.assign(buffer, buffer + frame->dlc);
    frame->dir = 1;

    if (eventHandler) eventHandler(*frame);
    return true;
}

bool KvaserCanFDWrapper::Send(FrameBase& frameBase) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || handle_ < 0) return false;

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

    auto& api = KvaserApi::Instance();

    canFrame->dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame->id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);
    const bool isRTR = CanFrameHelper::IsRTR(canFrame->id);
    uint32_t flag = isExtended ? canMSG_EXT : canMSG_STD;
    if (isRTR) flag |= canMSG_RTR;
    if (canFrame->fdf) flag |= canFDMSG_FDF;
    if (canFrame->brs) flag |= canFDMSG_BRS;

    int32_t st = api.Write(handle_, static_cast<int32_t>(validId), canFrame->data.data(),
                           static_cast<uint32_t>(canFrame->dlc), flag);
    // Kvaser CAN 驱动 bug 临时方案:写入失败(TXBUFOFL)再次写入,超过 5 次报错
    int maxWriteCount = 0;
    while (st == canERR_TXBUFOFL && maxWriteCount < 5) {
        st = api.Write(handle_, static_cast<int32_t>(validId), canFrame->data.data(),
                       static_cast<uint32_t>(canFrame->dlc), flag);
        if (st == canOK) break;
        std::this_thread::sleep_for(50ms);
        ++maxWriteCount;
    }
    if (st != canOK) return false;

    canFrame->flags = KvaserCanFlagParser::Parse(flag);
    if (eventHandler) eventHandler(*canFrame);
    return true;
}

} // namespace kvaser
} // namespace canhw
