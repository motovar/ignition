/*
 * race/debug.h - the race's debug lines (race/debug.c).
 */

#ifndef IGN_RACE_DEBUG_H
#define IGN_RACE_DEBUG_H

#include "types.h"

/* 0x4949b4: the frame rate on; the port's 2, all the debug lines. */
extern s32 r_4949b4;

/* 0x43e9c0: the lines, over the race's screen. */
void race_debug_lines(void);

/* 0x43ec70: the replay camera's numbers, when r_4949bc is on. */
void race_replay_numbers(void);

#endif /* IGN_RACE_DEBUG_H */
