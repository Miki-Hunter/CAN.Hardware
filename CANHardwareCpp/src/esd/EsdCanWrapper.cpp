#include "EsdCanWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "EsdFlagParser.h"

namespace canhw {
namespace esd {

namespace {

// ntcan 默认关闭所有 ID 的接收,需按 ID 段显式使能
// idCount 语义为"使能个数":标准段 0x800 个(0..0x7FF),扩展段 0x20000000 个
void EnableAllIdRegions(void* handle) {
    int32_t cnt = static_cast<int32_t>(NTCAN_STD_ID_MASK + 1);
    EsdCanResult result = EsdApi::Instance().CanIdRegionAdd(handle, 0, &cnt);
    if (result != RESULT_OK)
        throw std::runtime_error("Enable standard ID region failed(" +
                                 EsdApi::ResultText(result) + ")");

    cnt = static_cast<int32_t>(NTCAN_EXT_ID_MASK + 1);
    result = EsdApi::Instance().CanIdRegionAdd(
        handle, static_cast<int32_t>(NTCAN_EXT_FLAG), &cnt);
    if (result != RESULT_OK)
        throw std::runtime_error("Enable extended ID region failed(" +
                                 EsdApi::ResultText(result) + ")");
}

} // namespace

bool EsdCanWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    auto& api = EsdApi::Instance();
    void* tempHandle = nullptr;

    try {
        // 定位并加载 ntcan64.dll
        api.EnsureLoaded();

        // 打开通道;不传 NO_RTR 以保留 RTR 接收能力;
        // txTimeout=0 立即返回,rxTimeout=200ms 支撑接收线程轮询退出
        EsdCanResult result =
            api.CanOpen(config_.channel, EsdCanMode::NONE, 1000, 1000, 0,
                        RX_TIMEOUT_MS, &tempHandle);
        if (result != RESULT_OK)
            throw std::runtime_error("canOpen failed(" + EsdApi::ResultText(result) +
                                     ")");
        // 非连接模式下驱动可能返回 OK 但句柄无效
        if (tempHandle == nullptr || tempHandle == reinterpret_cast<void*>(-1)) {
            tempHandle = nullptr;
            throw std::runtime_error("canOpen: invalid handle on channel " +
                                     std::to_string(config_.channel));
        }

        // 数值波特率:USER_BAUDRATE_NUM | bps;自定义模式下取 CustomBitrate
        uint32_t bitrate =
            config_.enableCustomBitrate
                ? config_.customBitrate
                : static_cast<uint32_t>(config_.canBitrate);
        // USER_BAUDRATE_NUM 模式下低 28 位为 bps 数值,0 或超出 28 位直接拦截
        if (bitrate == 0 || bitrate > 0x0FFFFFFF)
            throw std::invalid_argument(
                "Invalid bitrate(" + std::to_string(bitrate) +
                "), valid range: 1-268435455 bps.");
        result = api.CanSetBaudrate(tempHandle,
                                    EsdBaudMode::USER_BAUDRATE_NUM | bitrate);
        if (result != RESULT_OK)
            throw std::runtime_error("Set baudrate " + std::to_string(bitrate) +
                                     " failed(" + EsdApi::ResultText(result) + ")");

        // ntcan 默认关闭所有 ID,需显式使能接收范围
        EnableAllIdRegions(tempHandle);

        // 所有操作成功后,才提交状态
        handle_ = tempHandle;
        isOpen_ = true;
        stop_ = false;
        readThread_.Start(&EsdCanWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 清理已打开的通道
        if (tempHandle != nullptr) {
            try {
                api.CanClose(tempHandle);
            } catch (...) {
            }
        }
        isOpen_ = false;
        handle_ = nullptr;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool EsdCanWrapper::Close() {
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    // 在锁外部等待读线程退出(canReadT 最坏 200ms 超时后退出),避免死锁
    readThread_.Join();

    canhw::LockGuard lock(lock_);
    void* handle = handle_.load();
    if (handle != nullptr) {
        try {
            EsdApi::Instance().CanClose(handle);
        } catch (...) {
        }
        handle_ = nullptr;
    }
    isOpen_ = false;
    return true;
}

void EsdCanWrapper::FlushReceiveBuffer() {
    // ntcan 子集无队列清空接口,空实现(与 .NET 一致)
}

void EsdCanWrapper::FlushTransmitBuffer() {
    // ntcan 子集无队列清空接口,空实现(与 .NET 一致)
}

void EsdCanWrapper::ReadTaskProc() {
    while (!stop_) {
        try {
            // canReadT 阻塞至多有帧或 200ms 超时,无需额外延时
            ReadBlocking();
        } catch (...) {
            if (!stop_) canhw::SleepMs(10);
        }
    }
}

void EsdCanWrapper::ReadBlocking() {
    if (!isOpen_) return;
    void* handle = handle_.load();
    if (handle == nullptr) return;

    EsdCanMsg msg{};
    int32_t len = 1;
    const EsdCanResult result = EsdApi::Instance().CanRead(handle, &msg, &len);
    // RX_TIMEOUT/强制关闭等直接结束本轮
    if (result != RESULT_OK || len <= 0) return;
    if (!isOpen_) return;

    const uint8_t rawLen = msg.base.len;
    // 钳制 dlc 到经典 CAN 有效范围,防止驱动返回异常值导致越界
    const int dlc = std::min<int>(rawLen & LEN_MASK_DLC,
                                  DataLenConverter::MaxCanDataLength);
    auto frame = std::make_shared<CANFrame>();
    frame->id = EsdApi::BuildManagedId(msg.base.id, rawLen);
    frame->dlc = dlc;
    frame->dir = 1;
    frame->flag = rawLen;
    frame->flags =
        EsdCanFlagParser::Parse(msg.base.id, rawLen, msg.base.esi, msg.base.msgLost);
    // ntcan 时间戳单位为 µs,与工程统一单位一致,直接使用
    frame->timestamp = msg.timestamp;
    // Data 为 8 字节小端
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&msg.data);
    frame->data.assign(raw, raw + dlc);

    if (eventHandler) eventHandler(*frame);
}

bool EsdCanWrapper::Send(FrameBase& frameBase) {
    canhw::LockGuard lock(lock_);
    if (!isOpen_ || handle_.load() == nullptr) return false;

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
        auto& api = EsdApi::Instance();

        canFrame->dir = 0;
        const bool isRtr = CanFrameHelper::IsRTR(canFrame->id);

        EsdCanMsg msg{};
        msg.base.id = EsdApi::BuildNtcanId(canFrame->id);
        // bit0-3=DLC, bit4=RTR
        msg.base.len = static_cast<uint8_t>(
            canFrame->dlc | (isRtr ? LEN_FLAG_RTR : 0));
        uint64_t dataValue = 0;
        std::memcpy(&dataValue, canFrame->data.data(), canFrame->dlc);
        msg.data = dataValue;

        // txTimeout=0(canOpen 时设定),队列满时 len 回读为 0
        int32_t len = 1;
        const EsdCanResult result = api.CanWrite(handle_.load(), &msg, &len);
        if (result != RESULT_OK || len == 0) return false;

        // 发送成功后回显
        canFrame->flag = msg.base.len;
        canFrame->flags = EsdCanFlagParser::Parse(msg.base.id, msg.base.len, 0, 0);
        if (eventHandler) eventHandler(*canFrame);
        return true;
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

} // namespace esd
} // namespace canhw
