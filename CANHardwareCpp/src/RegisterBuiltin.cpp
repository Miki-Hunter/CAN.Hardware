#include "BuiltinRegistrations.h"

#include <canhw/Factory.h>

#include <mutex>

namespace canhw {

void RegisterBuiltinHardware() {
    static std::once_flag once;
    std::call_once(once, [] {
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
    });
}

} // namespace canhw
