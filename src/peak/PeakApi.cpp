#include "PeakApi.h"

#include <cstdio>
#include <stdexcept>

namespace canhw {
namespace peak {

PeakApi& PeakApi::Instance() {
    static PeakApi api;
    return api;
}

bool PeakApi::Load() {
    if (loaded_) return true;

    if (!lib_.Load(DefaultDllPath())) return false;

    pInitialize_ = reinterpret_cast<InitializeFn>(lib_.GetSymbol("CAN_Initialize"));
    pInitializeFD_ = reinterpret_cast<InitializeFDFn>(lib_.GetSymbol("CAN_InitializeFD"));
    pUninitialize_ = reinterpret_cast<UninitializeFn>(lib_.GetSymbol("CAN_Uninitialize"));
    pReset_ = reinterpret_cast<ResetFn>(lib_.GetSymbol("CAN_Reset"));
    pRead_ = reinterpret_cast<ReadFn>(lib_.GetSymbol("CAN_Read"));
    pReadFD_ = reinterpret_cast<ReadFDFn>(lib_.GetSymbol("CAN_ReadFD"));
    pWrite_ = reinterpret_cast<WriteFn>(lib_.GetSymbol("CAN_Write"));
    pWriteFD_ = reinterpret_cast<WriteFDFn>(lib_.GetSymbol("CAN_WriteFD"));
    pFilterMessages_ =
        reinterpret_cast<FilterMessagesFn>(lib_.GetSymbol("CAN_FilterMessages"));
    pGetValue_ = reinterpret_cast<GetValueFn>(lib_.GetSymbol("CAN_GetValue"));
    pGetErrorText_ = reinterpret_cast<GetErrorTextFn>(lib_.GetSymbol("CAN_GetErrorText"));

    const bool ok = pInitialize_ && pInitializeFD_ && pUninitialize_ && pReset_ &&
                    pRead_ && pReadFD_ && pWrite_ && pWriteFD_ && pFilterMessages_ &&
                    pGetValue_ && pGetErrorText_;
    if (!ok) {
        lib_.Unload();
        return false;
    }
    loaded_ = true;
    return true;
}

void PeakApi::EnsureLoaded() {
    if (loaded_) return;
    if (!Load()) {
        throw std::runtime_error(
            std::string("Failed to load ") + DefaultDllPath() +
            " (WinError=" + std::to_string(DynamicLibrary::LastError()) + ")");
    }
}

uint32_t PeakApi::Initialize(uint16_t channel, uint16_t btr0btr1, uint8_t hwType,
                             uint32_t ioPort, uint16_t interrupt) {
    EnsureLoaded();
    return pInitialize_(channel, btr0btr1, hwType, ioPort, interrupt);
}

uint32_t PeakApi::InitializeFD(uint16_t channel, const char* bitrateFD) {
    EnsureLoaded();
    return pInitializeFD_(channel, bitrateFD);
}

uint32_t PeakApi::Uninitialize(uint16_t channel) {
    EnsureLoaded();
    return pUninitialize_(channel);
}

uint32_t PeakApi::Reset(uint16_t channel) {
    EnsureLoaded();
    return pReset_(channel);
}

uint32_t PeakApi::Read(uint16_t channel, TPCANMsg* msg, TPCANTimestamp* timestamp) {
    EnsureLoaded();
    return pRead_(channel, msg, timestamp);
}

uint32_t PeakApi::ReadFD(uint16_t channel, TPCANMsgFD* msg, uint64_t* timestamp) {
    EnsureLoaded();
    return pReadFD_(channel, msg, timestamp);
}

uint32_t PeakApi::Write(uint16_t channel, TPCANMsg* msg) {
    EnsureLoaded();
    return pWrite_(channel, msg);
}

uint32_t PeakApi::WriteFD(uint16_t channel, TPCANMsgFD* msg) {
    EnsureLoaded();
    return pWriteFD_(channel, msg);
}

uint32_t PeakApi::FilterMessages(uint16_t channel, uint32_t fromId, uint32_t toId,
                                 uint8_t mode) {
    EnsureLoaded();
    return pFilterMessages_(channel, fromId, toId, mode);
}

uint32_t PeakApi::GetValue(uint16_t channel, uint8_t parameter, void* buffer,
                           uint32_t bufferLength) {
    EnsureLoaded();
    return pGetValue_(channel, parameter, buffer, bufferLength);
}

std::string PeakApi::GetErrorText(uint32_t status) {
    if (!loaded_ || pGetErrorText_ == nullptr) {
        char hex[16] = {};
        std::snprintf(hex, sizeof(hex), "0x%X", status);
        return hex;
    }
    char buffer[256] = {};
    pGetErrorText_(status, 0, buffer);  // Language 0 = 中性语言(通常为英文)
    return buffer;
}

} // namespace peak
} // namespace canhw
