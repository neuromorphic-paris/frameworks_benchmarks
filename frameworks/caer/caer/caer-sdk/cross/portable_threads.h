#ifndef CAER_SDK_PORTABLE_THREADS_H_
#define CAER_SDK_PORTABLE_THREADS_H_

#ifdef __cplusplus

#include <cstdlib>

#else

#include <stdbool.h>
#include <stdlib.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

bool portable_thread_set_name(const char *name);
bool portable_thread_set_priority_highest(void);

#ifdef __cplusplus
}
#endif

#endif /* CAER_SDK_PORTABLE_THREADS_H_ */
