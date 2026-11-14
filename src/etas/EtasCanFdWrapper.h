#pragma once
// ==================================================================
//  ETAS CAN FD 封装(BOA V2 / OCI,回调驱动接收,无轮询线程)
//  对应 .NET Entity/EtasCanFdWrapper.cs
//  注册键:Etas + Generic + CANFD
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>

#include <canhw/EtasConfig.h>
#include <canhw/IHardware.h>

#include "EtasBoaApi.h"

namespace canhw {
namespace etas {

class EtasCanFdWrapper : public IHardware {
public:
    EtasCanFdWrapper() = default;
    ~EtasCanFdWrapper() override { Close(); }

    EtasCanFdWrapper(const EtasCanFdWrapper&) = delete;
    EtasCanFdWrapper& operator=(const EtasCanFdWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    std::string ResolveUri();
    static void RxThunk(void* userData, void* msg);
    void OnRxMsg(void* msg);

    std::shared_ptr<CANFrame> Convert(const EtasCanRxMsg& rx) const;
    std::shared_ptr<CANFrame> Convert(const EtasCanFdRxMsg& rx) const;
    uint64_t TicksToMicros(int64_t ticks) const;
    static int64_t BuildId(uint32_t frameID, uint16_t flags);

    EtasCanFDConfig config_;

    int32_t ctrlHandle_ = UNDEFINED;
    int32_t txHandle_ = UNDEFINED;
    int32_t rxHandle_ = UNDEFINED;

    uint32_t tickFrequency_ = 0;

    canhw::Mutex lock_;
    std::atomic<bool> isOpen_{false};
};

} // namespace etas
} // namespace canhw
