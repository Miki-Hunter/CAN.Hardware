#include "EtasCanFdWrapper.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "EtasFlagParser.h"

namespace canhw {
namespace etas {

std::string EtasCanFdWrapper::ResolveUri() {
    std::string uri = config_.interfaceUri;
    if (uri.empty() || uri.find_first_not_of(" \t\r\n") == std::string::npos) {
        const auto nodes = EtasBoaApi::Instance().EnumerateNodes("CAN:");
        if (nodes.empty())
            throw std::runtime_error("未发现任何 ETAS CAN 硬件节点");
        if (config_.channelIndex < 0 ||
            config_.channelIndex >= static_cast<int32_t>(nodes.size()))
            throw std::runtime_error(
                "通道索引 " + std::to_string(config_.channelIndex) +
                " 超出可用 CAN 节点数(" + std::to_string(nodes.size()) + ")");
        uri = nodes[static_cast<size_t>(config_.channelIndex)];
    }
    const std::string prefix = URI_PREFIX;
    return uri.rfind(prefix, 0) == 0 ? uri : prefix + uri;
}

bool EtasCanFdWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    auto& api = EtasBoaApi::Instance();
    int32_t ctrl = UNDEFINED;

    try {
        api.EnsureLoaded();

        const std::string uri = ResolveUri();

        // 创建控制器(内部自行 CSI 搜索),API 版本固定协商 1.3
        EtasVersion version{1, 3, 0, 0};
        int32_t created = UNDEFINED;
        EtasErrorCode status =
            api.CreateCANControllerVersion(uri.c_str(), &version, &created);
        if (status != CODE_SUCCESS)
            throw std::runtime_error("Create CAN controller failed(" + uri + ", " +
                                     EtasBoaApi::StatusText(status) + ")");
        ctrl = created;

        // 配置并打开控制器(CAN FD:canFDEnabled=1 + 数据段波特率);
        // 仅把带 ERROR 位的返回值当失败,WARNING 放行
        EtasCanConfiguration cfg =
            config_.enableCustomBitrate
                ? EtasCanConfiguration::Create(
                      config_.customArbitrationBitrate, config_.customDataBitrate,
                      config_.arbitrationSamplePoint, config_.arbitrationBtlCycles,
                      config_.arbitrationSjw, config_.dataSamplePoint,
                      config_.dataBtlCycles, config_.dataSjw)
                : EtasCanConfiguration::Create(
                      static_cast<uint32_t>(config_.canFdArbitrationBitrate),
                      static_cast<uint32_t>(config_.canFdDataBitrate));
        EtasControllerProperties props{};
        props.mode = 0;
        status = api.OpenCANController(ctrl, &cfg, &props);
        if (IsError(status))
            throw std::runtime_error("Open CAN controller failed(" + uri + ", " +
                                     EtasBoaApi::StatusText(status) + ")");

        // 查询硬件定时器频率供报文时间戳换算
        tickFrequency_ = api.QueryTickFrequency(ctrl);

        // 创建发送队列
        EtasTxQueueConfiguration txCfg{};
        int32_t tx = UNDEFINED;
        status = api.CreateCANTxQueue(ctrl, &txCfg, &tx);
        if (status != CODE_SUCCESS)
            throw std::runtime_error("Create CAN TxQueue failed(" +
                                     EtasBoaApi::StatusText(status) + ")");

        // 创建接收队列(回调驱动,userData 传本实例)
        EtasRxQueueConfiguration rxCfg{};
        rxCfg.onFrame.functionEx = &EtasCanFdWrapper::RxThunk;
        rxCfg.onFrame.userData = this;
        rxCfg.onEvent = EtasRxCallbackSingleMsg{nullptr, nullptr};
        rxCfg.selfReceptionMode = 0;
        int32_t rx = UNDEFINED;
        status = api.CreateCANRxQueue(ctrl, &rxCfg, &rx);
        if (status != CODE_SUCCESS)
            throw std::runtime_error("Create CAN RxQueue failed(" +
                                     EtasBoaApi::StatusText(status) + ")");

        // 全 0 过滤器 = 放行所有帧
        EtasRxFilter filter{};
        status = api.AddCANFrameFilter(rx, &filter, 1);
        if (status != CODE_SUCCESS)
            throw std::runtime_error("Add CAN frame filter failed(" +
                                     EtasBoaApi::StatusText(status) + ")");

        ctrlHandle_ = ctrl;
        txHandle_ = tx;
        rxHandle_ = rx;
        isOpen_ = true;
        return true;
    } catch (const std::exception& ex) {
        if (ctrl != UNDEFINED) {
            try {
                api.DestroyCANController(ctrl);
            } catch (...) {
            }
        }
        ctrlHandle_ = txHandle_ = rxHandle_ = UNDEFINED;
        tickFrequency_ = 0;
        isOpen_ = false;
        throw std::runtime_error(std::string("Open:") + ex.what());
    }
}

bool EtasCanFdWrapper::Close() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_) return true;

    isOpen_ = false;

    bool ok = true;
    auto& api = EtasBoaApi::Instance();
    if (ctrlHandle_ != UNDEFINED) {
        try {
            api.CloseCANController(ctrlHandle_);
        } catch (...) {
        }
        try {
            ok = !IsError(api.DestroyCANController(ctrlHandle_));
        } catch (...) {
            ok = false;
        }
        if (ok) ctrlHandle_ = txHandle_ = rxHandle_ = UNDEFINED;
    }
    return ok;
}

