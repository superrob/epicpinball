/*
 * msvc-gettimeofday.h - for libftdi built with Visual Studio: dof/build.ps1
 * has libftdi-msvc.cmake force-include it in libftdi's C, nowhere else.
 *
 * ftdi_stream.c times its progress reports with gettimeofday(), which MinGW
 * has and MSVC does not; libdof never calls that code, but the DLL must link.
 */
#pragma once
#ifdef _MSC_VER
#include <winsock2.h>                /* struct timeval; before windows.h, which libusb.h includes */

static __inline int gettimeofday(struct timeval *tv, void *tz)
{
    FILETIME ft;
    unsigned __int64 t;
    (void)tz;
    GetSystemTimeAsFileTime(&ft);
    t = ((unsigned __int64)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10 - 11644473600000000ULL;
    tv->tv_sec = (long)(t / 1000000);
    tv->tv_usec = (long)(t % 1000000);
    return 0;
}
#endif
