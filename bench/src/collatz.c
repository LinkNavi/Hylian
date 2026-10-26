#include <stdio.h>
#include <stdint.h>
int main(){ int64_t best=0,bestn=0;
 for(int64_t n=1;n<1000000;n++){ int64_t x=n,st=0; while(x!=1){ if(x%2==0) x=x/2; else x=3*x+1; st++; } if(st>best){best=st;bestn=n;} }
 printf("%ld\n",(long)bestn); return 0; }
