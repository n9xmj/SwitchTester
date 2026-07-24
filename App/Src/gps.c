/*******************************************************************************
 * gps.c
 *******************************************************************************/

#include "device_config.h"
#include "uart_int.h"
#include "gps.h"

char c_gps_data[GPS_DATA_BUFFER_SIZE];

gps_info_t g_x_gps_info;

//------------------------------------------------------------------------------

/*******************************************************************************
 *
 *******************************************************************************/

bool b_gps_get_data(void)
{
    static uint16_t u16_index;
    static uint8_t u8_state;
    uint16_t u16_char;
    uint8_t u8_char;
    uint8_t u8_status;
    bool b_got_msg = false;

    do
    {
        u8_status = u8_uart_rx(&GPS_UART_HANDLE, &u16_char, 0);
        if (u8_status)
        {
            return false;
        }

        u8_char = (uint8_t) u16_char & 0x7F;
        if (u8_state == 0)
        {
            if (u16_char == '$')
            {
                u16_index = 0;
                u8_state = 1;
            }
        }
        if (u8_state == 1)
        {
            if (u8_char == ',')
            {
                if ( (u16_index > 0) && (c_gps_data[u16_index-1] == ',') )
                {
                    c_gps_data[u16_index] = '0';
                    u16_index++;
                }
            }
            if (u8_char >= 0x20)
            {
                c_gps_data[u16_index] = u8_char;
                u16_index++;
            }
            else
            {
                if (u8_char == '\r')
                {
                    c_gps_data[u16_index] = 0;
                    b_got_msg = true;
                    u8_state = 0;
                    printf("%s\r\n", c_gps_data);
//                    b_gps_parse_data(c_gps_data);
                }
            }
        }
    }
    while (!b_got_msg && (u8_status == 0));

    return b_got_msg;
}

/*******************************************************************************
 *
 *******************************************************************************/

void v_gps_print_info(gps_info_t *p_x_gps_info)
{
    printf("Time    : %06.4lf\r\n"
           "Lat     : %04.4lf %c\r\n"
           "Long    : %05.4lf %c\r\n"
           "Pos Fix : %u\r\n"
           "Sats    : %u\r\n"
           "HDOP    : %1.2f\r\n"
           "Alt     : %1.2f %c\r\n"
           "Geo Sep : %1.1f %c\r\n"
           ,p_x_gps_info->d_utc_time
           ,p_x_gps_info->d_latitude, p_x_gps_info->c_latitude_ns
           ,p_x_gps_info->d_longitude, p_x_gps_info->c_longitude_ew
           ,p_x_gps_info->ui_position_fix
           ,p_x_gps_info->ui_satellites
           ,p_x_gps_info->f_hdop
           ,p_x_gps_info->f_altitude, p_x_gps_info->c_altitude_units
           ,p_x_gps_info->f_geoidal_separation, p_x_gps_info->c_geoidal_separation_units
          );
}

/*******************************************************************************
 *
 *******************************************************************************/

bool b_gps_parse_data(char *p_c_message, gps_info_t *p_x_gps_info)
{
    memset(&g_x_gps_info, 0, sizeof(gps_info_t));

    int i_ret =
    sscanf(p_c_message, "$GNGGA,%lf,%lf,%c,%lf,%c,%u,%u,%f,%f,%c,%f,%c,%*c,*%x"
           ,&p_x_gps_info->d_utc_time
           ,&p_x_gps_info->d_latitude
           ,&p_x_gps_info->c_latitude_ns
           ,&p_x_gps_info->d_longitude
           ,&p_x_gps_info->c_longitude_ew
           ,&p_x_gps_info->ui_position_fix
           ,&p_x_gps_info->ui_satellites
           ,&p_x_gps_info->f_hdop
           ,&p_x_gps_info->f_altitude
           ,&p_x_gps_info->c_altitude_units
           ,&p_x_gps_info->f_geoidal_separation
           ,&p_x_gps_info->c_geoidal_separation_units
           ,&p_x_gps_info->ui_checksum);

    if (i_ret == 13)
    {
        v_gps_print_info(p_x_gps_info);
    }
    else
    {
//      printf("i_ret = %d\r\n", i_ret);
    }

    return (i_ret == 13);
}
