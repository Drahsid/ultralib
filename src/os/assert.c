#include "PR/os_internal.h"

#ifdef USE_OS_LIBC
void __assert(const char *exp, const char *filename, int line) {
    osSyncPrintf("\nASSERTION FAULT: %s, %d: \"%s\"\n", filename, line, exp);
}
#endif
