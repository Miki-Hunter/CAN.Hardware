#pragma once
// ==================================================================
//  EbEl(Eberspächer Electronics FlexCard)经典 CAN 封装(fcbase.dll)
//  对应 .NET Entity/EbElCanWrapper.cs
//  特点:缓冲式发送(fcbCANTransmit 不带 CAN ID)+ 链表接收
//  (先整链遍历完再 fcFreeMemory 归还)。仅经典 CAN,无 CAN FD。
//  注册键:EbEl + Generic + CAN
// ==================================================================
#include <atomic>
#include <canhw/Sync.h>
#include <canhw/Sync.h>

#include <canhw/EbElConfig.h>
#include <canhw/IHardware.h>

#include "EbElApi.h"

namespace canhw {
namespace ebel {

class EbElCanWrapper : public IHardware {
public:
    EbElCanWrapper() = default;
    ~EbElCanWrapper() override { Close(); }

    EbElCanWrapper(const EbElCanWrapper&) = delete;
    EbElCanWrapper& operator=(const EbElCanWrapper&) = delete;

    BusConfigBase& Config() override { return config_; }
    bool Open() override;
    bool Close() override;
    bool Send(FrameBase& frame) override;
    void FlushReceiveBuffer() override;
    void FlushTransmitBuffer() override;

private:
    void ReadTaskProc();
    /// 取一串报文包并处理;返回是否取到报文。
    /// 关键纪律:先整条链表编组遍历完,再 FreePacket 归还。
    bool ReceiveChain(void* handle);
    void ProcessCanPacket(const FcCanPacket& canPacket);

    /// 按波特率枚举组位时序(参考实现同款查表)
    static FcCANCcConfig BuildCcConfig(EbElCanBitrate bitrate);

    /// 托管层 SocketCAN 高位标志 -> fcbase 原生 ID(bit29=EFF)
    static uint32_t BuildFlexCardId(int64_t id);
    /// fcbase 原生 ID + FlagsAndDLC -> 合成 SocketCAN 高位标志的 ID
    static int64_t BuildManagedId(uint32_t fcId, uint8_t flagsAndDlc);

    EbElCANConfig config_;

    // FlexCard 设备句柄(nullptr 表示未打开)
    void* handle_ = nullptr;
    // 通道号(0 基) -> 通信控制器(fcCC1~fcCC8)
    int32_t cc_ = CC1;

    // 接收轮询空闲间隔(ms):fcbReceive 为"尝试取"语义,无报文时小睡避免空转
    static constexpr int RxPollIdleMs = 5;

    canhw::Mutex lock_;
    std::atomic<bool> stop_{false};
    canhw::Thread readThread_;
    bool isOpen_ = false;
};

} // namespace ebel
} // namespace canhw
