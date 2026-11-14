#include "IntrepidApi.h"

#include <stdexcept>

namespace canhw {
namespace intrepid {

IntrepidApi& IntrepidApi::Instance() {
    static IntrepidApi api;
    return api;
}

void IntrepidApi::EnsureLoaded() {
    if (loaded_) return;
    canhw::LockGuard lock(loadLock_);
    if (loaded_) return;

    // 裸 DLL 名,由系统搜索路径解析(与 .NET 一致)
    if (!lib_.Load(DefaultDllPath())) {
        throw std::runtime_error(
            std::string("未找到 ") + DefaultDllPath() +
            ",请确认已安装 Intrepid 驱动(Vehicle Spy 3 / Intrepid Product Drivers) "
            "(WinError=" + std::to_string(DynamicLibrary::LastError()) + ")");
    }

    pFindDevices_ = reinterpret_cast<FindDevicesFn>(lib_.GetSymbol("icsneoFindDevices"));
    pOpenNeoDevice_ =
        reinterpret_cast<OpenNeoDeviceFn>(lib_.GetSymbol("icsneoOpenNeoDevice"));
    pClosePort_ = reinterpret_cast<ClosePortFn>(lib_.GetSymbol("icsneoClosePort"));
    pSetBitRate_ = reinterpret_cast<SetBitRateFn>(lib_.GetSymbol("icsneoSetBitRate"));
    pSetFDBitRate_ =
        reinterpret_cast<SetBitRateFn>(lib_.GetSymbol("icsneoSetFDBitRate"));
    pGetMessages_ =
        reinterpret_cast<GetMessagesFn>(lib_.GetSymbol("icsneoGetMessages"));
    pTxMessages_ = reinterpret_cast<TxMessagesFn>(lib_.GetSymbol("icsneoTxMessages"));
    pTxMessagesEx_ =
        reinterpret_cast<TxMessagesExFn>(lib_.GetSymbol("icsneoTxMessagesEx"));
    pWaitForRx_ = reinterpret_cast<WaitForRxFn>(
        lib_.GetSymbol("icsneoWaitForRxMessagesWithTimeOut"));

    const bool ok = pFindDevices_ && pOpenNeoDevice_ && pClosePort_ && pSetBitRate_ &&
                    pSetFDBitRate_ && pGetMessages_ && pTxMessages_ &&
                    pTxMessagesEx_ && pWaitForRx_;
    if (!ok) {
        lib_.Unload();
        throw std::runtime_error(
            std::string(DefaultDllPath()) +
            " 导出函数不完整(icsneoFindDevices/.../icsneoWaitForRxMessagesWithTimeOut),"
            "请确认安装的是完整的 Intrepid 驱动");
    }
    loaded_ = true;
}

int32_t IntrepidApi::FindDevices(NeoDeviceEx* pNeoDevice, int32_t* pNumDevices,
                                 uint32_t deviceTypes, uint32_t numDeviceTypes,
                                 OptionsNeoEx* pOptions, uint32_t reserved) {
    EnsureLoaded();
    return pFindDevices_(pNeoDevice, pNumDevices, deviceTypes, numDeviceTypes,
                         pOptions, reserved);
}

int32_t IntrepidApi::OpenNeoDevice(NeoDevice* pNeoDevice, void** hObject,
                                   uint8_t* bNetworkIDs, int32_t bConfigRead,
                                   int32_t bSyncToPC) {
    EnsureLoaded();
    return pOpenNeoDevice_(pNeoDevice, hObject, bNetworkIDs, bConfigRead, bSyncToPC);
}

int32_t IntrepidApi::ClosePort(void* hObject, int32_t* pNumberOfErrors) {
    EnsureLoaded();
    return pClosePort_(hObject, pNumberOfErrors);
}

int32_t IntrepidApi::SetBitRate(void* hObject, int32_t bitRate, int32_t networkId) {
    EnsureLoaded();
    return pSetBitRate_(hObject, bitRate, networkId);
}

int32_t IntrepidApi::SetFDBitRate(void* hObject, int32_t bitRate, int32_t networkId) {
    EnsureLoaded();
    return pSetFDBitRate_(hObject, bitRate, networkId);
}

int32_t IntrepidApi::GetMessages(void* hObject, SpyMessage* pMsg,
                                 int32_t* pNumberOfMessages, int32_t* pNumberOfErrors) {
    EnsureLoaded();
    return pGetMessages_(hObject, pMsg, pNumberOfMessages, pNumberOfErrors);
}

int32_t IntrepidApi::TxMessages(void* hObject, const SpyMessage* pMsg,
                                int32_t networkId, int32_t numMessages) {
    EnsureLoaded();
    return pTxMessages_(hObject, pMsg, networkId, numMessages);
}

int32_t IntrepidApi::TxMessagesEx(void* hObject, const SpyMessage* pMsg,
                                  int32_t networkId, int32_t numMessages,
                                  uint32_t* numTxed, uint32_t reserved) {
    EnsureLoaded();
    return pTxMessagesEx_(hObject, pMsg, networkId, numMessages, numTxed, reserved);
}

int32_t IntrepidApi::WaitForRxMessagesWithTimeOut(void* hObject, uint32_t timeoutMs) {
    EnsureLoaded();
    return pWaitForRx_(hObject, timeoutMs);
}

void* IntrepidApi::OpenFirstDevice() {
    // 查找第一台已连接设备并打开(bConfigRead=1,DLL 先读出设备配置)
    NeoDeviceEx device{};
    OptionsNeoEx options{};
    int32_t numDevices = 1;
    int32_t result = FindDevices(&device, &numDevices, 0, 0, &options, 0);
    if (result != 1)
        throw std::runtime_error(
            "Failed(" + std::to_string(result) +
            ") call to icsneoFindDevices,请确认已安装 Intrepid 驱动并连接设备");
    if (numDevices < 1) throw std::runtime_error("No connected Intrepid devices found");

    void* handle = nullptr;
    if ((result = OpenNeoDevice(&device.neoDevice, &handle, nullptr, 1, 0)) != 1)
        throw std::runtime_error("Failed(" + std::to_string(result) +
                                 ") call to icsneoOpenNeoDevice");
    return handle;
}

uint64_t IntrepidApi::GetTimestampUs(int32_t timeHardware, int32_t timeHardware2) {
    // 秒 = TimeHardware2*0.1048576 + TimeHardware*0.0000016,换算为 µs
    constexpr double NEOVI_TIMESTAMP_2 = 0.1048576;
    constexpr double NEOVI_TIMESTAMP_1 = 0.0000016;
    return static_cast<uint64_t>(
        static_cast<double>(static_cast<uint32_t>(timeHardware2)) *
            NEOVI_TIMESTAMP_2 * 1e6 +
        static_cast<double>(static_cast<uint32_t>(timeHardware)) *
            NEOVI_TIMESTAMP_1 * 1e6);
}

} // namespace intrepid
} // namespace canhw
