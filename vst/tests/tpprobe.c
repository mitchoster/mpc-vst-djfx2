#include <stdio.h>
#include <math.h>
#define M_PIf 3.14159265359f
static float sc(float t){ return (fabsf(t)<1e-6f)?1.0f:sinf(M_PIf*t)/(M_PIf*t); }

/* fractional-delay Tiefpass: h[k] = 2fc*sinc(2fc*(k-o)) * Hann */
static float err_fd(int taps, float fc, int phases, float f){
  float h[8][64]; const float o_step = 1.0f/phases;
  for(int p=0;p<phases;p++){
    const float o=p*o_step; float sum=0;
    for(int k=0;k<taps;k++){
      const float t=(float)(k-(taps-1))+o;
      const float w=(taps>1)?0.5f-0.5f*cosf(2*M_PIf*(float)k/(float)(taps-1)):1.0f;
      h[p][k]=2.0f*fc*sc(2.0f*fc*t)*w; sum+=h[p][k];
    }
    for(int k=0;k<taps;k++) h[p][k]/=sum;
  }
  const float A=1.0f, fs=44100.0f; float mx=0,hist[64];
  for(int n=taps;n<8000;n++){
    for(int k=0;k<taps;k++) hist[k]=A*sinf(2*M_PIf*f*(float)(n-taps+1+k)/fs+M_PIf/4);
    for(int p=0;p<phases;p++){ float y=0; for(int k=0;k<taps;k++) y+=h[p][k]*hist[k]; if(fabsf(y)>mx) mx=fabsf(y); }
  }
  return 20.0f*log10f(mx/A);
}

/* Half-Band-Mitteninterpolatoren (nur o=0.5) */
static float err_hb(int taps, float f){
  float h[64]; const float tab[4]={-0.0625f,0.5625f,0.5625f,-0.0625f};
  float hb8[8]={0.00390625f,-0.03515625f,0.28125f,0.75f,0.75f,0.28125f,-0.03515625f,0.00390625f};
  for(int k=0;k<64;k++) h[k]=0;
  if(taps==4){ for(int k=0;k<4;k++) h[k+30]=tab[k]; }
  else       { for(int k=0;k<8;k++) h[k+28]=hb8[k]; }
  const float A=1.0f, fs=44100.0f; float mx=0,hist[64];
  for(int n=taps;n<8000;n++){
    for(int k=0;k<64;k++) hist[k]=0;
    for(int k=0;k<taps;k++) hist[k]=A*sinf(2*M_PIf*f*(float)(n-k)/fs+M_PIf/4);
    float y=0; for(int k=0;k<64;k++) y+=h[k]*hist[k]; if(fabsf(y)>mx) mx=fabsf(y);
    /* nur Sample selbst als Vergleich */
    float s=A*sinf(2*M_PIf*f*(float)n/fs+M_PIf/4); if(fabsf(s)>mx) mx=fabsf(s);
  }
  return 20.0f*log10f(mx/A);
}

int main(void){
  const float fr[5]={1000,4410,8000,11025,14000};
  printf("%-28s %7s %7s %7s %7s %7s\n","Schaetzer","1k","4.4k","8k","11k","14k");
  for(int p=0;p<2;p++){
    int ph = p?4:2;
    printf("%-28s", ph==4?"FD 20T fc0.45 4x":"FD 20T fc0.45 2x");
    for(int i=0;i<5;i++) printf(" %+7.2f", err_fd(20,0.45f,ph,fr[i]));
    printf("\n");
    printf("%-28s", ph==4?"FD 32T fc0.45 4x":"FD 32T fc0.45 2x");
    for(int i=0;i<5;i++) printf(" %+7.2f", err_fd(32,0.45f,ph,fr[i]));
    printf("\n");
  }
  printf("%-28s","Half-Band 4-Tap (nur Mitte)");
  for(int i=0;i<5;i++) printf(" %+7.2f", err_hb(4,fr[i]));
  printf("\n%-28s","Half-Band 8-Tap (nur Mitte)");
  for(int i=0;i<5;i++) printf(" %+7.2f", err_hb(8,fr[i]));
  printf("\n");
  return 0;
}
