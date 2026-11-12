#include "EbElCanWrapper.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "EbElFlagParser.h"

namespace canhw {
namespace ebel {

using namespace std::chrono_literals;

FcCANCcConfig EbElCanWrapper::BuildCcConfig(EbElCanBitrate bitrate) {
    // 按波特率枚举组位时序(参考实现同款查表)。
    // 除 800K 外各档均满足 12MHz / (Prescaler+1) / (1+TS1+TS2=6) = bps;
    // 800K 行使用特殊时序(SJW=0,TS1=1,TS2=0),与上式不一致,原样保留参考值。
    FcCANCcConfig cfg{};
    cfg.synchronizationJumpWidth = 1;
    cfg.timeSegment1 = 4;
    cfg.timeSegment2 = 1;
    // 参考实现遗留默认值 0(关闭自动重发=变相单次发送),这里修正为使能
    cfg.enableAutomaticRetransmission = 1;

    switch (bitrate) {
        case EbElCanBitrate::fcBAUD_1M:   cfg.baudRatePrescaler = 1; break;
        case EbElCanBitrate::fcBAUD_500K: cfg.baudRatePrescaler = 3; break;
        case EbElCanBitrate::fcBAUD_250K: cfg.baudRatePrescaler = 7; break;
        case EbElCanBitrate::fcBAUD_125K: cfg.baudRatePrescaler = 15; break;
        case EbElCanBitrate::fcBAUD_100K: cfg.baudRatePrescaler = 19; break;
        case EbElCanBitrate::fcBAUD_50K:  cfg.baudRatePrescaler = 39; break;
        case EbElCanBitrate::fcBAUD_20K:  cfg.baudRatePrescaler = 99; break;
        case EbElCanBitrate::fcBAUD_10K:  cfg.baudRatePrescaler = 199; break;
        case EbElCanBitrate::fcBAUD_800K: cfg.baudRatePrescaler = 4; break;
        default:
            throw std::invalid_argument("Unsupported bitrate.");
    }
    if (bitrate == EbElCanBitrate::fcBAUD_800K) {
        cfg.synchronizationJumpWidth = 0;
        cfg.timeSegment1 = 1;
        cfg.timeSegment2 = 0;
    }
    return cfg;
}

uint32_t EbElCanWrapper::BuildFlexCardId(int64_t id) {
    uint32_t result = CanFrameHelper::GetId(id);
    if (CanFrameHelper::IsEFF(id)) result |= FC_EXT_FLAG;
    return result;
}

int64_t EbElCanWrapper::BuildManagedId(uint32_t fcId, uint8_t flagsAndDlc) {
    int64_t id = fcId & CanFrameDef::CAN_ID_FLAG;
    if ((fcId & FC_EXT_FLAG) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
    if ((flagsAndDlc & DlcFlags::RemoteFrame) != 0) id |= CanFrameDef::CAN_RTR_FLAG;
    return id;
}

bool EbElCanWrapper::Open() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isOpen_) return true;

    void* tempHandle = nullptr;
    auto& api = EbElApi::Instance();

    try {
        api.EnsureLoaded();

        if (config_.channel > static_cast<uint32_t>(CC8))
            throw std::invalid_argument(
                "Invalid channel(" + std::to_string(config_.channel) +
                "), valid range: 0-7(CC1~CC8).");
        cc_ = static_cast<int32_t>(config_.channel);

        // 枚举第一块带 CAN 控制器的 FlexCard
        const uint32_t flexCardId = api.FindFirstCanCard();
        if (flexCardId == 0)
            throw std::runtime_error(
                "No FlexCard with CAN controller found,请确认已安装 FlexCard 驱动并连接设备");

        FcResult result = api.Open(flexCardId, &tempHandle);
        if (result != FC_OK || tempHandle == nullptr) {
            tempHandle = nullptr;
            throw std::runtime_error("fcbOpen failed(" + EbElApi::ResultText(result) +
                                     ")");
        }

        // 波特率:fcbase 无数值接口,按枚举查位时序表组 fcCANCcConfig
        const FcCANCcConfig ccConfig = BuildCcConfig(config_.canBitrate);
        result = api.SetCcConfiguration(tempHandle, cc_, ccConfig);
        if (result != FC_OK)
            throw std::runtime_error("fcbCANSetCcConfiguration failed(" +
                                     EbElApi::ResultText(result) + ")");

        // 启动监控(resetTimestamps=1, Normal 模式);此后 fcbReceive 才有报文
        result = api.MonitoringStart(tempHandle, cc_);
        if (result != FC_OK)
            throw std::runtime_error("fcbCANMonitoringStart failed(" +
                                     EbElApi::ResultText(result) + ")");

        // 所有操作成功后,才提交状态
        handle_ = tempHandle;
        isOpen_ = true;
        stop_ = false;
        readThread_ = std::thread(&EbElCanWrapper::ReadTaskProc, this);
        return true;
    } catch (const std::exception& ex) {
        // 清理已打开的资源
        if (tempHandle != nullptr) {
            try {
                api.Close(tempHandle);
            } catch (...) {
            }
        }
        isOpen_ = false;
        handle_ = nullptr;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool EbElCanWrapper::Close() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!isOpen_) return true;
        stop_ = true;
    }

    // 在锁外部等待读线程退出,避免死锁
    if (readThread_.joinable()) readThread_.join();

