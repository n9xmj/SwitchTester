/*******************************************************************************
 * ui.h
 *******************************************************************************/

#include <stdlib.h>
#include <math.h>

#include "device_config.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "button.h"
#include "nvmparams.h"
#include "ui.h"

//------------------------------------------------------------------------------


/*******************************************************************************
 *
 *******************************************************************************/

#if 0
void v_ui_param_display(void)
{
    uint8_t y = 0;
    char c_marker = ' ';
    char c_display_str[40];

    ssd1306_Fill(Black);

    uint8_t u8_period_sec = u16_ui_period / 10;
    uint8_t u8_period_tenths = u16_ui_period % 10;
    c_marker = (u8_param_selected == 1) ? '>' : ' ';
    //                      .Period    1.0 sec
    sprintf(c_display_str, "%cPeriod%5u.%u sec", c_marker, u8_period_sec, u8_period_tenths);
    ssd1306_SetCursor(2, y);
    ssd1306_WriteString(c_display_str, Font_7x10, White);
    y += 11;

    c_marker = (u8_param_selected == 2) ? '>' : ' ';
    //                      .Swing   +/-30 deg
    sprintf(c_display_str, "%cSwing   +/-%u deg", c_marker, u16_ui_amplitude);
    ssd1306_SetCursor(2, y);
    ssd1306_WriteString(c_display_str, Font_7x10, White);
    y += 11;

    ssd1306_SetCursor(2, y);
    ssd1306_WriteString(" Y:Sel G:Down B:Up", Font_7x10, White);

    ssd1306_UpdateScreen();
}
#endif

/*******************************************************************************
 *
 *******************************************************************************/

#if 0
void v_ui_display_running(void)
{
    ssd1306_Fill(Black);
    ssd1306_DrawRectangle(0,0, 127,31, White);
    ssd1306_SetCursor(25,6);
    ssd1306_WriteString("Running", Font_11x18, White);
    ssd1306_UpdateScreen();
}

void v_ui_display_stopping(void)
{
    ssd1306_Fill(Black);
    ssd1306_DrawRectangle(0,0, 127,31, White);
    ssd1306_SetCursor(20,6);
    ssd1306_WriteString("Stopping", Font_11x18, White);
    ssd1306_UpdateScreen();
}
#endif

/*******************************************************************************
 *
 *******************************************************************************/

void v_ui_display_gps_info(gps_info_t *p_x_gps_info)
{
    ldiv_t x_div;
    uint32_t u32_time;
    uint8_t y = 0;
//    char c_marker = ' ';
    char c_display_str[40];

    u32_time = (uint32_t) p_x_gps_info->d_utc_time;
    x_div = ldiv(u32_time, 10000);
    uint8_t u8_hour = (uint8_t) x_div.quot;
    x_div = ldiv(x_div.rem, 100);
    uint8_t u8_min = (uint8_t) x_div.quot;
    uint8_t u8_sec = (uint8_t) x_div.rem;

    ssd1306_Fill(Black);

    sprintf(c_display_str, "UTC %02u:%02u:%02u", u8_hour, u8_min, u8_sec);
    ssd1306_SetCursor(0, y);
    ssd1306_WriteString(c_display_str, Font_7x10, White);
    y += 11;

    sprintf(c_display_str, "Fix:%u Sats:%u",
            p_x_gps_info->ui_position_fix,
            p_x_gps_info->ui_satellites);
    ssd1306_SetCursor(0, y);
    ssd1306_WriteString(c_display_str, Font_7x10, White);
    y += 11;

    double d_int;
    double d_ll_sec = modf(p_x_gps_info->d_latitude, &d_int);
    d_ll_sec *= 60.0F;
    uint32_t u32_deg_min = (uint32_t) d_int;
    x_div = ldiv(u32_deg_min, 100);
    uint8_t u8_ll_deg = (uint8_t) x_div.quot;
    uint8_t u8_ll_min = (uint8_t) x_div.rem;

    sprintf(c_display_str, "Lat:%02u:%02u:%02.2lf%c",
            u8_ll_deg, u8_ll_min, d_ll_sec,
            p_x_gps_info->c_latitude_ns);
    ssd1306_SetCursor(0, y);
    ssd1306_WriteString(c_display_str, Font_7x10, White);
    y += 11;

    d_ll_sec = modf(p_x_gps_info->d_longitude, &d_int);
    d_ll_sec *= 60.0F;
    u32_deg_min = (uint32_t) d_int;
    x_div = ldiv(u32_deg_min, 100);
    u8_ll_deg = (uint8_t) x_div.quot;
    u8_ll_min = (uint8_t) x_div.rem;

    sprintf(c_display_str, "Lon:%02u:%02u:%02.2lf%c",
            u8_ll_deg, u8_ll_min, d_ll_sec,
            p_x_gps_info->c_longitude_ew);
    ssd1306_SetCursor(0, y);
    ssd1306_WriteString(c_display_str, Font_7x10, White);
    y += 11;

    sprintf(c_display_str, "Alt %1.2f%c",
            p_x_gps_info->f_altitude, p_x_gps_info->c_altitude_units);
    ssd1306_SetCursor(0, y);
    ssd1306_WriteString(c_display_str, Font_7x10, White);
    y += 11;

    ssd1306_UpdateScreen();
}

/*******************************************************************************
 *
 *******************************************************************************/

void v_ui_handler(uint8_t u8_button_pressed)
{
}