void EtasCanFdWrapper::FlushReceiveBuffer() {
    // 当前实现的 OCI 子集无队列清空接口,留空(与 .NET 一致)
}

void EtasCanFdWrapper::FlushTransmitBuffer() {
    // 当前实现的 OCI 子集无队列清空接口,留空(与 .NET 一致)
}

void EtasCanFdWrapper::RxThunk(void* userData, void* msg) {
    try {
        if (userData != nullptr)
            static_cast<EtasCanFdWrapper*>(userData)->OnRxMsg(msg);
    } catch (...) {
    }
}

void EtasCanFdWrapper::OnRxMsg(void* msg) {
    if (!isOpen_ || msg == nullptr) return;

    try {
        const uint32_t msgType = *reinterpret_cast<const uint32_t*>(msg);
        std::shared_ptr<CANFrame> frame;
        switch (msgType) {
            case MSG_TYPE_RX_MESSAGE:
                frame = Convert(*reinterpret_cast<const EtasCanRxMsg*>(msg));
                break;
            case MSG_TYPE_FDRX_MESSAGE:
                frame = Convert(*reinterpret_cast<const EtasCanFdRxMsg*>(msg));
                break;
            default:
                return;
        }
        if (frame && eventHandler) eventHandler(*frame);
    } catch (...) {
    }
}

std::shared_ptr<CANFrame> EtasCanFdWrapper::Convert(const EtasCanRxMsg& rx) const {
    auto frame = std::make_shared<CANFrame>();
    frame->id = BuildId(rx.frameID, rx.flags);
    frame->dlc = std::min<int>(rx.dlc, DataLenConverter::MaxCanDataLength);
    frame->flag = rx.flags;
    frame->flags = EtasCanFlagParser::Parse(rx.flags);
    frame->timestamp = TicksToMicros(rx.timeStamp);
    frame->dir = 1;
    frame->data.assign(rx.data, rx.data + frame->dlc);
    return frame;
}

std::shared_ptr<CANFrame> EtasCanFdWrapper::Convert(const EtasCanFdRxMsg& rx) const {
    auto frame = std::make_shared<CANFrame>();
    frame->id = BuildId(rx.frameID, rx.flags);
    // 钳制负载长度到 CANFD 有效范围
    frame->dlc = std::min<int>(rx.dlc, DataLenConverter::MaxCanFdDataLength);
    frame->flag = rx.flags;
    frame->flags = EtasCanFlagParser::Parse(rx.flags);
    frame->fdf = (rx.flags & MSG_FLAG_FD_DATA) != 0;
    frame->brs = (rx.flags & MSG_FLAG_FD_DATA_BIT_RATE) != 0;
    frame->timestamp = TicksToMicros(rx.timeStamp);
    frame->dir = 1;
    frame->data.assign(rx.data, rx.data + frame->dlc);
    return frame;
}

uint64_t EtasCanFdWrapper::TicksToMicros(int64_t ticks) const {
    if (tickFrequency_ == 0 || ticks <= 0) return 0;
    const uint64_t t = static_cast<uint64_t>(ticks);
    const uint64_t f = tickFrequency_;
    return t / f * 1000000ULL + t % f * 1000000ULL / f;
}

