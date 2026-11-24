/*
 * dof_pipe.h - --dof: the game's end of the pipe to the output helper.
 */
#pragma once

int  dof_open(const char *helper, const char *savedir);      /* 0: cannot, and has said why */
void dof_tick(void);                 /* each host_frame; never waits */
void dof_close(void);                /* the game is over; idempotent */

#ifdef _WIN32
#define DOF_HELPER "dof/build/epicpinball-dof.exe"
#else
#define DOF_HELPER "dof/build/epicpinball-dof"
#endif
