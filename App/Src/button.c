/******************************************************************************
 * button.c
 ******************************************************************************/

#include "device_config.h"              // Includes debug_config.h, main.h, macros.h
#include "jobs.h"
#include "button.h"

//------------------------------------------------------------------------------

debounce_t g_x_button[NUM_BUTTONS];

/******************************************************************************
 *
 ******************************************************************************/

static uint8_t u8_button0_level(void)
{
    return NUCLEO_BUTTON_PRESSED();
}

static uint8_t u8_button1_level(void)
{
    return SWITCH1_PRESSED();
}

static uint8_t u8_button2_level(void)
{
    return SWITCH2_PRESSED();
}

static uint8_t u8_button3_level(void)
{
    return SWITCH3_PRESSED();
}

static uint8_t u8_button4_level(void)
{
    return SWITCH4_PRESSED();
}

/******************************************************************************
 *
 ******************************************************************************/

void v_button_init(void)
{
    v_debounce_init(&g_x_button[BUTTON0], &u8_button0_level, 5);
    v_debounce_init(&g_x_button[BUTTON1], &u8_button1_level, 5);
    v_debounce_init(&g_x_button[BUTTON2], &u8_button2_level, 5);
    v_debounce_init(&g_x_button[BUTTON3], &u8_button3_level, 5);
    g_x_button[BUTTON3].u8_auto_repeat_delay = 75;
    g_x_button[BUTTON3].u8_auto_repeat_interval = 10;
    g_x_button[BUTTON3].x_auto_repeat_mode = DEBOUNCE_AUTO_REPEAT_HIGH;
    v_debounce_init(&g_x_button[BUTTON4], &u8_button4_level, 5);
    g_x_button[BUTTON4].u8_auto_repeat_delay = 75;
    g_x_button[BUTTON4].u8_auto_repeat_interval = 10;
    g_x_button[BUTTON4].x_auto_repeat_mode = DEBOUNCE_AUTO_REPEAT_HIGH;
}

/******************************************************************************
 *
 ******************************************************************************/

void v_button_debounce_service(void)
{
    debounced_level_t x_debounced_state;

    for (uint8_t u8_index = 0; u8_index < NUM_BUTTONS; u8_index++)
    {
        x_debounced_state = x_debounce(&g_x_button[u8_index]);
        if ( (x_debounced_state == DEBOUNCED_LEVEL_CHANGED_HIGH)
             || (x_debounced_state == DEBOUNCED_ACTIVE_LEVEL_REPEAT) )
        {
            v_job_add_with_params(NULL, JOB_BUTTON, u8_index, 0);
        }
    }
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_get_debounced_button_state(uint8_t u8_button_id)
{
    if (u8_button_id < NUM_BUTTONS)
    {
        return g_x_button[u8_button_id].u8_debounced_level;
    }
    else
    {
        return 0;
    }
}
