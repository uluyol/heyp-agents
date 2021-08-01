#ifndef HEYP_LOG_SPDLOG_H_
#define HEYP_LOG_SPDLOG_H_

#include <iostream>
#include <string>
#include <vector>

#include "absl/base/optimization.h"
#include "spdlog/fmt/ostr.h"  // export
#include "spdlog/spdlog.h"

namespace heyp {

const std::vector<spdlog::sink_ptr>& LogSinks();

spdlog::logger MakeLogger(std::string name);

//// Checks (uses logger) ////

#define H_SPDLOG_CHECK_MESG(logger, cond, mesg)                                     \
  do {                                                                              \
    if (ABSL_PREDICT_FALSE(!(cond))) {                                              \
      if (std::string(mesg) != "") {                                                \
        SPDLOG_LOGGER_CRITICAL(logger, "invariant violation: wanted {}: {}", #cond, \
                               mesg);                                               \
      } else {                                                                      \
        SPDLOG_LOGGER_CRITICAL(logger, "invariant violation: wanted {}", #cond);    \
      }                                                                             \
      ::heyp::DumpStackTraceAndExit(5);                                             \
    }                                                                               \
  } while (0);
#define H_SPDLOG_CHECK_EQ_MESG(logger, val1, val2, mesg)                                 \
  do {                                                                                   \
    if (ABSL_PREDICT_FALSE(!((val1) == (val2)))) {                                       \
      if (std::string(mesg) != "") {                                                     \
        SPDLOG_LOGGER_CRITICAL(logger,                                                   \
                               "invariant violation: wanted {} [{}] == {} [{}]: {}",     \
                               #val1, val1, #val2, val2, mesg);                          \
      } else {                                                                           \
        SPDLOG_LOGGER_CRITICAL(logger, "invariant violation: wanted {} [{}] == {} [{}]", \
                               #val1, val1, #val2, val2);                                \
      }                                                                                  \
      ::heyp::DumpStackTraceAndExit(5);                                                  \
    }                                                                                    \
  } while (0);
#define H_SPDLOG_CHECK_NE_MESG(logger, val1, val2, mesg)                                 \
  do {                                                                                   \
    if (ABSL_PREDICT_FALSE(!((val1) != (val2)))) {                                       \
      if (std::string(mesg) != "") {                                                     \
        SPDLOG_LOGGER_CRITICAL(logger,                                                   \
                               "invariant violation: wanted {} [{}] != {} [{}]: {}",     \
                               #val1, val1, #val2, val2, mesg);                          \
      } else {                                                                           \
        SPDLOG_LOGGER_CRITICAL(logger, "invariant violation: wanted {} [{}] != {} [{}]", \
                               #val1, val1, #val2, val2);                                \
      }                                                                                  \
      ::heyp::DumpStackTraceAndExit(5);                                                  \
    }                                                                                    \
  } while (0);
#define H_SPDLOG_CHECK_LE_MESG(logger, val1, val2, mesg)                                 \
  do {                                                                                   \
    if (ABSL_PREDICT_FALSE(!((val1) <= (val2)))) {                                       \
      if (std::string(mesg) != "") {                                                     \
        SPDLOG_LOGGER_CRITICAL(logger,                                                   \
                               "invariant violation: wanted {} [{}] <= {} [{}]: {}",     \
                               #val1, val1, #val2, val2, mesg);                          \
      } else {                                                                           \
        SPDLOG_LOGGER_CRITICAL(logger, "invariant violation: wanted {} [{}] <= {} [{}]", \
                               #val1, val1, #val2, val2);                                \
      }                                                                                  \
      ::heyp::DumpStackTraceAndExit(5);                                                  \
    }                                                                                    \
  } while (0);
#define H_SPDLOG_CHECK_LT_MESG(logger, val1, val2, mesg)                                 \
  do {                                                                                   \
    if (ABSL_PREDICT_FALSE(!((val1) < (val2)))) {                                        \
      if (std::string(mesg) != "") {                                                     \
        SPDLOG_LOGGER_CRITICAL(logger,                                                   \
                               "invariant violation: wanted {} [{}] <  {} [{}]: {}",     \
                               #val1, val1, #val2, val2, mesg);                          \
      } else {                                                                           \
        SPDLOG_LOGGER_CRITICAL(logger, "invariant violation: wanted {} [{}] <  {} [{}]", \
                               #val1, val1, #val2, val2);                                \
        ::heyp::DumpStackTraceAndExit(5);                                                \
      }                                                                                  \
    }                                                                                    \
  } while (0);
#define H_SPDLOG_CHECK_GE_MESG(logger, val1, val2, mesg)                                 \
  do {                                                                                   \
    if (ABSL_PREDICT_FALSE(!((val1) >= (val2)))) {                                       \
      if (std::string(mesg) != "") {                                                     \
        SPDLOG_LOGGER_CRITICAL(logger,                                                   \
                               "invariant violation: wanted {} [{}] >= {} [{}]: {}",     \
                               #val1, val1, #val2, val2, mesg);                          \
      } else {                                                                           \
        SPDLOG_LOGGER_CRITICAL(logger, "invariant violation: wanted {} [{}] >= {} [{}]", \
                               #val1, val1, #val2, val2);                                \
      }                                                                                  \
      ::heyp::DumpStackTraceAndExit(5);                                                  \
    }                                                                                    \
  } while (0);
#define H_SPDLOG_CHECK_GT_MESG(logger, val1, val2, mesg)                                 \
  do {                                                                                   \
    if (ABSL_PREDICT_FALSE(!((val1) > (val2)))) {                                        \
      if (std::string(mesg) != "") {                                                     \
        SPDLOG_LOGGER_CRITICAL(logger,                                                   \
                               "invariant violation: wanted {} [{}] >  {} [{}]: {}",     \
                               #val1, val1, #val2, val2, mesg);                          \
      } else {                                                                           \
        SPDLOG_LOGGER_CRITICAL(logger, "invariant violation: wanted {} [{}] >  {} [{}]", \
                               #val1, val1, #val2, val2);                                \
      }                                                                                  \
      ::heyp::DumpStackTraceAndExit(5);                                                  \
    }                                                                                    \
  } while (0);

#define H_SPDLOG_CHECK(logger, cond) H_SPDLOG_CHECK_MESG(logger, cond, "")
#define H_SPDLOG_CHECK_EQ(logger, val1, val2) \
  H_SPDLOG_CHECK_EQ_MESG(logger, val1, val2, "")
#define H_SPDLOG_CHECK_NE(logger, val1, val2) \
  H_SPDLOG_CHECK_NE_MESG(logger, val1, val2, "")
#define H_SPDLOG_CHECK_LE(logger, val1, val2) \
  H_SPDLOG_CHECK_LE_MESG(logger, val1, val2, "")
#define H_SPDLOG_CHECK_LT(logger, val1, val2) \
  H_SPDLOG_CHECK_LT_MESG(logger, val1, val2, "")
#define H_SPDLOG_CHECK_GE(logger, val1, val2) \
  H_SPDLOG_CHECK_GE_MESG(logger, val1, val2, "")
#define H_SPDLOG_CHECK_GT(logger, val1, val2) \
  H_SPDLOG_CHECK_GT_MESG(logger, val1, val2, "")

//// Assertions (uses stderr) ////

#define H_ASSERT_MESG(cond, mesg)                                               \
  do {                                                                          \
    if (ABSL_PREDICT_FALSE(!(cond))) {                                          \
      if (std::string(mesg) != "") {                                            \
        std::cerr << "assert failed: wanted " << #cond << ": " << mesg << "\n"; \
      } else {                                                                  \
        std::cerr << "assert failed: wanted " << #cond << "\n";                 \
      }                                                                         \
      ::heyp::DumpStackTraceAndExit(5);                                         \
    }                                                                           \
  } while (0);
#define H_ASSERT_EQ_MESG(val1, val2, mesg)                                      \
  do {                                                                          \
    if (ABSL_PREDICT_FALSE(!((val1) == (val2)))) {                              \
      if (std::string(mesg) != "") {                                            \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1          \
                  << "] == " << #val2 << " [" << val2 << "]: " << mesg << "\n"; \
      } else {                                                                  \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1          \
                  << "] == " << #val2 << " [" << val2 << "]\n"                  \
                  << mesg << "\n";                                              \
      }                                                                         \
      ::heyp::DumpStackTraceAndExit(5);                                         \
    }                                                                           \
  } while (0);
#define H_ASSERT_NE_MESG(val1, val2, mesg)                                      \
  do {                                                                          \
    if (ABSL_PREDICT_FALSE(!((val1) != (val2)))) {                              \
      if (std::string(mesg) != "") {                                            \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1          \
                  << "] != " << #val2 << " [" << val2 << "]: " << mesg << "\n"; \
      } else {                                                                  \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1          \
                  << "] != " << #val2 << " [" << val2 << "]\n"                  \
                  << mesg << "\n";                                              \
      }                                                                         \
      ::heyp::DumpStackTraceAndExit(5);                                         \
    }                                                                           \
  } while (0);
#define H_ASSERT_LE_MESG(val1, val2, mesg)                                      \
  do {                                                                          \
    if (ABSL_PREDICT_FALSE(!((val1) <= (val2)))) {                              \
      if (std::string(mesg) != "") {                                            \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1          \
                  << "] <= " << #val2 << " [" << val2 << "]: " << mesg << "\n"; \
      } else {                                                                  \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1          \
                  << "] <= " << #val2 << " [" << val2 << "]\n";                 \
      }                                                                         \
      ::heyp::DumpStackTraceAndExit(5);                                         \
    }                                                                           \
  } while (0);
#define H_ASSERT_LT_MESG(val1, val2, mesg)                                        \
  do {                                                                            \
    if (ABSL_PREDICT_FALSE(!((val1) < (val2)))) {                                 \
      if (std::string(mesg) != "") {                                              \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1 << "] <  " \
                  << #val2 << " [" << val2 << "]: " << mesg << "\n";              \
      } else {                                                                    \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1 << "] <  " \
                  << #val2 << " [" << val2 << "]\n";                              \
        ::heyp::DumpStackTraceAndExit(5);                                         \
      }                                                                           \
    }                                                                             \
  } while (0);
#define H_ASSERT_GE_MESG(val1, val2, mesg)                                      \
  do {                                                                          \
    if (ABSL_PREDICT_FALSE(!((val1) >= (val2)))) {                              \
      if (std::string(mesg) != "") {                                            \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1          \
                  << "] >= " << #val2 << " [" << val2 << "]: " << mesg << "\n"; \
      } else {                                                                  \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1          \
                  << "] >= " << #val2 << " [" << val2 << "]\n";                 \
      }                                                                         \
      ::heyp::DumpStackTraceAndExit(5);                                         \
    }                                                                           \
  } while (0);
#define H_ASSERT_GT_MESG(val1, val2, mesg)                                        \
  do {                                                                            \
    if (ABSL_PREDICT_FALSE(!((val1) > (val2)))) {                                 \
      if (std::string(mesg) != "") {                                              \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1 << "] >  " \
                  << #val2 << " [" << val2 << "]: " << mesg << "\n";              \
      } else {                                                                    \
        std::cerr << "assert failed: wanted " << #val1 << " [" << val1 << "] >  " \
                  << #val2 << " [" << val2 << "]\n";                              \
      }                                                                           \
      ::heyp::DumpStackTraceAndExit(5);                                           \
    }                                                                             \
  } while (0);

#define H_ASSERT(cond) H_ASSERT_MESG(cond, "")
#define H_ASSERT_EQ(val1, val2) H_ASSERT_EQ_MESG(val1, val2, "")
#define H_ASSERT_NE(val1, val2) H_ASSERT_NE_MESG(val1, val2, "")
#define H_ASSERT_LE(val1, val2) H_ASSERT_LE_MESG(val1, val2, "")
#define H_ASSERT_LT(val1, val2) H_ASSERT_LT_MESG(val1, val2, "")
#define H_ASSERT_GE(val1, val2) H_ASSERT_GE_MESG(val1, val2, "")
#define H_ASSERT_GT(val1, val2) H_ASSERT_GT_MESG(val1, val2, "")

void DumpStackTraceAndExit(int exit_status);

}  // namespace heyp

#endif  // HEYP_LOG_SPDLOG_H_
