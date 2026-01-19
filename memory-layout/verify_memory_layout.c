#include <stdio.h>

#include <stdlib.h>

// Global variable
int gvar = 66;

// Constant global variable
const int cgvar = 1010;

// uninitialized global variable
int ugvar;

void foo() {
  // Local variable
  int lvar = 1;
  printf("Address of lvar:\t%p", (void *)&lvar);
}

int main() {
  // Heap variable
  int *hvar = (int *)malloc(sizeof(int));

  // Checking and comparing address of different
  // elements of program that should be stored in
  // different segements of the memory
  printf("Address of foo:\t\t%p\n", (void *)&foo);
  printf("Address of cgvar:\t%p\n", (void *)&cgvar);
  printf("Address of gvar:\t%p\n", (void *)&gvar);
  printf("Address of ugvar:\t%p\n", (void *)&ugvar);
  printf("Address of hvar:\t%p\n", (void *)hvar);
  foo();

  return 0;
}
