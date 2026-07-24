/******************************************************************************
 * button.h
 ******************************************************************************/

#ifndef BUTTON_H
#define BUTTON_H

#include "utils.h"

#define NUM_BUTTONS     5

//------------------------------------------------------------------------------

enum
{
    BUTTON0,
    BUTTON1,
    BUTTON2,
    BUTTON3,
    BUTTON4,
    NO_BUTTON = 0xFF,
};

//------------------------------------------------------------------------------

extern debounce_t g_x_button[NUM_BUTTONS];

//------------------------------------------------------------------------------

void v_button_init(void);
void v_button_debounce_service(void);
uint8_t u8_get_debounced_button_state(uint8_t u8_button_id);

#endif
