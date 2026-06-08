#pragma once
#include "psu_types.h"
#include "flash_store.h"

/* Runs the blocking calibration menu.
 * The caller must pass a function pointer to redraw the current mode's
 * static screen when the menu exits, so the menu is decoupled from display.c */
typedef void (*draw_static_fn_t)(void);

bool cal_menu_run(draw_static_fn_t redraw_fn);
