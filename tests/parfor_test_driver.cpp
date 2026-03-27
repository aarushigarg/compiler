#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

namespace {

constexpr double kTolerance = 1e-9;
std::mutex recordedValuesMutex;
std::vector<double> recordedValues;
int failures = 0;

void resetRecordedValues() {
  std::lock_guard<std::mutex> lock(recordedValuesMutex);
  recordedValues.clear();
}

std::vector<double> snapshotRecordedValues() {
  std::lock_guard<std::mutex> lock(recordedValuesMutex);
  return recordedValues;
}

void expectClose(const char *name, double actual, double expected) {
  if (std::fabs(actual - expected) > kTolerance) {
    std::fprintf(stderr, "FAIL %s: expected %.12f, got %.12f\n", name, expected,
                 actual);
    ++failures;
    return;
  }
  std::printf("PASS %s = %.12f\n", name, actual);
}

void expectValues(const char *name, const std::vector<double> &expected) {
  std::vector<double> actual = snapshotRecordedValues();
  std::sort(actual.begin(), actual.end());
  std::vector<double> sortedExpected = expected;
  std::sort(sortedExpected.begin(), sortedExpected.end());

  if (actual.size() != sortedExpected.size()) {
    std::fprintf(stderr, "FAIL %s: expected %zu values, got %zu\n", name,
                 sortedExpected.size(), actual.size());
    ++failures;
    return;
  }

  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (std::fabs(actual[i] - sortedExpected[i]) > kTolerance) {
      std::fprintf(stderr, "FAIL %s: mismatch at index %zu (expected %.12f, got %.12f)\n",
                   name, i, sortedExpected[i], actual[i]);
      ++failures;
      return;
    }
  }

  std::printf("PASS %s (%zu values)\n", name, actual.size());
}

} // namespace

extern "C" double recordvalue(double value) {
  std::lock_guard<std::mutex> lock(recordedValuesMutex);
  recordedValues.push_back(value);
  return value;
}

extern "C" {
double parforcount();
double parfordefaultstep();
double parforstep();
double parforcapture(double);
double parfornested();
double parforempty();
}

int main() {
  resetRecordedValues();
  expectClose("parforcount return", parforcount(), 0.0);
  expectValues("parforcount", {0, 1, 2, 3, 4, 5, 6, 7});

  resetRecordedValues();
  expectClose("parfordefaultstep return", parfordefaultstep(), 0.0);
  expectValues("parfordefaultstep", {0, 1, 2});

  resetRecordedValues();
  expectClose("parforstep return", parforstep(), 0.0);
  expectValues("parforstep", {2, 4, 6, 8});

  resetRecordedValues();
  expectClose("parforcapture return", parforcapture(10.0), 0.0);
  expectValues("parforcapture", {10, 11, 12, 13});

  resetRecordedValues();
  expectClose("parfornested return", parfornested(), 0.0);
  expectValues("parfornested", {0, 1, 10, 11, 20, 21});

  resetRecordedValues();
  expectClose("parforempty return", parforempty(), 0.0);
  expectValues("parforempty", {});

  if (failures != 0) {
    std::fprintf(stderr, "%d parfor check(s) failed\n", failures);
    return 1;
  }

  std::printf("All parfor checks passed\n");
  return 0;
}
