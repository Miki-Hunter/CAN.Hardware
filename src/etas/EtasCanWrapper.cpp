#include "EtasCanWrapper.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <canhw/CanFrameHelper.h>
#include <canhw/DataLenConverter.h>

#include "EtasFlagParser.h"

namespace canhw {
namespace etas {

std::string EtasCanWrapper::ResolveUri() {
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

bool EtasCanWrapper::Open() {
    canhw::LockGuard lock(lock_);
    if (isOpen_) return true;

    auto& api = EtasBoaApi::Instance();
    int32_t ctrl = UNDEFINED;

    try {
        // 定位并加载 dll-ocdProxy.dll / dll-csiBind.dll
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

        // 配置并打开控制器(经典 CAN);仅把带 ERROR 位的返回值当失败,
        // WARNING(如 PARAM_ADAPTED 参数被微调)放行
        EtasCanConfiguration cfg =
            config_.enableCustomBitrate
                ? EtasCanConfiguration::Create(
                      config_.customBitrate, 0, config_.samplePoint,
                      config_.btlCycles, config_.sjw)
                : EtasCanConfiguration::Create(
                      static_cast<uint32_t>(config_.canBitrate), 0);
        EtasControllerProperties props{};
        props.mode = 0;
        status = api.OpenCANController(ctrl, &cfg, &props);
        if (IsError(status))
            throw std::runtime_error("Open CAN controller failed(" + uri + ", " +
                                     EtasBoaApi::StatusText(status) + ")");

        // 查询硬件定时器频率供报文时间戳换算;失败降级为无时间戳,不影响打开
        tickFrequency_ = api.QueryTickFrequency(ctrl);

        // 创建发送队列
        EtasTxQueueConfiguration txCfg{};
        int32_t tx = UNDEFINED;
        status = api.CreateCANTxQueue(ctrl, &txCfg, &tx);
        if (status != CODE_SUCCESS)
            throw std::runtime_error("Create CAN TxQueue failed(" +
                                     EtasBoaApi::StatusText(status) + ")");

        // 创建接收队列(回调驱动,userData 传本实例,驱动线程直接推送报文)
        EtasRxQueueConfiguration rxCfg{};
        rxCfg.onFrame.functionEx = &EtasCanWrapper::RxThunk;
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

        // 所有操作成功后,才提交状态
        ctrlHandle_ = ctrl;
        txHandle_ = tx;
        rxHandle_ = rx;
        isOpen_ = true;
        return true;
    } catch (const std::exception& ex) {
        // 控制器已创建则级联销毁(队列随控制器释放)
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

bool EtasCanWrapper::Close() {
    canhw::LockGuard lock(lock_);
    if (!isOpen_) return true;

    // 先置关闭标志,阻止在途回调继续推送报文
    isOpen_ = false;

    bool ok = true;
    auto& api = EtasBoaApi::Instance();
    if (ctrlHandle_ != UNDEFINED) {
        // 先 Close 让控制器下线,再 Destroy 级联释放其下的 Tx/Rx 队列
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
    // 回调 thunk 是静态函数且不持有实例引用之外的状态,无需额外保持
    return ok;
}

void EtasCanWrapper::FlushReceiveBuffer() {
    // 当前实现的 OCI 子集无队列清空接口,留空(与 .NET 一致)
}

void EtasCanWrapper::FlushTransmitBuffer() {
    // 当前实现的 OCI 子集无队列清空接口,留空(与 .NET 一致)
}

void EtasCanWrapper::RxThunk(void* userData, void* msg) {
    // 回调在驱动线程上执行,异常不能抛回 native 侧
    try {
        if (userData != nullptr) static_cast<EtasCanWrapper*>(userData)->OnRxMsg(msg);
    } catch (...) {
    }
}

void EtasCanWrapper::OnRxMsg(void* msg) {
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
                // 错误帧/总线事件/队列事件等暂不处理
                return;
        }
        if (frame && eventHandler) eventHandler(*frame);
    } catch (...) {
        // 回调在驱动线程上执行,异常不能抛回 native 侧
    }
}

std::shared_ptr<CANFrame> EtasCanWrapper::Convert(const EtasCanRxMsg& rx) const {
    auto frame = std::make_shared<CANFrame>();
    frame->id = BuildId(rx.frameID, rx.flags);
    // 钳制 dlc 到经典 CAN 有效范围
    frame->dlc = std::min<int>(rx.dlc, DataLenConverter::MaxCanDataLength);
    frame->flag = rx.flags;
    frame->flags = EtasCanFlagParser::Parse(rx.flags);
    frame->timestamp = TicksToMicros(rx.timeStamp);
    frame->dir = 1;
    frame->data.assign(rx.data, rx.data + frame->dlc);
    return frame;
}

std::shared_ptr<CANFrame> EtasCanWrapper::Convert(const EtasCanFdRxMsg& rx) const {
    auto frame = std::make_shared<CANFrame>();
    frame->id = BuildId(rx.frameID, rx.flags);
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

uint64_t EtasCanWrapper::TicksToMicros(int64_t ticks) const {
    if (tickFrequency_ == 0 || ticks <= 0) return 0;
    const uint64_t t = static_cast<uint64_t>(ticks);
    const uint64_t f = tickFrequency_;
    return t / f * 1000000ULL + t % f * 1000000ULL / f;
}

int64_t EtasCanWrapper::BuildId(uint32_t frameID, uint16_t flags) {
    int64_t id = frameID;
    if ((flags & MSG_FLAG_EXTENDED) != 0) id |= CanFrameDef::CAN_EFF_FLAG;
    if ((flags & MSG_FLAG_REMOTE_FRAME) != 0) id |= CanFrameDef::CAN_RTR_FLAG;
    return id;
}

bool EtasCanWrapper::Send(FrameBase& frameBase) {
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

    uint16_t flags = 0;
    bool sent = false;
    {
        canhw::LockGuard lock(lock_);
        if (!isOpen_ || txHandle_ == UNDEFINED) return false;

        try {
            canFrame->dir = 0;
            EtasCanTxMsg msg{};
            msg.type = MSG_TYPE_TX_MESSAGE;
            msg.reserved = 0;
            msg.frameID = CanFrameHelper::GetId(canFrame->id);
            flags = 0;
            if (CanFrameHelper::IsEFF(canFrame->id)) flags |= MSG_FLAG_EXTENDED;
            if (CanFrameHelper::IsRTR(canFrame->id)) flags |= MSG_FLAG_REMOTE_FRAME;
            msg.flags = flags;
            msg.res = 0;
            msg.dlc = static_cast<uint8_t>(canFrame->dlc);
            std::memcpy(msg.data, canFrame->data.data(), canFrame->dlc);

            // OCI_WriteCANDataEx 收发 OCI_CANMessageEx* 指针数组,这里单帧;
            // timeout=0 立即返回,队列满时由 remaining 回读实际未写入数量
            void* ptr = &msg;
            uint32_t remaining = 1;
            const EtasErrorCode status = EtasBoaApi::Instance().WriteCANDataEx(
                txHandle_, 0, &ptr, 1, &remaining);
            sent = status == CODE_SUCCESS && remaining == 0;
            flags = msg.flags;
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("Send:") + ex.what());
        }
    }

    if (!sent) return false;

    // 锁外推送发送回显,避免订阅者在持锁路径上阻塞或重入(与 .NET 一致)
    canFrame->flag = flags;
    canFrame->flags = EtasCanFlagParser::Parse(flags);
    if (eventHandler) eventHandler(*canFrame);
    return true;
}

} // namespace etas
} // namespace canhw
