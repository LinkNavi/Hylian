#include <stdio.h>
#include <stdint.h>
int64_t fib(int64_t n){ return n<2?n:fib(n-1)+fib(n-2); }
int main(){ printf("%ld\n",(long)fib(32)); return 0; }
