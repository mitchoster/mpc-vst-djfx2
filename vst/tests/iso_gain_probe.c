#include <stdio.h>
#include <math.h>
#include "../include/iso.h"
#define FS 44100.0f
int main(void){
  const float hz[3] = {60.0f, 120.0f, 1000.0f};
  const float db[6] = {0.0f, -3.0f, -6.0f, -12.0f, -18.0f, -24.0f};
  printf("%6s", "lo_db");
  for (int h=0;h<3;h++) printf(" | %5.0f Hz", hz[h]);
  printf("\n");
  for (int d=0; d<6; d++){
    printf("%6.1f", db[d]);
    for (int h=0; h<3; h++){
      iso_t *s = iso_create(FS);
      iso_set_lo_db(s, db[d]);
      float l[8192], r[8192];
      double ref=0, out=0;
      for (int pass=0; pass<2; pass++){
        for (int i=0;i<8192;i++){ float x=0.3f*sinf(2.0f*3.14159265f*hz[h]*(float)i/FS); l[i]=x; r[i]=x; }
        iso_process(s,l,r,8192);
      }
      for (int i=4096;i<8192;i++){ out += (double)l[i]*l[i]; ref += (double)(0.3f*0.3f*0.5f); }
      printf(" | %+8.2f", 10.0*log10(out/ref));
      iso_destroy(s);
    }
    printf("\n");
  }
  return 0;
}
