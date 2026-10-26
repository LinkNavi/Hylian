#include <stdio.h>
#include <stdint.h>
int main(){ int64_t s=0; for(int64_t i=0;i<12000;i++) for(int64_t j=0;j<5000;j++) s=(s*31+(i^j))%1000003; printf("%ld\n",(long)s); return 0; }
