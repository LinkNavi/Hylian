#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
int main(){ int64_t n=10000000; int64_t *f=calloc(n+1,8); int64_t c=0;
 for(int64_t i=2;i<=n;i++) if(f[i]==0){ c++; for(int64_t j=i*2;j<=n;j+=i) f[j]=1; }
 printf("%ld\n",(long)c); return 0; }
