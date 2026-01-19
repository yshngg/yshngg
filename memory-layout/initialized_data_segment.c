#include <stdio.h>

// Global variables (stored in initialized data segment)
int globalVar = 10;
char message[] = "Hello";

int main() {
  // Static variable (also stored in initialized data segment)
  static int staticVar = 20;

  // printf("Global variable: %d\n", globalVar);
  // printf("Static variable: %d\n", staticVar);
  // printf("Message: %s\n", message);

  return 0;
}
