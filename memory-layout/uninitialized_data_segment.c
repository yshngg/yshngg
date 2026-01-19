#include <stdio.h>

// Global uninitialized variables (stored in BSS segment)
int globalVar;
char message[50];

int main() {
  // Static uninitialized variable (also stored in BSS)
  static int staticVar;

  // Assigning values at runtime
  globalVar = 10;
  staticVar = 20;
  snprintf(message, sizeof(message), "Hello BSS");

  printf("Global variable: %d\n", globalVar);
  printf("Static variable: %d\n", staticVar);
  printf("Message: %s\n", message);

  return 0;
}