int64_t EtasCanFdWrapper::BuildId(uint32_t frameID, uint16_t flags) {
    int64_t id = frameID;
    if ((flags & MSG_FLAG_EXTENDED) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
    if ((flags & MSG_FLAG_REMOTE_FRAME) != 0) id |= CanFrameDef::CAN_RTR_FLAG;
    return id;
}

bool EtasCanFdWrapper::Send(FrameBase& frameBase) {
    auto* canFrame = dynamic_cast<CANFrame*>(&frameBase);
    if (canFrame == nullptr) return false;

    // 发送前校验数据长度合法性(非法参数直接抛异常,不做静默失败)
    if (canFrame->brs && !canFrame->fdf)
        throw std::invalid_argument("BRS requires FDF(CANFD frame).");
    if (canFrame->fdf && CanFrameHelper::IsRTR(canFrame->id))
        throw std::invalid_argument("CANFD frame does not support RTR.");
    if (canFrame->fdf) {
        if (!DataLenConverter::IsValidCanFdLength(canFrame->dlc))
            throw std::invalid_argument(
                "Invalid CANFD data length(" + std::to_string(canFrame->dlc) +
                "), valid lengths: 0-8,12,16,20,24,32,48,64.");
    } else if (!DataLenConverter::IsValidCanLength(canFrame->dlc)) {
        throw std::invalid_argument("Invalid CAN DLC(" +
                                    std::to_string(canFrame->dlc) +
                                    "), valid range: 0-" +
                                    std::to_string(DataLenConverter::MaxCanDataLength) +
                                    ".");
    }
    if (static_cast<int>(canFrame->data.size()) < canFrame->dlc)
        throw std::invalid_argument("Data length(" +
                                    std::to_string(canFrame->data.size()) +
                                    ") is less than DLC(" +
                                    std::to_string(canFrame->dlc) + ").");

    uint16_t flags = 0;
    bool sent = false;
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_ || txHandle_ == UNDEFINED) return false;

        try {
            canFrame->dir = 0;
            const uint32_t frameID = CanFrameHelper::GetId(canFrame->id);
            flags = 0;
            if (CanFrameHelper::IsEFF(canFrame->id)) flags |= MSG_FLAG_EXTENDED;

            // 按帧形态选消息结构(均带 type+reserved 头):
            // FD 帧用 FDTX_MESSAGE,经典帧即使在 FD 通道也用 TX_MESSAGE
            if (canFrame->fdf) {
                flags |= MSG_FLAG_FD_DATA;
                if (canFrame->brs) flags |= MSG_FLAG_FD_DATA_BIT_RATE;
                EtasCanFdTxMsg fdMsg{};
                fdMsg.type = MSG_TYPE_FDTX_MESSAGE;
                fdMsg.frameID = frameID;
                fdMsg.flags = flags;
                fdMsg.size = static_cast<uint8_t>(canFrame->dlc);
                std::memcpy(fdMsg.data, canFrame->data.data(), canFrame->dlc);

                void* ptr = &fdMsg;
                uint32_t remaining = 1;
                const EtasErrorCode status = EtasBoaApi::Instance().WriteCANDataEx(
                    txHandle_, 0, &ptr, 1, &remaining);
                sent = status == CODE_SUCCESS && remaining == 0;
            } else {
                if (CanFrameHelper::IsRTR(canFrame->id)) flags |= MSG_FLAG_REMOTE_FRAME;
                EtasCanTxMsg msg{};
                msg.type = MSG_TYPE_TX_MESSAGE;
                msg.frameID = frameID;
                msg.flags = flags;
                msg.dlc = static_cast<uint8_t>(canFrame->dlc);
                std::memcpy(msg.data, canFrame->data.data(), canFrame->dlc);

                void* ptr = &msg;
                uint32_t remaining = 1;
                const EtasErrorCode status = EtasBoaApi::Instance().WriteCANDataEx(
                    txHandle_, 0, &ptr, 1, &remaining);
                sent = status == CODE_SUCCESS && remaining == 0;
            }
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("Send:") + ex.what());
        }
    }

    if (!sent) return false;

    // 锁外推送发送回显(与 .NET 一致)
    canFrame->flag = flags;
    canFrame->flags = EtasCanFlagParser::Parse(flags);
    if (eventHandler) eventHandler(*canFrame);
    return true;
}

} // namespace etas
} // namespace canhw
