#include "BuiltinRegistrations.h"

#include <canhw/Factory.h>

#include <canhw/Sync.h>

namespace canhw {

void RegisterBuiltinHardware() {
    // 本机 MinGW 无 std::once_flag,用 Mutex+bool 实现一次性注册(与参考工程惯例一致);
    // 注册动作在持锁期间完成,保证并发调用者等到全部注册结束才返回
    static Mutex onceMutex;
    static bool done = false;
    LockGuard guard(onceMutex);
    if (done) return;
    RegisterZlgHardware();
    RegisterPeakHardware();
    RegisterKvaserHardware();
    RegisterChuangXinHardware();
    RegisterTosunHardware();
    RegisterToomossHardware();
    RegisterVectorHardware();
    RegisterEsdHardware();
    RegisterNiHardware();
    RegisterEtasHardware();
    RegisterIntrepidHardware();
    RegisterEbElHardware();
    done = true;
}

} // namespace canhw
