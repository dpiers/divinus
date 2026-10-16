/* Linux asm-generic encoding for host tests; no device ioctl is executed. */
#ifdef _IOR
#undef _IOR
#endif
#define _IOR(t,n,s) ((unsigned long)(2u<<30)|((unsigned long)sizeof(s)<<16)|((unsigned long)(t)<<8)|(n))
