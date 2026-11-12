#include <canhw/Frame.h>

#include <iomanip>
#include <sstream>
#include <vector>

namespace canhw {

std::string CanFrameFlags::ToString() const {
    std::vector<std::string> parts;
    if (isStandardFrame)    parts.emplace_back("STD");
    if (isExtendedFrame)    parts.emplace_back("EXT");
    if (isRTR)              parts.emplace_back("RTR");
    if (isCanFd)            parts.emplace_back("FDF");
    if (hasBrs)             parts.emplace_back("BRS");
    if (hasEsi)             parts.emplace_back("ESI");
    if (isErrorFrame)       parts.emplace_back("ERROR_FRAME");
    if (hasHwOverrun)       parts.emplace_back("HW_OVERRUN");
    if (hasSwOverrun)       parts.emplace_back("SW_OVERRUN");
    if (hasStuffError)      parts.emplace_back("STUFF");
    if (hasFormError)       parts.emplace_back("FORM");
    if (hasCrcError)        parts.emplace_back("CRC");
    if (hasBitError)        parts.emplace_back("BIT");
    if (isTxAck)            parts.emplace_back("TXACK");
    if (isTxRequest)        parts.emplace_back("TXRQ");
    if (isLocalEcho)        parts.emplace_back("LOCAL_TXACK");
    if (isTxFailed)         parts.emplace_back("TXNACK");
    if (isArbitrationLost)  parts.emplace_back("ABL");
    if (isWakeup)           parts.emplace_back("WAKEUP");
    if (isNerr)             parts.emplace_back("NERR");
    if (isDelayMessage)     parts.emplace_back("DELAY_MSG");

    std::ostringstream sb;
    sb << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
       << rawFlags << std::dec;
    if (!parts.empty()) {
        sb << " [";
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) sb << " | ";
            sb << parts[i];
        }
        sb << ']';
    }
    return sb.str();
}

} // namespace canhw
