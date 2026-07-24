/*******************************************************************************
 * gps.h
 *******************************************************************************/

#ifndef GPS_H
#define GPS_H

#define GPS_DATA_BUFFER_SIZE    128

typedef struct
{
    double d_utc_time;
    double d_latitude;
    char c_latitude_ns;
    double d_longitude;
    char c_longitude_ew;
    unsigned int ui_position_fix;
    unsigned int ui_satellites;
    float f_hdop;
    float f_altitude;
    char c_altitude_units;
    float f_geoidal_separation;
    char c_geoidal_separation_units;
    unsigned int ui_checksum;
}
gps_info_t;

//------------------------------------------------------------------------------

extern char c_gps_data[GPS_DATA_BUFFER_SIZE];

extern gps_info_t g_x_gps_info;

//------------------------------------------------------------------------------

extern bool b_gps_get_data(void);
extern void v_gps_print_info(gps_info_t *p_x_gps_info);
extern bool b_gps_parse_data(char *p_c_message, gps_info_t *p_x_gps_info);

#endif
