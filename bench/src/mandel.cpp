#include <stdio.h>
#include <stdint.h>
int main(){ int64_t total=0;
 for(int py=0;py<600;py++) for(int px=0;px<600;px++){
  double cr=(double)px*3.0/600.0-2.0, ci=(double)py*2.0/600.0-1.0, zr=0,zi=0; int it=0;
  while(it<100 && zr*zr+zi*zi<4.0){ double t=zr*zr-zi*zi+cr; zi=2.0*zr*zi+ci; zr=t; it++; }
  total+=it; }
 printf("%ld\n",(long)total); return 0; }
