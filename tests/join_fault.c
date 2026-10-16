/* Controlled error injection only; production never links this translation unit. */
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdlib.h>
static atomic_uint calls;
extern int __real_pthread_join(pthread_t,void **);
int __wrap_pthread_join(pthread_t thread,void **result)
{
 unsigned n=atomic_fetch_add(&calls,1)+1;
 const char *at=getenv("OWNED_TEST_JOIN_FAIL_AT");
 if(at&&n==(unsigned)atoi(at))return EDEADLK;
 return __real_pthread_join(thread,result);
}
