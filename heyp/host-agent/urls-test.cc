#include "heyp/host-agent/urls.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

struct Result {
  absl::Status status = absl::OkStatus();
  std::string host;
  int32_t port = 0;
};

std::ostream& operator<<(std::ostream& os, const Result& r) {
  return os << "{status: " << r.status << " host: " << r.host
            << " port: " << r.port << "}";
}

Result EasyParse(absl::string_view s) {
  absl::string_view host;
  Result r;
  r.status = ParseHostPort(s, &host, &r.port);
  r.host = std::string(host);
  return r;
}

MATCHER_P(EqResult, expected, "") {
  return (arg.status == expected.status) && (arg.host == expected.host) &&
         (arg.port == expected.port);
}

TEST(ParseHostPortTest, Basic) {
  EXPECT_THAT(EasyParse("google.com:80"), EqResult(Result{
                                              .host = "google.com",
                                              .port = 80,
                                          }));
  EXPECT_THAT(EasyParse("1.1.1.1:50"), EqResult(Result{
                                           .host = "1.1.1.1",
                                           .port = 50,
                                       }));
  EXPECT_THAT(EasyParse("[2001:DB8::1]:9913"), EqResult(Result{
                                                   .host = "2001:DB8::1",
                                                   .port = 9913,
                                               }));
  EXPECT_THAT(EasyParse("").status.code(), testing::Ne(absl::StatusCode::kOk));
  EXPECT_THAT(EasyParse(":42").status.code(),
              testing::Ne(absl::StatusCode::kOk));
  EXPECT_THAT(EasyParse("mozilla.com:").status.code(),
              testing::Ne(absl::StatusCode::kOk));
  EXPECT_THAT(EasyParse(":").status.code(), testing::Ne(absl::StatusCode::kOk));
}

}  // namespace
}  // namespace heyp
