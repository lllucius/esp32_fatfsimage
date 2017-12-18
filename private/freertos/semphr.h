#if !defined(__SEMPHR_H__)
#define __SEMPHR_H__

// Do not need to be reentrant and we don't have the FreeRTOS semaphore
// functions, so disable the option
#undef FF_FS_REENTRANT
#define FF_FS_REENTRANT 0

#endif

