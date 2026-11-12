#pragma once
// ==================================================================
//  ETAS BOA V2(Boa Open Api)互操作层
//  (对应 .NET BaseLibDll/EtasBoaLibDLL.cs)
//  BOA 由两个原生 DLL 组成:dll-ocdProxy.dll(OCI,控制器与收发)
//  与 dll-csiBind.dll(CSI,硬件枚举),随 ETAS BOA V2 驱动安装
//  (如随 INCA 安装)到 Program Files 目录,不随工程携带。
//  定位:注册表 HKLM\SOFTWARE\ETAS\BOA\2 的 PATH 值 + 默认安装路径回退;
//  加载:完整路径 LoadLibraryEx(LOAD_WITH_ALTERED_SEARCH_PATH),
//  使 BOA DLL 的同目录依赖可解析。调用约定均为 Cdecl。
//  进程生命周期内不卸载(驱动 DLL 内部有全局状态)。
// ==================================================================
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <canhw/EtasConfig.h>

#include "DynamicLibrary.h"

namespace canhw {
namespace etas {

constexpr int UNDEFINED = -1;

/// BOA 控制器 URI 前缀(如 "ETAS:/" + "/ETAS.ETH://ES581.4/CAN:1")
constexpr const char* URI_PREFIX = "ETAS:/";

/// OCI CAN API 版本(固定协商 1.3)
struct EtasVersion {
    uint8_t major;
    uint8_t minor;
    uint8_t bugFix;
    uint8_t build;
};

using EtasErrorCode = uint32_t;
constexpr EtasErrorCode CODE_SUCCESS = 0;
constexpr EtasErrorCode CODE_ERROR = 0x80000000;
inline bool IsError(EtasErrorCode code) { return (code & CODE_ERROR) != 0; }

// ---------------- 配置结构体(自然对齐,与 OCI 头一致) ----------------

/// OCI_CAN FD 扩展配置(OCI_CANFDConfiguration,40 字节)
struct EtasCanFdConfiguration {
    uint32_t bitRate;      ///< 数据段波特率(Hz)
    uint32_t samplePoint;  ///< 采样点(单位 1%)
    uint32_t btlCycles;    ///< 每比特时间量子数
    uint32_t sjw;
    uint32_t flags;
    uint32_t txSecondarySamplePointOffset;
    uint32_t canRxMode;    ///< 2 = CAN_FRAMES_USING_CAN_MESSAGE
    uint32_t canFdRxMode;  ///< 4 = CANFD_FRAMES_USING_CANFD_MESSAGE
    uint32_t canFdTxConfig;///< 4 = USE_CAN_AND_CANFD_FRAMES
    uint16_t txSecondarySamplePointFilterWindow;
    uint16_t reserved;
};

/// OCI_CAN 控制器配置(OCI_CANConfiguration)
struct EtasCanConfiguration {
    uint32_t baudrate;            ///< 仲裁段波特率(Hz)
    uint32_t samplePoint;
    uint32_t samplesPerBit;       ///< 1 = ONE_SAMPLE_PER_BIT
    uint32_t btlCycles;
    uint32_t sjw;
    uint32_t syncEdge;            ///< 1 = SINGLE_SYNC_EDGE
    uint32_t physicalMedia;       ///< 1 = HIGH_SPEED
    uint32_t selfReceptionMode;   ///< 0 = RECEPTION_OFF
    uint32_t busParticipationMode;///< 2 = ACTIVE
    uint32_t canFDEnabled;
    EtasCanFdConfiguration canFDConfig;
    uint32_t canTxPolicy;         ///< 1 = FIFO

