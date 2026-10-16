/* Include only when compiling accepted POSIX adapter for host application tests.
 * poll, clock_gettime, close, fcntl and the entire adapter remain real. */
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
int owned_test_device_open(const char *,int,...);
ssize_t owned_test_device_read(int,void *,size_t);
int owned_test_device_ioctl(int,unsigned long,...);
#define open owned_test_device_open
#define read owned_test_device_read
#define ioctl owned_test_device_ioctl
