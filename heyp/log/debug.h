#ifndef HEYP_LOG_DEBUG_H_
#define HEYP_LOG_DEBUG_H_

#include "heyp/log/spdlog.h"

namespace heyp {

void GdbPrintAllStacks(spdlog::logger* logger);

}

#endif  // HEYP_LOG_DEBUG_H_
