#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rtsp/common.h"
#include "rtsp/mime.h"
int main(void)
{
 const char *input[]={"f","fo","foo","foob","fooba","foobar"};
 const char *expected[]={"Zg==","Zm8=","Zm9v","Zm9vYg==","Zm9vYmE=","Zm9vYmFy"};
 for(unsigned i=0;i<6;i++){
  mime_encoded_handle h=mime_base64_create((char*)input[i],strlen(input[i]));
  assert(h&&strcmp(h->result,expected[i])==0&&h->len_result==strlen(expected[i])+1);
  mime_encoded_delete(h);
 }
 assert(mime_base64_create("x",SIZE_MAX)==NULL);
 puts("base64 short-input and overflow cases passed");
}
