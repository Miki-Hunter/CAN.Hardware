#include "KvaserCanWrapper.h"

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

bool KvaserCanWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    auto& api = KvaserApi::Instance();
    int32_t tempHandle = -1;
    try {
        api.InitializeLibrary();
        tempHandle = api.OpenChannel(static_cast<int32_t>(config_.canDeviceChannel),
                                     canOPEN_ACCEPT_VIRTUAL);
        if (tempHandle < 0)
            throw std::runtime_error("Open channel failed(" +
                                     std::to_string(tempHandle) + ", " +
                                     KvaserApi::StatusText(tempHandle) + ")");

        // 设置波特率
        if (config_.enableCustomBitrate) {
            const KvBusParamsTq dataBitra{config_.tq,      config_.phase1, config_.phase2,
                                          config_.sjw,     config_.prop,   config_.prescale};
            const int32_t st = api.SetBusParamsTq(tempHandle, dataBitra);
            if (st != canOK)
                throw std::runtime_error("SetCustomBusParams failed(" +
                                         KvaserApi::StatusText(st) + ")");
        } else {
            const int32_t st = api.SetBusParams(
                tempHandle, KvaserApi::BuildCanBitrate(config_.canBitrate), 0, 0, 0, 0, 0);
            if (st != canOK)
                throw std::runtime_error("Set bitrate failed(" +
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

        // 所有操作成功后,才提交状态
        handle_ = tempHandle;
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&KvaserCanWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 回滚已打开的资源(与 .NET 一致:失败路径不 UnloadLibrary)
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

bool KvaserCanWrapper::Close() {
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    // 在锁外部等待读线程退出,避免死锁
    readThread_.Join();

    canhw::LockGuard lock(lock_);
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

void KvaserCanWrapper::FlushReceiveBuffer() {
    canhw::LockGuard lock(lock_);
    if (handle_ >= 0) KvaserApi::Instance().FlushReceiveQueue(handle_);
}

void KvaserCanWrapper::FlushTransmitBuffer() {
    canhw::LockGuard lock(lock_);
    if (handle_ >= 0) KvaserApi::Instance().FlushTransmitQueue(handle_);
}

void KvaserCanWrapper::ReadTaskProc() {
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

bool KvaserCanWrapper::Read() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || handle_ < 0) return false;

    auto& api = KvaserApi::Instance();

    uint8_t buffer[64] = {};
    int32_t id = 0;
    uint32_t dlc = 0, flag = 0, time = 0;
    const int32_t st = api.Read(handle_, &id, buffer, &dlc, &flag, &time);
    // 钳制 dlc 到经典 CAN 有效范围,防止驱动返回异常值导致越界
    dlc = std::min<uint32_t>(dlc, DataLenConverter::MaxCanDataLength);

    // 与 .NET 一致:仅在读取成功且 id != 0 时派发
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

bool KvaserCanWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || handle_ < 0) return false;

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

    auto& api = KvaserApi::Instance();

    canFrame->dir = 0;
    const uint32_t validId = CanFrameHelper::GetId(canFrame->id);
    const bool isExtended = CanFrameHelper::IsEFF(canFrame->id);
    const bool isRTR = CanFrameHelper::IsRTR(canFrame->id);
    uint32_t flag = isExtended ? canMSG_EXT : canMSG_STD;
    if (isRTR) flag |= canMSG_RTR;

    int32_t st = api.Write(handle_, static_cast<int32_t>(validId), canFrame->data.data(),
                           static_cast<uint32_t>(canFrame->dlc), flag);
    // Kvaser CAN 驱动 bug 临时方案:写入失败(TXBUFOFL)再次写入,超过 5 次报错
    int maxWriteCount = 0;
    while (st == canERR_TXBUFOFL && maxWriteCount < 5) {
        st = api.Write(handle_, static_cast<int32_t>(validId), canFrame->data.data(),
                       static_cast<uint32_t>(canFrame->dlc), flag);
        if (st == canOK) break;
        canhw::SleepMs(50);
        ++maxWriteCount;
    }
    if (st != canOK) return false;

    canFrame->flags = KvaserCanFlagParser::Parse(flag);
    if (eventHandler) eventHandler(*canFrame);
    return true;
}

} // namespace kvaser
} // namespace canhw
