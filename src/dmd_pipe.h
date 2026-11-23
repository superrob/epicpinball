/*
 * dmd_pipe.h - --dmd-device: the DMD picture (dmd.c) to a real DMD through
 * dmd/build/epicpinball-dmd, the GPL helper that owns libdmdutil (dmd/).
 */
#pragma once
#include <stdint.h>

#define DMD_HELPER "dmd/build/epicpinball-dmd"

int  dmd_pipe_open(const char *helper, const char *savedir);  /* 0: said why, and off */
void dmd_pipe_frame(const uint32_t *px);   /* 128x32 0xAARRGGBB, a picture that changed */
void dmd_pipe_tick(void);                  /* each host_frame: a frame that did not fit goes now */
void dmd_pipe_close(void);
int  dmd_pipe_alive(void);                 /* a helper is taking frames */
