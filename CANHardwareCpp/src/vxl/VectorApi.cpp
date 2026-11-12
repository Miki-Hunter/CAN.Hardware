#include "VectorApi.h"

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace canhw {
namespace vxl {

namespace {

// 通道名解码(ASCII,遇 0 截断)
std::string DecodeChannelName(const uint8_t* raw, size_t len) {
    size_t n = 0;
    while (n < len && raw[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(raw), n);
}

// busParams.data.can 布局: bitRate@0, sjw@4, tseg1@5, tseg2@6, sam@7,
// outputMode@8, canOpMode@16
XLcanBusParamsInfo ParseBusParams(const XLchannelConfig& c) {
    XLcanBusParamsInfo info;
    info.channelName = DecodeChannelName(c.name, sizeof(c.name));
    std::memcpy(&info.bitRate, &c.busParams[0], 4);
    info.sjw = c.busParams[4];
    info.tseg1 = c.busParams[5];
    info.tseg2 = c.busParams[6];
    info.sam = c.busParams[7];
    info.outputMode = c.busParams[8];
    info.canOpMode = c.busParams[16];
    return info;
}

// CAN FD 仲裁段时序表:baudrate -> {tseg1, tseg2, sjw}(参考工程参数,80MHz 时钟)
const std::map<uint32_t, std::array<uint32_t, 3>>& FdNominalTiming() {
    static const std::map<uint32_t, std::array<uint32_t, 3>> table = {
        {10000, {16, 248, 1}},    {20000, {8, 248, 1}},     {50000, {4, 198, 1}},
        {100000, {2, 198, 1}},    {125000, {2, 158, 1}},    {250000, {10, 11, 1}},
        {500000, {119, 40, 40}},  {800000, {1, 48, 1}},     {1000000, {59, 20, 20}},
    };
    return table;
}

// CAN FD 数据段时序表:baudrate -> {tseg1, tseg2, sjw}
const std::map<uint32_t, std::array<uint32_t, 3>>& FdDataTiming() {
    static const std::map<uint32_t, std::array<uint32_t, 3>> table = {
        {500000, {119, 40, 1}},  {1000000, {59, 20, 20}},  {2000000, {29, 10, 10}},
        {4000000, {14, 5, 5}},   {5000000, {11, 4, 4}},    {8000000, {5, 3, 3}},
    };
    return table;
}

} // namespace

VectorApi& VectorApi::Instance() {
    static VectorApi api;
    return api;
}

bool VectorApi::Load() {
    if (loaded_) return true;

    std::lock_guard<std::mutex> lock(initLock_);
    if (loaded_) return true;

    if (!lib_.Load(DefaultDllPath())) return false;

    pOpenDriver_ = reinterpret_cast<XlStatusFn>(lib_.GetSymbol("xlOpenDriver"));
    pCloseDriver_ = reinterpret_cast<XlStatusFn>(lib_.GetSymbol("xlCloseDriver"));
    pGetDriverConfig_ =
        reinterpret_cast<GetDriverConfigFn>(lib_.GetSymbol("xlGetDriverConfig"));
    pSetApplConfig_ =
        reinterpret_cast<SetApplConfigFn>(lib_.GetSymbol("xlSetApplConfig"));
    pOpenPort_ = reinterpret_cast<OpenPortFn>(lib_.GetSymbol("xlOpenPort"));
    pClosePort_ = reinterpret_cast<ClosePortFn>(lib_.GetSymbol("xlClosePort"));
    pActivateChannel_ =
        reinterpret_cast<ActivateFn>(lib_.GetSymbol("xlActivateChannel"));
    pDeactivateChannel_ =
        reinterpret_cast<DeactivateFn>(lib_.GetSymbol("xlDeactivateChannel"));
    pCanSetChannelBitrate_ =
        reinterpret_cast<SetBitrateFn>(lib_.GetSymbol("xlCanSetChannelBitrate"));
    pCanSetChannelParams_ =
        reinterpret_cast<SetParamsFn>(lib_.GetSymbol("xlCanSetChannelParams"));
    pCanSetAcceptanceFilter_ =
        reinterpret_cast<SetFilterFn>(lib_.GetSymbol("xlCanSetAcceptanceFilter"));
    pCanSetChannelMode_ =
        reinterpret_cast<SetModeFn>(lib_.GetSymbol("xlCanSetChannelMode"));
    pCanFdSetConfiguration_ =
        reinterpret_cast<FdSetConfFn>(lib_.GetSymbol("xlCanFdSetConfiguration"));
    pReceive_ = reinterpret_cast<ReceiveFn>(lib_.GetSymbol("xlReceive"));
    pCanReceive_ = reinterpret_cast<CanReceiveFn>(lib_.GetSymbol("xlCanReceive"));
    pCanTransmit_ = reinterpret_cast<TransmitFn>(lib_.GetSymbol("xlCanTransmit"));
    pCanTransmitEx_ = reinterpret_cast<TransmitExFn>(lib_.GetSymbol("xlCanTransmitEx"));
    pFlushReceiveQueue_ =
        reinterpret_cast<FlushFn>(lib_.GetSymbol("xlFlushReceiveQueue"));
    pCanFlushTransmitQueue_ = reinterpret_cast<FlushTxFn>(
        lib_.GetSymbol("xlCanFlushTransmitQueue"));
    pCanRequestChipState_ =
        reinterpret_cast<ChipStateFn>(lib_.GetSymbol("xlCanRequestChipState"));

    const bool ok = pOpenDriver_ && pCloseDriver_ && pGetDriverConfig_ &&
                    pSetApplConfig_ && pOpenPort_ && pClosePort_ &&
                    pActivateChannel_ && pDeactivateChannel_ &&
                    pCanSetChannelBitrate_ && pCanSetChannelParams_ &&
                    pCanSetAcceptanceFilter_ && pCanSetChannelMode_ &&
                    pCanFdSetConfiguration_ && pReceive_ && pCanReceive_ &&
                    pCanTransmit_ && pCanTransmitEx_ && pFlushReceiveQueue_ &&
                    pCanFlushTransmitQueue_ && pCanRequestChipState_;
    if (!ok) {
        lib_.Unload();
        return false;
    }
    loaded_ = true;
    return true;
}

void VectorApi::EnsureLoaded() {
    if (loaded_) return;
    if (!Load()) {
        throw std::runtime_error(
            std::string("Failed to load ") + DefaultDllPath() +
            " (WinError=" + std::to_string(DynamicLibrary::LastError()) +
            ",请安装 Vector XL Driver)");
    }
}

int16_t VectorApi::OpenDriver() {
    EnsureLoaded();
    std::lock_guard<std::mutex> lock(initLock_);
    int16_t status = SUCCESS;
    if (refCount_ == 0) status = pOpenDriver_();
    if (status == SUCCESS) ++refCount_;
    return status;
}

void VectorApi::CloseDriver() {
    if (!loaded_) return;
    std::lock_guard<std::mutex> lock(initLock_);
    if (refCount_ <= 0) return;
    --refCount_;
    if (refCount_ == 0) pCloseDriver_();
}

bool VectorApi::TryGetChannel(VectorHwType hwType, uint32_t hwChannel,
                              XLchannelConfig& channelConfig) {
    EnsureLoaded();
    XLdriverConfig driverConfig{};
    if (pGetDriverConfig_(&driverConfig) != SUCCESS) return false;

    const uint32_t count =
        driverConfig.channelCount < 64 ? driverConfig.channelCount : 64;
    for (uint32_t i = 0; i < count; ++i) {
        const XLchannelConfig& c = driverConfig.channel[i];
        if (c.busType != XL_BUS_TYPE_CAN) continue;
        if (static_cast<VectorHwType>(c.hwType) == hwType &&
            c.hwChannel == static_cast<uint8_t>(hwChannel)) {
            channelConfig = c;
            return true;
        }
    }
    return false;
}

std::string VectorApi::GetCanChannelsText(VectorHwType hwType) {
    std::string text;
    try {
        if (OpenDriver() != SUCCESS) return "(无,请检查驱动安装与硬件连接)";
        XLdriverConfig driverConfig{};
        if (pGetDriverConfig_(&driverConfig) == SUCCESS) {
            const uint32_t count =
                driverConfig.channelCount < 64 ? driverConfig.channelCount : 64;
            for (uint32_t i = 0; i < count; ++i) {
                const XLchannelConfig& c = driverConfig.channel[i];
                if (c.busType != XL_BUS_TYPE_CAN) continue;
                if (static_cast<VectorHwType>(c.hwType) != hwType) continue;
                if (!text.empty()) text += ", ";
                text += DecodeChannelName(c.name, sizeof(c.name)) +
                        "(hwChannel=" + std::to_string(c.hwChannel) + ")";
            }
        }
    } catch (...) {
    }
    CloseDriver();
    if (text.empty()) text = "(无,请检查驱动安装与硬件连接)";
    return text;
}

bool VectorApi::TryReadBusParams(uint64_t channelMask, XLcanBusParamsInfo& info) {
    EnsureLoaded();
    XLdriverConfig driverConfig{};
    if (pGetDriverConfig_(&driverConfig) != SUCCESS) return false;

    const uint32_t count =
        driverConfig.channelCount < 64 ? driverConfig.channelCount : 64;
    for (uint32_t i = 0; i < count; ++i) {
        const XLchannelConfig& c = driverConfig.channel[i];
        if (c.busType != XL_BUS_TYPE_CAN || c.channelMask != channelMask) continue;
        info = ParseBusParams(c);
        return true;
    }
    return false;
}

int16_t VectorApi::SetApplConfig(const char* appName, uint32_t appChannel,
                                 uint64_t hwType, uint32_t hwIndex, uint32_t hwChannel,
                                 uint32_t busType) {
    EnsureLoaded();
    return pSetApplConfig_(appName, appChannel, hwType, hwIndex, hwChannel, busType);
}

int16_t VectorApi::OpenPort(int32_t* portHandle, const char* userName,
                            uint64_t accessMask, uint64_t* permissionMask,
                            uint32_t rxQueueSize, uint32_t interfaceVersion,
                            uint32_t busType) {
    EnsureLoaded();
    return pOpenPort_(portHandle, userName, accessMask, permissionMask, rxQueueSize,
                      interfaceVersion, busType);
}

int16_t VectorApi::ClosePort(int32_t portHandle) {
    EnsureLoaded();
    return pClosePort_(portHandle);
}

int16_t VectorApi::ActivateChannel(int32_t portHandle, uint64_t accessMask,
                                   uint32_t busType, uint32_t flags) {
    EnsureLoaded();
    return pActivateChannel_(portHandle, accessMask, busType, flags);
}

int16_t VectorApi::DeactivateChannel(int32_t portHandle, uint64_t accessMask) {
    EnsureLoaded();
    return pDeactivateChannel_(portHandle, accessMask);
}

int16_t VectorApi::CanSetChannelBitrate(int32_t portHandle, uint64_t accessMask,
                                        uint32_t bitrate) {
    EnsureLoaded();
    return pCanSetChannelBitrate_(portHandle, accessMask, bitrate);
}

int16_t VectorApi::CanSetChannelParams(int32_t portHandle, uint64_t accessMask,
                                       const XLchipParams* chipParams) {
    EnsureLoaded();
    return pCanSetChannelParams_(portHandle, accessMask, chipParams);
}

int16_t VectorApi::CanSetAcceptanceFilter(int32_t portHandle, uint64_t accessMask,
                                          uint32_t code, uint32_t mask,
                                          uint32_t idRange) {
    EnsureLoaded();
    return pCanSetAcceptanceFilter_(portHandle, accessMask, code, mask, idRange);
}

int16_t VectorApi::CanSetChannelMode(int32_t portHandle, uint64_t accessMask,
                                     int32_t tx, int32_t txrq) {
    EnsureLoaded();
    return pCanSetChannelMode_(portHandle, accessMask, tx, txrq);
}

int16_t VectorApi::CanFdSetConfiguration(int32_t portHandle, uint64_t accessMask,
                                         const XLcanFdConf* fdConf) {
    EnsureLoaded();
    return pCanFdSetConfiguration_(portHandle, accessMask, fdConf);
}

int16_t VectorApi::Receive(int32_t portHandle, uint32_t* eventCount,
                           XLevent* events) {
    EnsureLoaded();
    return pReceive_(portHandle, eventCount, events);
}

int16_t VectorApi::CanReceive(int32_t portHandle, XLcanRxEvent* rxEvent) {
    EnsureLoaded();
    return pCanReceive_(portHandle, rxEvent);
}

int16_t VectorApi::CanTransmit(int32_t portHandle, uint64_t accessMask,
                               uint32_t* messageCount, XLevent* messages) {
    EnsureLoaded();
    return pCanTransmit_(portHandle, accessMask, messageCount, messages);
}

int16_t VectorApi::CanTransmitEx(int32_t portHandle, uint64_t accessMask,
                                 uint32_t msgCnt, uint32_t* msgCntSent,
                                 XLcanTxEvent* messages) {
    EnsureLoaded();
    return pCanTransmitEx_(portHandle, accessMask, msgCnt, msgCntSent, messages);
}

int16_t VectorApi::FlushReceiveQueue(int32_t portHandle) {
    EnsureLoaded();
    return pFlushReceiveQueue_(portHandle);
}

int16_t VectorApi::CanFlushTransmitQueue(int32_t portHandle, uint64_t accessMask) {
    EnsureLoaded();
    return pCanFlushTransmitQueue_(portHandle, accessMask);
}

int16_t VectorApi::CanRequestChipState(int32_t portHandle, uint64_t accessMask) {
    EnsureLoaded();
    return pCanRequestChipState_(portHandle, accessMask);
}

XLcanFdConf VectorApi::BuildFdConf(uint32_t arbitrationBitrate, uint32_t dataBitrate,
                                   bool nonIso) {
    const auto& nomTable = FdNominalTiming();
    const auto& datTable = FdDataTiming();
    const auto nomIt = nomTable.find(arbitrationBitrate);
    if (nomIt == nomTable.end())
        throw std::invalid_argument("Unsupported arbitration bitrate: " +
                                    std::to_string(arbitrationBitrate));
    const auto datIt = datTable.find(dataBitrate);
    if (datIt == datTable.end())
        throw std::invalid_argument("Unsupported data bitrate: " +
                                    std::to_string(dataBitrate));

    XLcanFdConf conf{};
    conf.arbitrationBitRate = arbitrationBitrate;
    conf.tseg1Abr = nomIt->second[0];
    conf.tseg2Abr = nomIt->second[1];
    conf.sjwAbr = nomIt->second[2];
    conf.dataBitRate = dataBitrate;
    conf.tseg1Dbr = datIt->second[0];
    conf.tseg2Dbr = datIt->second[1];
    conf.sjwDbr = datIt->second[2];
    conf.options = nonIso ? 0x08 : 0;
    return conf;
}

XLcanFdConf VectorApi::BuildFdConfCustom(uint32_t arbitrationBitrate, int arbitrationTseg1,
                                         int arbitrationTseg2, int arbitrationSjw,
                                         uint32_t dataBitrate, int dataTseg1,
                                         int dataTseg2, int dataSjw, bool nonIso) {
    XLcanFdConf conf{};
    conf.arbitrationBitRate = arbitrationBitrate;
    conf.tseg1Abr = static_cast<uint32_t>(arbitrationTseg1);
    conf.tseg2Abr = static_cast<uint32_t>(arbitrationTseg2);
    conf.sjwAbr = static_cast<uint32_t>(arbitrationSjw);
    conf.dataBitRate = dataBitrate;
    conf.tseg1Dbr = static_cast<uint32_t>(dataTseg1);
    conf.tseg2Dbr = static_cast<uint32_t>(dataTseg2);
    conf.sjwDbr = static_cast<uint32_t>(dataSjw);
    conf.options = nonIso ? 0x08 : 0;
    return conf;
}

std::string VectorApi::StatusText(int16_t status) {
    switch (status) {
        case SUCCESS: return "SUCCESS";
        case ERR_QUEUE_IS_EMPTY: return "ERR_QUEUE_IS_EMPTY";
        case ERR_INIT_ACCESS_MISSING:
            return "ERR_INIT_ACCESS_MISSING(缺少初始化权限,请先在 Vector "
                   "Hardware Config 中为该应用分配通道)";
        case ERR_CANNOT_OPEN_DRIVER:
            return "ERR_CANNOT_OPEN_DRIVER(无法打开驱动,请确认已安装 Vector XL Driver)";
        case ERR_DLL_NOT_FOUND:
            return "ERR_DLL_NOT_FOUND(未找到 vxlapi64.dll,请安装 Vector XL Driver "
                   "Library)";
        default: return "XLstatus(" + std::to_string(status) + ")";
    }
}

} // namespace vxl
} // namespace canhw
