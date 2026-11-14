#pragma once
// ==================================================================
//  CANHardwareCpp 伞头文件
//  纯标准 C++17 的多厂商 CAN / CAN FD 总线硬件抽象库
//  (对应 .NET 版 CAN.Hardware,已移植全部 12 家厂商)
// ==================================================================

#include "CanFrameHelper.h"
#include "ChannelInfo.h"
#include "Config.h"
#include "DataLenConverter.h"
#include "Discovery.h"
#include "Enums.h"
#include "Factory.h"
#include "Frame.h"
#include "IHardware.h"

// 已移植厂商的配置类(按 README 厂商表排序)
#include "ChuangXinConfig.h"
#include "EbElConfig.h"
#include "EsdConfig.h"
#include "EtasConfig.h"
#include "IntrepidConfig.h"
#include "KvaserConfig.h"
#include "NiConfig.h"
#include "PeakConfig.h"
#include "TosunConfig.h"
#include "ToomossConfig.h"
#include "VectorConfig.h"
#include "ZlgConfig.h"
