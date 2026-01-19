#include <stdio.h>

void func() {
  // Stored in the stack
  int local_var = 10;
}

int main() {
  func();
  return 0;
}
