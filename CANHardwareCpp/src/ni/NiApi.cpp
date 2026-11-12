#include "NiApi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace canhw {
namespace ni {

NiApi& NiApi::Instance() {
    static NiApi api;
    return api;
}

void NiApi::EnsureLoaded() {
    if (loaded_) return;
    std::lock_guard<std::mutex> lock(loadLock_);
    if (loaded_) return;

    // 裸 DLL 名,由系统搜索路径解析(与 .NET 一致)
    if (!lib_.Load(DefaultDllPath())) {
        throw std::runtime_error(
            std::string("未找到 ") + DefaultDllPath() +
            ",请先安装 NI-XNET 驱动 (WinError=" +
            std::to_string(DynamicLibrary::LastError()) + ")");
    }

    pCreateSession_ =
        reinterpret_cast<CreateSessionFn>(lib_.GetSymbol("nxCreateSession"));
    pClear_ = reinterpret_cast<SessionFn>(lib_.GetSymbol("nxClear"));
    pFlush_ = reinterpret_cast<SessionFn>(lib_.GetSymbol("nxFlush"));
    pSetProperty64_ =
        reinterpret_cast<SetProperty64Fn>(lib_.GetSymbol("nxSetProperty"));
    pReadFrame_ = reinterpret_cast<ReadFrameFn>(lib_.GetSymbol("nxReadFrame"));
    pWriteFrame_ = reinterpret_cast<WriteFrameFn>(lib_.GetSymbol("nxWriteFrame"));
    pStatusToString_ =
        reinterpret_cast<StatusToStringFn>(lib_.GetSymbol("nxStatusToString"));

    if (!pCreateSession_ || !pClear_ || !pFlush_ || !pSetProperty64_ ||
        !pReadFrame_ || !pWriteFrame_ || !pStatusToString_) {
        lib_.Unload();
        throw std::runtime_error(
            std::string(DefaultDllPath()) +
            " 导出函数不完整(nxCreateSession/nxClear/nxFlush/nxSetProperty/"
            "nxReadFrame/nxWriteFrame/nxStatusToString),请确认安装完整的 NI-XNET 驱动");
    }
    loaded_ = true;
}

int32_t NiApi::CreateSession(const char* databaseName, const char* clusterName,
                             const char* list, const char* interfaceName,
                             uint32_t mode, uint32_t* sessionRef) {
    EnsureLoaded();
    return pCreateSession_(databaseName, clusterName, list, interfaceName, mode,
                           sessionRef);
}

int32_t NiApi::Clear(uint32_t sessionRef) {
    EnsureLoaded();
    return pClear_(sessionRef);
}

int32_t NiApi::Flush(uint32_t sessionRef) {
    EnsureLoaded();
    return pFlush_(sessionRef);
}

int32_t NiApi::SetProperty64(uint32_t sessionRef, uint32_t propertyId,
                             uint32_t propertySize, uint64_t* propertyValue) {
    EnsureLoaded();
    return pSetProperty64_(sessionRef, propertyId, propertySize, propertyValue);
}

int32_t NiApi::ReadFrame(uint32_t sessionRef, uint8_t* buffer, uint32_t sizeOfBuffer,
                         double timeout, uint32_t* numberOfBytesReturned) {
    EnsureLoaded();
    return pReadFrame_(sessionRef, buffer, sizeOfBuffer, timeout,
                       numberOfBytesReturned);
}

int32_t NiApi::WriteFrame(uint32_t sessionRef, const uint8_t* buffer,
                          uint32_t numberOfBytesForFrames, double timeout) {
    EnsureLoaded();
    return pWriteFrame_(sessionRef, buffer, numberOfBytesForFrames, timeout);
}

uint64_t NiApi::PackCustomBaudRate(int prescale, int tseg1, int tseg2, int sjw) {
    const int tqNs = prescale * NX_CAN_TQ_NS;
    if (prescale < 1 || tqNs > 0xFFFF)
        throw std::invalid_argument(
            "prescale valid range: 1-2621(tqNs <= 65535).");
    if (tseg1 < 1 || tseg1 > 128)
        throw std::invalid_argument("tseg1 valid range: 1-128.");
    if (tseg2 < 1 || tseg2 > 128)
        throw std::invalid_argument("tseg2 valid range: 1-128.");
    if (sjw < 1 || sjw > 128)
        throw std::invalid_argument("sjw valid range: 1-128.");
    if (sjw > tseg2)
        throw std::invalid_argument("sjw must not be greater than tseg2.");
    return (static_cast<uint64_t>(tqNs) << 32) | 0xA0000000ull |
           (static_cast<uint64_t>(sjw - 1) << 16) |
           (static_cast<uint64_t>(tseg1 - 1) << 8) |
           static_cast<uint64_t>(tseg2 - 1);
}

uint64_t NiApi::PackCustomFdDataBaudRate(int prescale, int tseg1, int tseg2,
                                         int sjw) {
    const int tqNs = prescale * NX_CAN_TQ_NS;
    if (prescale < 1 || tqNs > 0x3FFF)
        throw std::invalid_argument("prescale valid range: 1-655(tqNs <= 16383).");
    if (tseg1 < 1 || tseg1 > 32)
        throw std::invalid_argument("tseg1 valid range: 1-32.");
    if (tseg2 < 1 || tseg2 > 16)
        throw std::invalid_argument("tseg2 valid range: 1-16.");
    if (sjw < 1 || sjw > 16)
        throw std::invalid_argument("sjw valid range: 1-16.");
    if (sjw > tseg2)
        throw std::invalid_argument("sjw must not be greater than tseg2.");
    return 0xA0000000ull | (static_cast<uint64_t>(tqNs) << 13) |
           (static_cast<uint64_t>(tseg1 - 1) << 8) |
           (static_cast<uint64_t>(tseg2 - 1) << 4) |
           static_cast<uint64_t>(sjw - 1);
}

int NiApi::GetFrameSize(int payloadLength) {
    return NX_FRAME_HEADER_SIZE + std::max(8, (payloadLength + 7) & ~7);
}

std::vector<uint8_t> NiApi::BuildFrameBytes(uint32_t identifier, uint8_t frameType,
                                            const uint8_t* data, int payloadLength) {
    std::vector<uint8_t> buffer(static_cast<size_t>(GetFrameSize(payloadLength)), 0);
    // 偏移 0:Timestamp(u64),发送时由硬件回填,置 0 即可
    std::memcpy(&buffer[8], &identifier, 4);  // 偏移 8:Identifier(u32)
    buffer[12] = frameType;                    // 偏移 12:Type
    // 偏移 13:Flags,偏移 14:Info,发送时均置 0
    buffer[15] = static_cast<uint8_t>(payloadLength);  // 偏移 15:PayloadLength
    if (payloadLength > 0 && data != nullptr)
        std::memcpy(&buffer[NX_FRAME_HEADER_SIZE], data,
                    static_cast<size_t>(payloadLength));
    return buffer;
}

std::string NiApi::GetStatusString(int32_t status) {
    if (status == NX_SUCCESS) return "Success";
    try {
        if (loaded_ && pStatusToString_ != nullptr) {
            uint8_t buffer[1024] = {};
            pStatusToString_(status, buffer, sizeof(buffer));
            size_t len = 0;
            while (len < sizeof(buffer) && buffer[len] != 0) ++len;
            return std::string(reinterpret_cast<const char*>(buffer), len);
        }
    } catch (...) {
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<uint32_t>(status));
    return buf;
}

} // namespace ni
} // namespace canhw
