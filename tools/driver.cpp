#include <cstdio>

extern "C" double __program_main();

int main() {
  double result = __program_main();
  std::printf("Program result = %.12f\n", result);
  return 0;
}