    /// 构造控制器配置;fdBaudrateHz=0 表示经典 CAN,否则使能 CAN FD
    static EtasCanConfiguration Create(uint32_t baudrateHz, uint32_t fdBaudrateHz,
                                       uint32_t samplePoint = 75,
                                       uint32_t btlCycles = 10, uint32_t sjw = 1,
                                       uint32_t fdSamplePoint = 80,
                                       uint32_t fdBtlCycles = 10, uint32_t fdSjw = 1);
};

/// OCI_ControllerProperties(0=RUNNING 立即上线)
struct EtasControllerProperties {
    uint32_t mode;
};

/// OCI_CANTxQueueConfiguration
struct EtasTxQueueConfiguration {
    uint32_t reserved;
};

/// OCI_RxCallbackSingleMsg:函数指针 + 用户数据
using OciCallbackEx = void (*)(void* userData, void* msg);

struct EtasRxCallbackSingleMsg {
    OciCallbackEx functionEx;
    void* userData;
};

/// OCI_CANRxQueueConfiguration
struct EtasRxQueueConfiguration {
    EtasRxCallbackSingleMsg onFrame;
    EtasRxCallbackSingleMsg onEvent;
    uint32_t selfReceptionMode;  ///< 0 = RECEPTION_OFF
};

/// OCI_CANRxFilter:全 0 即放行所有帧
struct EtasRxFilter {
    uint32_t frameIDValue;
    uint32_t frameIDMask;
    uint32_t tag;
};

/// OCI_TimerCapabilities(仅用 tickFrequency 换算时间戳,其余占位)
struct EtasTimerCapabilities {
    char localClockID[40];
    uint32_t format;
    uint32_t tickFrequency;
    uint32_t ticksPerIncrement;
    uint32_t localStratumLevel;
    uint32_t localReferenceScale;
    char localTimeOriginIso8601[40];
    uint32_t syncSlave;
    uint32_t syncMaster;
};

// ---------------- 消息结构体 ----------------

// OCI_CAN 消息类型(回调消息头 type 字段)
constexpr uint32_t MSG_TYPE_RX_MESSAGE = 1;
constexpr uint32_t MSG_TYPE_TX_MESSAGE = 2;
constexpr uint32_t MSG_TYPE_ERROR_FRAME = 3;
constexpr uint32_t MSG_TYPE_BUS_EVENT = 4;
constexpr uint32_t MSG_TYPE_INTERNAL_ERROR_EVENT = 5;
constexpr uint32_t MSG_TYPE_QUEUE_EVENT = 6;
constexpr uint32_t MSG_TYPE_TIMER_EVENT = 7;
constexpr uint32_t MSG_TYPE_FDRX_MESSAGE = 8;
constexpr uint32_t MSG_TYPE_FDTX_MESSAGE = 9;

// OCI_CAN_MSG_FLAG 标志位(u16)
constexpr uint16_t MSG_FLAG_EXTENDED = 0x01;         ///< 扩展帧(29bit ID)
constexpr uint16_t MSG_FLAG_REMOTE_FRAME = 0x02;     ///< RTR 远程帧
constexpr uint16_t MSG_FLAG_SELFRECEPTION = 0x04;    ///< 自接收
constexpr uint16_t MSG_FLAG_FD_DATA_BIT_RATE = 0x08; ///< CAN FD BRS
constexpr uint16_t MSG_FLAG_FD_TRUNC_AND_PAD = 0x10;
constexpr uint16_t MSG_FLAG_FD_ERROR_PASSIVE = 0x20; ///< FD 错误被动(≈ESI)
constexpr uint16_t MSG_FLAG_FD_DATA = 0x40;          ///< CAN FD 帧

/// OCI_CAN 发送消息(24 字节,带 type+reserved 头)
struct EtasCanTxMsg {
    uint32_t type;      ///< MSG_TYPE_TX_MESSAGE
    uint32_t reserved;
    uint32_t frameID;
    uint16_t flags;
    uint8_t res;
    uint8_t dlc;        ///< 负载字节数(0..8),非 4bit DLC 编码
    uint8_t data[8];
};

/// OCI_CANFD 发送消息(80 字节)
struct EtasCanFdTxMsg {
    uint32_t type;      ///< MSG_TYPE_FDTX_MESSAGE
    uint32_t reserved;
    uint32_t frameID;
    uint16_t flags;
    uint8_t res;
    uint8_t size;       ///< 负载字节数(0..64)
    uint8_t data[64];
};

/// OCI_CANRxMessage(40 字节)
struct EtasCanRxMsg {
    uint32_t type;
    uint32_t reserved;
    int64_t timeStamp;  ///< OCI_Time 硬件 tick,按 tickFrequency 换算
    uint32_t tag;
    uint32_t frameID;
    uint16_t flags;
    uint8_t res;
    uint8_t dlc;
    uint32_t res1;
    uint8_t data[8];
};

/// OCI_CANFDRxMessage(96 字节)
struct EtasCanFdRxMsg {
    uint32_t type;
    uint32_t reserved;
    int64_t timeStamp;
    uint32_t tag;
    uint32_t frameID;
    uint16_t flags;
    uint8_t res;
    uint8_t dlc;  ///< 官方名 size,负载字节数(0..64)
    uint32_t res1;
    uint8_t data[64];
};

constexpr int TX_MSG_SIZE = 24;
constexpr int FD_TX_MSG_SIZE = 80;

// ---------------- CSI 硬件枚举 ----------------

constexpr uint32_t NODE_TYPE_MIN_PHYSICAL_NODE = 0x2000;
constexpr uint32_t NODE_TYPE_MAX_PHYSICAL_NODE = 0x2FFF;

struct EtasNodeRange {
    uint32_t min;
    uint32_t max;
};

struct EtasUuidBin {
    uint8_t data[16];
};

struct EtasUuidVersion {
    EtasUuidBin uuid;
    EtasVersion version;
};

struct EtasServiceId {
    EtasUuidVersion api;
    EtasUuidVersion access;
};

struct EtasServiceIdParam {
    EtasServiceId id;
    uint32_t count;
    uint8_t accessParam[128];
};

/// CSI_SubItem(628 字节,布局与 python-can 的 ctypes 绑定一致)
struct EtasSubItem {
    EtasServiceIdParam server;
    uint32_t nodeType;
    uint8_t uriName[128];
    uint8_t visibleName[4];
    EtasVersion version;
    uint8_t reserved2[88];
    EtasUuidBin serverAffinity;
    EtasUuidBin requiredAffinity0;
    int32_t reserved[4];
    EtasUuidBin requiredAffinity1;
    uint32_t count;
    EtasServiceId requiredAPI[4];
};
static_assert(sizeof(EtasSubItem) == 628, "EtasSubItem layout mismatch");

/// CSI 协议树节点(非托管内存,Sibling/Child 为指针)
struct EtasCsiTree {
    EtasSubItem item;
    EtasCsiTree* sibling;
    EtasCsiTree* child;
    int childrenProbed;
};

class EtasBoaApi {
public:
    static EtasBoaApi& Instance();

