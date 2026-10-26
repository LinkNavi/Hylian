#include <stdio.h>
#include <stdint.h>
int main(){ int n=250; static int64_t a[62500],b[62500],c[62500];
 for(int i=0;i<n;i++) for(int j=0;j<n;j++){ a[i*n+j]=(i+j)%10; b[i*n+j]=(i*j)%10; }
 for(int i=0;i<n;i++) for(int j=0;j<n;j++){ int64_t s=0; for(int k=0;k<n;k++) s+=a[i*n+k]*b[k*n+j]; c[i*n+j]=s; }
 int64_t sum=0; for(int i=0;i<n*n;i++) sum+=c[i]; printf("%ld\n",(long)sum); return 0; }
