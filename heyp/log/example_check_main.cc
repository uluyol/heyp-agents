#include "heyp/init/init.h"
#include "heyp/log/spdlog.h"

namespace ns1 {

void Func3(int x) {
  auto logger = heyp::MakeLogger("func-3");
  H_SPDLOG_CHECK_EQ(&logger, x, 1);
}

}  // namespace ns1

namespace ns {

void Func2() { ns1::Func3(5); }

}  // namespace ns

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);

  ns::Func2();
  return 0;
}