    /// 定位并加载 dll-ocdProxy.dll / dll-csiBind.dll,取出全部函数指针
    void EnsureLoaded();
    bool IsLoaded() const { return loaded_; }

    // OCI
    EtasErrorCode CreateCANControllerVersion(const char* uriLocation,
                                             EtasVersion* version, int32_t* handle);
    EtasErrorCode DestroyCANController(int32_t handle);
    EtasErrorCode CloseCANController(int32_t handle);
    EtasErrorCode OpenCANController(int32_t handle,
                                    EtasCanConfiguration* configuration,
                                    EtasControllerProperties* properties);
    EtasErrorCode CreateCANTxQueue(int32_t handle,
                                   EtasTxQueueConfiguration* configuration,
                                   int32_t* queue);
    EtasErrorCode CreateCANRxQueue(int32_t handle,
                                   EtasRxQueueConfiguration* configuration,
                                   int32_t* queue);
    EtasErrorCode WriteCANDataEx(int32_t handle, int64_t timeout, void** messages,
                                 uint32_t size, uint32_t* remaining);
    EtasErrorCode AddCANFrameFilter(int32_t queue, EtasRxFilter* filter,
                                    uint32_t count);
    /// 查询控制器硬件定时器频率(tick/秒);失败返回 0,调用方按"无时间戳"降级
    uint32_t QueryTickFrequency(int32_t ctrlHandle);

    // CSI
    /// 通过 CSI 协议树枚举支持指定协议的硬件节点路径(如 "/ETAS.ETH://ES581.4/CAN:1")
    std::vector<std::string> EnumerateNodes(const std::string& protocol);

    /// 错误码文本(十六进制)
    static std::string StatusText(EtasErrorCode code);

private:
    EtasBoaApi() = default;

    DynamicLibrary boaLib_;
    DynamicLibrary csiLib_;
    std::atomic<bool> loaded_{false};
    std::mutex loadLock_;

    using OciCreateControllerVersionFn = EtasErrorCode (*)(const char*, EtasVersion*,
                                                           int32_t*);
    using OciHandleFn = EtasErrorCode (*)(int32_t);
    using OciOpenCANControllerFn = EtasErrorCode (*)(int32_t, EtasCanConfiguration*,
                                                     EtasControllerProperties*);
    using OciCreateTxQueueFn = EtasErrorCode (*)(int32_t, EtasTxQueueConfiguration*,
                                                 int32_t*);
    using OciCreateRxQueueFn = EtasErrorCode (*)(int32_t, EtasRxQueueConfiguration*,
                                                 int32_t*);
    using OciWriteFn = EtasErrorCode (*)(int32_t, int64_t, void**, uint32_t,
                                         uint32_t*);
    using OciFilterFn = EtasErrorCode (*)(int32_t, EtasRxFilter*, uint32_t);
    using OciTimerFn = EtasErrorCode (*)(int32_t, EtasTimerCapabilities*);
    using CsiCreateTreeFn = EtasErrorCode (*)(const char*, EtasNodeRange,
                                              EtasCsiTree**);
    using CsiDestroyTreeFn = EtasErrorCode (*)(EtasCsiTree*);

    OciCreateControllerVersionFn pCreateController_ = nullptr;
    OciHandleFn pDestroyController_ = nullptr;
    OciHandleFn pCloseController_ = nullptr;
    OciOpenCANControllerFn pOpenCANController_ = nullptr;
    OciCreateTxQueueFn pCreateTxQueue_ = nullptr;
    OciCreateRxQueueFn pCreateRxQueue_ = nullptr;
    OciWriteFn pWriteCANDataEx_ = nullptr;
    OciFilterFn pAddFilter_ = nullptr;
    OciTimerFn pGetTimerCapabilities_ = nullptr;
    CsiCreateTreeFn pCreateTree_ = nullptr;
    CsiDestroyTreeFn pDestroyTree_ = nullptr;
};

} // namespace etas
} // namespace canhw