    std::lock_guard<std::mutex> lock(lock_);
    auto& api = EbElApi::Instance();
    // 先停监控再关句柄
    if (handle_ != nullptr) {
        try {
            api.MonitoringStop(handle_, cc_);
        } catch (...) {
        }
        try {
            api.Close(handle_);
        } catch (...) {
        }
        handle_ = nullptr;
    }
    isOpen_ = false;
    return true;
}

void EbElCanWrapper::FlushReceiveBuffer() {
    std::lock_guard<std::mutex> lock(lock_);
    // fcbase 无专用清空函数,循环取空驱动接收队列即可(取到即弃)
    if (handle_ == nullptr) return;
    auto& api = EbElApi::Instance();
    while (true) {
        void* pPacket = nullptr;
        const FcResult result = api.Receive(handle_, &pPacket);
        if (result != FC_OK || pPacket == nullptr) break;
        api.FreePacket(pPacket);
    }
}

void EbElCanWrapper::FlushTransmitBuffer() {
    // fcbase 发送直接交付报文缓冲,无用户态发送队列可清空(与 .NET 一致)
}

void EbElCanWrapper::ReadTaskProc() {
    auto& api = EbElApi::Instance();
    while (!stop_) {
        try {
            void* handle = handle_;
            if (handle == nullptr) break;

            if (!ReceiveChain(handle)) {
                // 无报文,小睡避免空转
                std::this_thread::sleep_for(RxPollIdleMs * 1ms);
            }
        } catch (...) {
            // 其他异常,稍等后继续
            if (!stop_) std::this_thread::sleep_for(10ms);
        }
    }
}

bool EbElCanWrapper::ReceiveChain(void* handle) {
    auto& api = EbElApi::Instance();
    void* pPacket = nullptr;
    const FcResult result = api.Receive(handle, &pPacket);
    if (result != FC_OK || pPacket == nullptr) return false;

    // 关键纪律:先在归还之前把整条链表遍历处理完,再 FreePacket 归还
    // (对应 .NET 的 try/finally;参考实现曾"先释放后访问",接收路径从未真正工作)
    try {
        auto* pCurrent = static_cast<FcPacket*>(pPacket);
        while (pCurrent != nullptr) {
            // 逐节点处理
            if (pCurrent->type == PKT_CAN && pCurrent->pCANPacket != nullptr) {
                ProcessCanPacket(*static_cast<FcCanPacket*>(pCurrent->pCANPacket));
            }
            pCurrent = static_cast<FcPacket*>(pCurrent->pNextPacket);
        }
    } catch (...) {
        api.FreePacket(pPacket);
        throw;
    }
    api.FreePacket(pPacket);
    return true;
}

void EbElCanWrapper::ProcessCanPacket(const FcCanPacket& canPacket) {
    // 过滤:非本通道 / 发送确认(由 Send 成功后自行推送)
    if (canPacket.cc != cc_) return;
    const uint8_t flagsAndDlc = canPacket.flagsAndDlc;
    if ((flagsAndDlc & DlcFlags::Direction) != 0) return;

    // bit0-3 直接是字节数(0..8),钳制防御
    const int length =
        std::min<int>(flagsAndDlc & 0x0F, DataLenConverter::MaxCanDataLength);

    auto frame = std::make_shared<CANFrame>();
    frame->id = BuildManagedId(canPacket.id, flagsAndDlc);
    frame->dlc = length;
    frame->flag = flagsAndDlc;
    frame->flags = EbElCanFlagParser::Parse(canPacket.id, flagsAndDlc);
    // 时间戳单位未公开,原始 tick 透传
    frame->timestamp = canPacket.timeStamp;
    frame->data.assign(canPacket.data, canPacket.data + length);
    frame->dir = 1;

    if (eventHandler) eventHandler(*frame);
}

bool EbElCanWrapper::Send(FrameBase& frameBase) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!isOpen_ || handle_ == nullptr) return false;

    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    // 发送前校验数据长度合法性(非法参数直接抛异常,不做静默失败)
    if (canFrame->fdf || canFrame->brs)
        throw std::invalid_argument(
            "EbEl(FlexCard) channel does not support CANFD frame(FDF/BRS).");
    if (CanFrameHelper::IsRTR(canFrame->id))
        throw std::invalid_argument(
            "EbEl(FlexCard) buffer-based transmit does not support RTR.");
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
        auto& api = EbElApi::Instance();

        canFrame->dir = 0;

        // 缓冲式发送:fcbCANTransmit 只收缓冲号 + 数据,不带 CAN ID
        // —— 报文的 ID/DLC 由缓冲 0 的既有配置决定,canFrame->id 无法生效
        const FcResult result =
            api.Transmit(handle_, cc_, 0, canFrame->data.data(),
                         static_cast<uint8_t>(canFrame->dlc));
        if (result != FC_OK) return false;

        // 发送成功后回显(发送侧不还原缓冲 ID,原样回显上层给的帧)
        const uint32_t rawId = BuildFlexCardId(canFrame->id);
        canFrame->flag = DlcFlags::Direction;
        canFrame->flags = EbElCanFlagParser::Parse(rawId, DlcFlags::Direction);
        if (eventHandler) eventHandler(*canFrame);
        return true;
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Send:") + ex.what());
    }
}

} // namespace ebel
} // namespace canhw
