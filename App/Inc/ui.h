/*******************************************************************************
 * ui.h
 *******************************************************************************/

#ifndef UI_H
#define UI_H

#include "gps.h"

extern void v_ui_handler(uint8_t u8_button_pressed);
extern void v_ui_display_gps_info(gps_info_t *p_gps_info);

#endif
