#include <stdio.h>
#include "engine.h"
int main(void){
  const mpc_engine_t *E = mpc_engine();
  void *i = E->create(NULL);
  const char *vals[] = {"0","0.25","0.37","0.5","0.75","1"};
  for (unsigned k=0;k<sizeof vals/sizeof vals[0];k++){
    E->set_param(i,"mac_lo",vals[k]);
    char b[64]={0}; E->get_param(i,"iso_lo",b,sizeof b);
    printf("mac_lo=%-5s -> iso_lo=%s\n", vals[k], b);
  }
  E->destroy(i);
  return 0;
}
