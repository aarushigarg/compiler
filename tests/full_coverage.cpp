#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr double kTolerance = 1e-9;
int failures = 0;

void checkClose(const char *name, double actual, double expected) {
  if (std::fabs(actual - expected) > kTolerance) {
    std::fprintf(stderr, "FAIL %s: expected %.12f, got %.12f\n", name, expected,
                 actual);
    ++failures;
    return;
  }
  std::printf("PASS %s = %.12f\n", name, actual);
}

} // namespace

extern "C" double printd(double x) {
  std::printf("printd(%.12f)\n", x);
  return x;
}

extern "C" double binary_colon(double x, double y) asm("_binary:");
extern "C" double binary_colon(double x, double y) { return x - y; }

extern "C" {
double identity(double);
double add(double, double);
double sub(double, double);
double mul(double, double);
double less(double, double);
double grouped(double, double);
double choose(double, double, double);
double withvar(double, double);
double shadowtest(double);
double countdown(double);
double accumulate(double);
double usecustomops(double, double);
double usecaret(double, double, double);
double useexterncolon(double, double);
double callexterns(double);
double useprintd(double);
double loopnostep();
double usesync();
double useasync();
double useasync4();
}

int main() {
  checkClose("identity", identity(1.5), 1.5);
  checkClose("add", add(2.0, 3.0), 5.0);
  checkClose("sub", sub(8.0, 5.0), 3.0);
  checkClose("mul", mul(4.0, 6.0), 24.0);
  checkClose("less true", less(1.0, 2.0), 1.0);
  checkClose("less false", less(3.0, 2.0), 0.0);
  checkClose("grouped", grouped(9.0, 3.0), 72.0);
  checkClose("choose then", choose(1.0, 10.0, 20.0), 10.0);
  checkClose("choose else", choose(0.0, 10.0, 20.0), 20.0);
  checkClose("withvar", withvar(7.0, 8.0), 15.0);
  checkClose("shadowtest", shadowtest(5.0), 6.0);
  checkClose("countdown", countdown(4.0), 0.0);
  checkClose("accumulate", accumulate(6.0), 0.0);
  checkClose("usecustomops", usecustomops(3.0, 2.0), 1.0);
  checkClose("usecaret", usecaret(2.0, 3.0, 4.0), 10.0);
  checkClose("useexterncolon", useexterncolon(9.0, 4.0), 5.0);
  checkClose("callexterns", callexterns(0.5), std::sin(0.5) + std::cos(0.5));
  checkClose("useprintd", useprintd(42.0), 42.0);
  checkClose("loopnostep", loopnostep(), 0.0);
  checkClose("usesync", usesync(), 1.0);
  checkClose("useasync", useasync(), 0.0);
  checkClose("useasync4", useasync4(), 0.0);

  if (failures != 0) {
    std::fprintf(stderr, "%d correctness check(s) failed\n", failures);
    return 1;
  }

  std::printf("All correctness checks passed\n");
  return 0;
}
