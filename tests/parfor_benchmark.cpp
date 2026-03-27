#include <chrono>
#include <cmath>
#include <cstdio>

extern "C" {
double serialburn(double);
double parallelburn(double);
}

extern "C" double burn(double x) {
  double value = x + 1.0;
  for (int i = 0; i < 400; ++i) {
    value = std::sin(value) + std::cos(value) + std::sqrt(value + 2.0);
  }
  return value;
}

namespace {

using Clock = std::chrono::steady_clock;

template <typename Func> double timeMillis(Func &&func, int trials) {
  double totalMillis = 0.0;
  for (int i = 0; i < trials; ++i) {
    auto start = Clock::now();
    func();
    auto end = Clock::now();
    totalMillis +=
        std::chrono::duration<double, std::milli>(end - start).count();
  }
  return totalMillis / static_cast<double>(trials);
}

} // namespace

int main() {
  constexpr double kLimit = 5000.0;
  constexpr int kTrials = 3;

  double serialMs = timeMillis([] { serialburn(kLimit); }, kTrials);
  double parallelMs = timeMillis([] { parallelburn(kLimit); }, kTrials);
  double speedup = parallelMs > 0.0 ? serialMs / parallelMs : 0.0;

  std::printf("parfor benchmark limit=%.0f trials=%d\n", kLimit, kTrials);
  std::printf("serialburn    %.3f ms\n", serialMs);
  std::printf("parallelburn  %.3f ms\n", parallelMs);
  std::printf("speedup       %.2fx\n", speedup);
  return 0;
}
