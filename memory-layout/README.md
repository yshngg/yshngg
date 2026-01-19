# Memory Layout of C Programs

Link: https://www.geeksforgeeks.org/c/memory-layout-of-c-program/

## Practical Examples

### 1. Check the following simple C program

```c
#include <stdio.h>

int main() {
  return 0;
}
```

```console
text    data     bss     dec     hex filename
 834     460       4    1298     512 a.out
```

### 2. Let us add one global variable in the program, now check the size of bss

```c
#include <stdio.h>

// Uninitialized variable stored in bss
int global;
int main() {
  return 0;
}
```

```console
text	   data	    bss	    dec	    hex	filename
 834	    460	     12	   1306	    51a	a.out
```

### 3. Let us add one static variable which is also stored in bss

```c
#include <stdio.h>

// Uninitialized variable stored in bss
int global;
int main() {
  // Uninitialized static variable stored in bss
  static int i;
  return 0;
}
```

```console
text    data     bss     dec     hex filename
 834     460      12    1306     51a a.out
```

### 4. Let us initialize the static variable which will then be stored in the Data Segment (DS)

```c
#include <stdio.h>

// Uninitialized variable stored in bss
int global;
int main(void) {
  // Initialized static variable stored in DS
  static int i = 100;
  return 0;
}
```

```console
text    data     bss     dec     hex filename
 834     464       8    1306     51a a.out
```

### 5. Let us initialize the global variable which will then be stored in the Data Segment (DS)

```c
#include <stdio.h>

// Uninitialized variable stored in bss
int global = 10;
int main(void) {
  // Initialized static variable stored in DS
  static int i = 100;
  return 0;
}
```

```console
text    data     bss     dec     hex filename
 834     468       4    1306     51a a.out
```

## Example to Verify the Memory Layout

```c
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
```

```console
Address of foo:         0x400476
Address of cgvar:       0x4011b8
Address of gvar:        0x403014
Address of ugvar:       0x40301c
Address of hvar:        0x401f7310
Address of lvar:        0x7fff4c70a1bc
```
