#include "EbElApi.h"

#include <cstdio>
#include <stdexcept>

namespace canhw {
namespace ebel {

EbElApi& EbElApi::Instance() {
    static EbElApi api;
    return api;
}

void EbElApi::EnsureLoaded() {
    if (loaded_) return;
    canhw::LockGuard lock(loadLock_);
    if (loaded_) return;

    // 裸 DLL 名,由系统搜索路径解析(与 .NET 一致)
    if (!lib_.Load(DefaultDllPath())) {
        throw std::runtime_error(
            std::string("未检测到 EbEl FlexCard 驱动(未找到 ") + DefaultDllPath() +
            "),请先安装 Eberspächer Electronics FlexCard 驱动(fcbase.dll,"
            "随 FlexCard 硬件发布) (WinError=" +
            std::to_string(DynamicLibrary::LastError()) + ")");
    }

    pGetEnumFlexCardsV3_ = reinterpret_cast<GetEnumFlexCardsV3Fn>(
        lib_.GetSymbol("fcbGetEnumFlexCardsV3"));
    pFreeMemory_ = reinterpret_cast<FreeMemoryFn>(lib_.GetSymbol("fcFreeMemory"));
    pOpen_ = reinterpret_cast<OpenFn>(lib_.GetSymbol("fcbOpen"));
    pClose_ = reinterpret_cast<CloseFn>(lib_.GetSymbol("fcbClose"));
    pMonitoringStart_ =
        reinterpret_cast<MonitoringStartFn>(lib_.GetSymbol("fcbCANMonitoringStart"));
    pMonitoringStop_ =
        reinterpret_cast<MonitoringStopFn>(lib_.GetSymbol("fcbCANMonitoringStop"));
    pTransmit_ = reinterpret_cast<TransmitFn>(lib_.GetSymbol("fcbCANTransmit"));
    pReceive_ = reinterpret_cast<ReceiveFn>(lib_.GetSymbol("fcbReceive"));
    pSetCcConfiguration_ = reinterpret_cast<SetCcConfigurationFn>(
        lib_.GetSymbol("fcbCANSetCcConfiguration"));

    const bool ok = pGetEnumFlexCardsV3_ && pFreeMemory_ && pOpen_ && pClose_ &&
                    pMonitoringStart_ && pMonitoringStop_ && pTransmit_ &&
                    pReceive_ && pSetCcConfiguration_;
    if (!ok) {
        lib_.Unload();
        throw std::runtime_error(
            std::string(DefaultDllPath()) +
            " 导出函数不完整(fcbGetEnumFlexCardsV3/fcFreeMemory/fcbOpen/fcbClose/"
            "fcbCANMonitoringStart/fcbCANMonitoringStop/fcbCANTransmit/fcbReceive/"
            "fcbCANSetCcConfiguration),请确认安装的是完整的 FlexCard 驱动");
    }
    loaded_ = true;
}

uint32_t EbElApi::FindFirstCanCard() {
    EnsureLoaded();
    void* pInfoHwSw = nullptr;
    uint32_t found = 0;
    try {
        const FcResult result = pGetEnumFlexCardsV3_(&pInfoHwSw, 1);
        if (result == FC_OK && pInfoHwSw != nullptr) {
            auto* pCurrent = static_cast<FcInfoHwSw*>(pInfoHwSw);
            while (pCurrent != nullptr) {
                if (pCurrent->flexCardId != 0 &&
                    pCurrent->infoHardware.deviceId != DEV_NO_DEVICE &&
                    pCurrent->infoHardware.supportedCCs.can > 0) {
                    found = pCurrent->flexCardId;
                    break;
                }
                pCurrent = static_cast<FcInfoHwSw*>(pCurrent->pNext);
            }
        }
    } catch (...) {
        // 对应 .NET 的 finally:异常路径也要归还链表
        if (pInfoHwSw != nullptr) {
            try {
                pFreeMemory_(MEM_INFO_HWSW, pInfoHwSw);
            } catch (...) {
            }
        }
        throw;
    }
    // 正常路径归还链表(对应 .NET finally)
    if (pInfoHwSw != nullptr) pFreeMemory_(MEM_INFO_HWSW, pInfoHwSw);
    return found;
}

FcResult EbElApi::Open(uint32_t flexCardId, void** handle) {
    EnsureLoaded();
    *handle = nullptr;
    return pOpen_(handle, flexCardId);
}

FcResult EbElApi::Close(void* handle) {
    EnsureLoaded();
    return pClose_(handle);
}

FcResult EbElApi::MonitoringStart(void* handle, int32_t cc) {
    EnsureLoaded();
    return pMonitoringStart_(handle, cc, 1, MODE_NORMAL);
}

FcResult EbElApi::MonitoringStop(void* handle, int32_t cc) {
    EnsureLoaded();
    return pMonitoringStop_(handle, cc);
}

FcResult EbElApi::Transmit(void* handle, int32_t cc, uint32_t bufferNumber,
                           const uint8_t* data, uint8_t len) {
    EnsureLoaded();
    (void)len;  // 原生接口无长度参数,报文长度由缓冲的既有配置决定
    // transmitNewData=1, ignoreTxRqstLock=0(与 .NET 一致)
    return pTransmit_(handle, cc, bufferNumber, data, 1, 0);
}

FcResult EbElApi::Receive(void* handle, void** pPacket) {
    EnsureLoaded();
    *pPacket = nullptr;
    return pReceive_(handle, pPacket);
}

void EbElApi::FreePacket(void* pPacket) {
    EnsureLoaded();
    pFreeMemory_(MEM_PACKET, pPacket);
}

FcResult EbElApi::SetCcConfiguration(void* handle, int32_t cc,
                                     const FcCANCcConfig& cfg) {
    EnsureLoaded();
    // 原生签名未公开,按参考实现按值传递(36 字节结构体;
    // x64 ABI 下大结构体按隐藏引用传递)
    return pSetCcConfiguration_(handle, cc, cfg);
}

std::string EbElApi::ResultText(FcResult result) {
    if (result == FC_OK) return "OK";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", result);
    return buf;
}

} // namespace ebel
} // namespace canhw
