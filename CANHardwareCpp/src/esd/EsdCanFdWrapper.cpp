#include "EsdCanFdWrapper.h"

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

// ntcan 默认关闭所有 ID 的接收,需按 ID 段显式使能(与经典 CAN 封装一致)
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

bool EsdCanFdWrapper::Open() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isOpen_) return true;

    auto& api = EsdApi::Instance();
    void* tempHandle = nullptr;

    try {
        // 定位并加载 ntcan64.dll
        api.EnsureLoaded();

        // 打开 FD 通道;不传 NO_RTR 以保留经典帧 RTR 接收能力;
        // txTimeout=0 立即返回,rxTimeout=200ms 支撑接收线程轮询退出
        EsdCanResult result =
            api.CanOpen(config_.channel, EsdCanMode::FD, 1000, 1000, 0,
                        RX_TIMEOUT_MS, &tempHandle);
        if (result != RESULT_OK)
            throw std::runtime_error("canOpen failed(" + EsdApi::ResultText(result) +
                                     ")");
        if (tempHandle == nullptr || tempHandle == reinterpret_cast<void*>(-1)) {
            tempHandle = nullptr;
            throw std::runtime_error("canOpen: invalid handle on channel " +
                                     std::to_string(config_.channel));
        }

        // CAN FD 波特率:canSetBaudrateX 数值模式(NUM),仲裁段/数据段直接填 bps
        const uint32_t arbBitrate =
            config_.enableCustomBitrate
                ? config_.customArbitrationBitrate
                : static_cast<uint32_t>(config_.canFdArbitrationBitrate);
        const uint32_t dataBitrate =
            config_.enableCustomBitrate
                ? config_.customDataBitrate
                : static_cast<uint32_t>(config_.canFdDataBitrate);
        if (arbBitrate == 0 || dataBitrate == 0)
            throw std::invalid_argument(
                "Invalid baudrate(arb=" + std::to_string(arbBitrate) +
                ", data=" + std::to_string(dataBitrate) +
                "), must be greater than 0.");

        EsdBaudrateXNum baudConfig{};
        baudConfig.mode = BAUDRATE_MODE_NUM;
        baudConfig.flags = BAUDRATE_FLAG_FD;
        baudConfig.tdc = EsdTdcCfg{};
        baudConfig.arb.value = arbBitrate;
        baudConfig.arb.reserved = 0;
        baudConfig.data.value = dataBitrate;
        baudConfig.data.reserved = 0;
        result = api.CanSetBaudrateX(tempHandle, &baudConfig);
        if (result != RESULT_OK)
            throw std::runtime_error("Set baudrateX(" + std::to_string(arbBitrate) +
                                     "/" + std::to_string(dataBitrate) +
                                     ") failed(" + EsdApi::ResultText(result) + ")");

        // ntcan 默认关闭所有 ID,需显式使能接收范围
        EnableAllIdRegions(tempHandle);

        // 所有操作成功后,才提交状态
        handle_ = tempHandle;
        isOpen_ = true;
        stop_ = false;
        readThread_ = std::thread(&EsdCanFdWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
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

bool EsdCanFdWrapper::Close() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    // 在锁外部等待读线程退出(canReadX 最坏 200ms 超时后退出),避免死锁
    if (readThread_.joinable()) readThread_.join();

    std::lock_guard<std::mutex> lock(lock_);
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

void EsdCanFdWrapper::FlushReceiveBuffer() {
    // ntcan 子集无队列清空接口,空实现(与 .NET 一致)
}

void EsdCanFdWrapper::FlushTransmitBuffer() {
    // ntcan 子集无队列清空接口,空实现(与 .NET 一致)
}

void EsdCanFdWrapper::ReadTaskProc() {
    while (!stop_) {
        try {
            // canReadX 阻塞至多有帧或 200ms 超时,无需额外延时
            ReadBlocking();
        } catch (...) {
            if (!stop_) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void EsdCanFdWrapper::ReadBlocking() {
    if (!isOpen_) return;
    void* handle = handle_.load();
    if (handle == nullptr) return;

    EsdCanFdMsg msg{};
    int32_t len = 1;
    const EsdCanResult result = EsdApi::Instance().CanReadX(handle, &msg, &len);
    if (result != RESULT_OK || len <= 0) return;
    if (!isOpen_) return;

    const uint8_t rawLen = msg.base.len;
    const bool isFd = (rawLen & LEN_FLAG_FD) != 0;
    // DLC 编码换算为实际数据长度,并钳制到 CANFD 有效范围
    const int dlc = std::min<int>(
        DataLenConverter::DlcToLength(rawLen & LEN_MASK_DLC),
        DataLenConverter::MaxCanFdDataLength);

    auto frame = std::make_shared<CANFrame>();
    frame->id = EsdApi::BuildManagedId(msg.base.id, rawLen);
    frame->dlc = dlc;
    frame->dir = 1;
    frame->flag = rawLen;
    frame->flags =
        EsdCanFlagParser::Parse(msg.base.id, rawLen, msg.base.esi, msg.base.msgLost);
    frame->fdf = isFd;
    frame->brs = isFd && (rawLen & LEN_FLAG_FD_NO_BRS) == 0;
    // ntcan 时间戳单位为 µs,直接使用
    frame->timestamp = msg.timestamp;
    frame->data.assign(msg.data, msg.data + dlc);

    if (eventHandler) eventHandler(*frame);
}

bool EsdCanFdWrapper::Send(FrameBase& frameBase) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || handle_.load() == nullptr) return false;

    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    // 发送前校验数据长度合法性(非法参数直接抛异常,不做静默失败)
    if (canFrame->brs && !canFrame->fdf)
        throw std::invalid_argument(
            "BRS is only valid for CANFD frame(FDF must be set).");
    if (canFrame->fdf && CanFrameHelper::IsRTR(canFrame->id))
        throw std::invalid_argument("CANFD frame does not support RTR.");
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

    try {
        auto& api = EsdApi::Instance();

        canFrame->dir = 0;
        const bool isRtr = CanFrameHelper::IsRTR(canFrame->id);

        EsdCanFdMsg msg{};
        msg.base.id = EsdApi::BuildNtcanId(canFrame->id);
        // bit0-3=DLC 编码;FD 帧置 bit7,不加速置 bit4(NO_BRS);经典帧 RTR 置 bit4
        uint8_t lenByte = static_cast<uint8_t>(DataLenConverter::LengthToDlc(canFrame->dlc));
        if (canFrame->fdf) {
            lenByte |= LEN_FLAG_FD;
            if (!canFrame->brs) lenByte |= LEN_FLAG_FD_NO_BRS;
        } else if (isRtr) {
            lenByte |= LEN_FLAG_RTR;
        }
        msg.base.len = lenByte;
        std::memcpy(msg.data, canFrame->data.data(), canFrame->dlc);

        // FD 通道统一走 canWriteX:经典帧即不带 FD 位的 X 消息
        int32_t len = 1;
        const EsdCanResult result = api.CanWriteX(handle_.load(), &msg, &len);
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
