#pragma once

namespace canhw {

// 各厂商的硬件注册入口(在 src/<vendor>/XxxRegister.cpp 中实现)。
// 由 RegisterBuiltinHardware()(见 Factory.h)统一调用,保证静态库链接时
// 相关编译单元被拉入。

void RegisterZlgHardware();
void RegisterPeakHardware();
void RegisterKvaserHardware();
void RegisterChuangXinHardware();
void RegisterTosunHardware();
void RegisterToomossHardware();
void RegisterVectorHardware();
void RegisterEsdHardware();
void RegisterNiHardware();
void RegisterEtasHardware();
void RegisterIntrepidHardware();
void RegisterEbElHardware();

} // namespace canhw
