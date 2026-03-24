#include <stdio.h>

// Case 1: inlinable — will match goto
int simple_add(int a, int b) {
    return a + b;
}

// Case 2: forced noinline — will NOT match goto
__attribute__((noinline)) int noinline_add(int a, int b) {
    return a + b;
}

// Case 3: complex function — compiler may choose not to inline
int heavy(int x) {
    int r = x;
    for (int i = 0; i < 10; i++)
        r = r * 31 + i;
    return r;
}

int main(void) {
    volatile int r;

    // A: goto
    r = 0;
    int i = 0;
loop:
    r = r + 1;
    i++;
    if (i < 100) goto loop;

    // B: inlined func
    r = 0;
    for (int j = 0; j < 100; j++)
        r = simple_add(r, 1);

    // C: noinline func
    r = 0;
    for (int j = 0; j < 100; j++)
        r = noinline_add(r, 1);

    // D: heavy func
    r = 0;
    for (int j = 0; j < 100; j++)
        r = heavy(r);

    return r;
}
