#ifndef STOPWATCH_H
#define STOPWATCH_H

#include <stdint.h>
#include <stdbool.h>
#include <lvgl/lvgl.h>

/* Open the stopwatch app UI and display it. */

void stopwatch_start(void);

/* Control functions for MCP voice control.
 * These operate on the stopwatch state; the UI must already
 * be visible (call stopwatch_start first if needed).
 */

void stopwatch_pause(void);
void stopwatch_resume(void);
void stopwatch_reset(void);

#endif

