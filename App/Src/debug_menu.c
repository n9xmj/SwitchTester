/******************************************************************************
 *
 ******************************************************************************/

#include <machine/endian.h>

#include "device_config.h"
#include "debug_config.h"

#if defined(DEBUG) && (DEBUG_MENU != 0)

#include "ansi.h"
#include "menusystem.h"
#include "debug_menu.h"
#include "utils.h"
#include "jobs.h"

#include "led.h"
#include "nvmparams.h"
#include "uart_int.h"

#include "ssd1306.h"
#include "ssd1306_tests.h"
#include "ssd1306_fonts.h"

typedef enum
{
    PARAM_INT32             = -4,
    PARAM_INT16             = -2,
    PARAM_INT8              = -1,
    PARAM_UINT8             = 1,
    PARAM_UINT16            = 2,
    PARAM_UINT32            = 4,
}
paramsize_t;

typedef struct
{
    const char *p_c_name;       // Text to print when listing params; description
    uint8_t u8_reserved;        // (alignment)
    const int8_t i8_size;       // Use negative values for signed integer types
                                // See paramsize_t enum definition above
    const bool b_end_of_list;   // Marks last entry in the list
    const bool b_hex_display;   // Show parameter value in hexadecimal format
    union                       // Pointer to value, or register address, or index
    {
        uint8_t u8_value;
        uint16_t u16_value;
        uint32_t u32_value;

        void *v_value_ptr;
        bool *b_value_ptr;
        int8_t *i8_value_ptr;
        uint8_t *u8_value_ptr;
        int16_t *i16_value_ptr;
        uint16_t *u16_value_ptr;
        int32_t *i32_value_ptr;
        uint32_t *u32_value_ptr;
    };
    union                   // Default value for parameter
    {
        const int32_t i_default;
        const uint32_t u_default;
    };
    const uint16_t u16_nvm_id;  // NVMParams API parameter ID
}
paramlist_t;

/******************************************************************************
 *
 ******************************************************************************/

extern uint32_t u32_test_param_1;

const paramlist_t x_pendulum_paramlist[] =
{
    {   // 0
        .p_c_name = "Placeholder parameter",
        .i8_size = sizeof(u32_test_param_1),
        .u32_value_ptr = &u32_test_param_1,
        .u_default = 0xDEAD,
        .u16_nvm_id = NVM_PARAM_TEST_1,
        .b_hex_display = false,
        .b_end_of_list = true,
    },
};

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_input_test(void)
{
    char c_entry[12];
    int i_len;
    int32_t i32_value = 1000;

    printf("Nonblocking input test\r\n"
           "Enter a value (int,default %li):", i32_value);
    i_len = i_getline(c_entry, sizeof(c_entry)-1);
    v_newline();
    if (i_len < 0)
    {
        printf("Got [ESC], value not changed\r\n");
    }
    else if (i_len == 0)
    {
        printf("Nothing entered, value not changed\r\n");
    }
    else
    {
        i32_value = strtol(c_entry, NULL, 0);
    }

    printf("String entered: \"%s\"\r\n"
           "int32 value   : %li\r\n",
           c_entry, i32_value);
}

/******************************************************************************
 *
 ******************************************************************************/

uint16_t u16_param_list_length(const paramlist_t *p_x_paramlist)
{
    uint16_t u16_length = 0;

    while (! p_x_paramlist[u16_length].b_end_of_list)
    {
        u16_length++;
    }
    u16_length++;

    return u16_length;
}

/******************************************************************************
 *
 ******************************************************************************/

int32_t i32_get_param_value(const paramlist_t *p_x_paramlist, uint8_t u8_param)
{
    if (u8_param >= u16_param_list_length(p_x_paramlist))
    {
        return 0x80000000;
    }

    int8_t i8_size = p_x_paramlist[u8_param].i8_size;
    int32_t i32_value = 0;

    switch (i8_size)
    {
        case PARAM_UINT8:               // 1
            i32_value = (int32_t) (uint32_t) *p_x_paramlist[u8_param].u8_value_ptr;
            break;
        case PARAM_INT8:                // -1
            i32_value = (int32_t) *p_x_paramlist[u8_param].i8_value_ptr;
            break;
        case PARAM_UINT16:              // 2
            i32_value = (int32_t) (uint32_t) *p_x_paramlist[u8_param].u16_value_ptr;
            break;
        case PARAM_INT16:               // -2
            i32_value = (int32_t) *p_x_paramlist[u8_param].i16_value_ptr;
            break;
        case PARAM_UINT32:              // 4
            i32_value = (int32_t) *p_x_paramlist[u8_param].u32_value_ptr;
            break;
        case PARAM_INT32:               // -4
            i32_value = (int32_t) *p_x_paramlist[u8_param].i32_value_ptr;
            break;
        default:
            i32_value = (int32_t) p_x_paramlist[u8_param].v_value_ptr;
            break;
    }

    return i32_value;
}

/******************************************************************************
 *
 ******************************************************************************/

void v_set_param_value(const paramlist_t *p_x_paramlist, uint8_t u8_param, int32_t i32_value)
{
    if (u8_param >= u16_param_list_length(p_x_paramlist))
    {
        return;
    }

    int8_t i8_size = p_x_paramlist[u8_param].i8_size;

    switch (i8_size)
    {
        case PARAM_UINT8:               // 1
            *p_x_paramlist[u8_param].u8_value_ptr = (uint8_t) i32_value;
            break;
        case PARAM_INT8:                // -1
            *p_x_paramlist[u8_param].i8_value_ptr = (int8_t) i32_value;
            break;
        case PARAM_UINT16:              // 2
            *p_x_paramlist[u8_param].u16_value_ptr = (uint16_t) i32_value;
            break;
        case PARAM_INT16:               // -2
            *p_x_paramlist[u8_param].i16_value_ptr = (int16_t) i32_value;
            break;
        case PARAM_UINT32:              // 4
            *p_x_paramlist[u8_param].u32_value_ptr = (uint32_t) i32_value;
            break;
        case PARAM_INT32:               // -4
            *p_x_paramlist[u8_param].i32_value_ptr = i32_value;
            break;
        default:
            break;
    }

    // Exceptions - params that need extra work to set
#if 0
    switch (p_x_paramlist[u8_param].u16_nvm_id)
    {
        case NVM_PARAM_DF100_AUTOCAL:
            x_df100_write_sensor_autocal(I2C_DEVICE_DF100_TOP, (uint16_t) i32_value);
            break;
        case NVM_PARAM_DF100_INTRTHR:
            x_df100_write_sensor_intrthr(I2C_DEVICE_DF100_TOP, (uint16_t) i32_value);
            break;
        default:
            break;
    }
#endif

    // Save parameter to NVM if applicable
    if (p_x_paramlist[u8_param].u16_nvm_id)
    {
        x_nvm_set(&g_x_nvm_param, p_x_paramlist[u8_param].u16_nvm_id, p_x_paramlist[u8_param].v_value_ptr);
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_list_parameters(const paramlist_t *p_x_paramlist)
{
   uint8_t u8_index = 0;
   char c_key;
   int32_t i32_value;

   do
   {
       i32_value = i32_get_param_value(p_x_paramlist, u8_index);
       c_key = (u8_index < 10)
               ? '0' + u8_index
               : 'a' + u8_index - 10;
       printf("[%c] %s (", c_key, p_x_paramlist[u8_index].p_c_name);
       if (p_x_paramlist[u8_index].b_hex_display)
       {
           printf("0x%02lX", i32_value);
       }
       else
       {
           printf("%ld", i32_value);
       }
       printf(")\r\n");
       if (p_x_paramlist[u8_index].b_end_of_list)
       {
           break;
       }
       u8_index++;
   }
   while (1);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_enter_parameter(const paramlist_t *p_x_paramlist, char c_key, uint8_t u8_param)
{
    char c_entry[12];
    int i_len;
    int32_t i32_value;

    if (u8_param >= u16_param_list_length(p_x_paramlist))
    {
        return;
    }

    i32_value = i32_get_param_value(p_x_paramlist, u8_param);
    printf("[%c] %s (", c_key, p_x_paramlist[u8_param].p_c_name);
    if (p_x_paramlist[u8_param].b_hex_display)
    {
        printf("0x%02lX", i32_value);
    }
    else
    {
        printf("%ld", i32_value);
    }
    printf(") '*'=default : ");

    i_len = i_getline(c_entry, sizeof(c_entry)-1);
    if (i_len < 1)
    {
        printf("Value not changed\r\n");
    }
    else if ( (i_len == 1) && (c_entry[0] == '*') )
    {
        i32_value = p_x_paramlist[u8_param].i_default;
        printf("Setting to default: %ld\r\n", i32_value);
        v_set_param_value(p_x_paramlist, u8_param, i32_value);
    }
    else
    {
        i32_value = strtol(c_entry, NULL, 0);
        v_set_param_value(p_x_paramlist, u8_param, i32_value);
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_list_parameters(void)
{
    v_list_parameters(x_pendulum_paramlist);
    v_newline();
}

void v_debug_enter_parameter(char c_key, uint8_t u8_param)
{
    v_enter_parameter(x_pendulum_paramlist, c_key, u8_param);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_erase_nvm(void)
{
    printf("--- ERASING NVM & RESETTING ---\r\n");
    memset(g_x_nvm_param.p_v_data, 0xFF, g_x_nvm_param.u32_size);
    x_nvm_write(&g_x_nvm_param);
    HAL_NVIC_SystemReset();
}

/******************************************************************************
 *
 ******************************************************************************/

// Use v_debug_delay() for blocking delays in cases where it's OK to perform the
// debug menu service while the delay is in progress

// Note: This is the same code used in HAL_Delay, except it calls the debug menu
// service while waiting for delay completion.

void v_debug_delay(uint32_t u32_delay)
{
    uint32_t u32_tickstart = HAL_GetTick();
    uint32_t u32_wait = u32_delay;

    /* Add a freq to guarantee minimum wait */
    if (u32_wait < HAL_MAX_DELAY)
    {
        u32_wait += (uint32_t)(uwTickFreq);
    }

    while (ELAPSED_TIME(u32_tickstart) < u32_wait)
    {
        #if DEBUG_MENU
        v_debug_menu_service();
        #endif
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_hexdump(void *p_v_buffer, uint16_t u16_bufsize)
{
    uint8_t u8_data;
    uint16_t u16_base;
    uint16_t u16_offset;
    uint16_t u16_index;

    u16_offset = 0;
    for (u16_base = 0; u16_base < u16_bufsize; u16_base += u16_offset)
    {
        printf("%04X :", u16_base);
        for (u16_offset = 0; u16_offset < 8; u16_offset++)
        {
            u16_index = u16_base + u16_offset;
            if (u16_index >= u16_bufsize)
            {
                break;
            }
            u8_data = ((uint8_t *) p_v_buffer)[u16_index];
            printf(" %02X", u8_data);
        }
        printf("\r\n");
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_blue_led_toggle(void)
{
    v_led_blue_toggle();
}

void v_debug_red_led_toggle(void)
{
    v_led_red_toggle();
}

void v_debug_debug_led_cycle(void)
{
    uint16_t u16_duty;

    u16_duty = __HAL_TIM_GET_COMPARE(&LED_TIMER_HANDLE, DEBUG_LED_CHANNEL);
    u16_duty += LED_PWM_MAX_DUTY / 4;
    if (u16_duty > LED_PWM_MAX_DUTY) u16_duty = 0;
    v_led_debug_pwm(u16_duty);
}

void v_debug_nucleo_led_toggle(void)
{
    if (NUCLEO_LED_OUT_LEVEL())
    {
        NUCLEO_LED_CLEAR();
    }
    else
    {
        NUCLEO_LED_SET();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_ssd1306_test(void)
{
    printf("SSD1306 I2C display test started.\r\n");
    ssd1306_TestAll();
}

void v_debug_display_clear(void)
{
    ssd1306_Fill(Black);
    ssd1306_UpdateScreen();
}

void v_debug_display_text(void)
{
    uint8_t y = 0;
    uint32_t u32_render_timestamp = HAL_GetTick();

    ssd1306_Fill(Black);

    ssd1306_SetCursor(2, y);
    ssd1306_WriteString("The Quick Brown", Font_7x10, White);
    y += 11;
    ssd1306_SetCursor(2, y);
    ssd1306_WriteString("Fox Jumps Over", Font_7x10, White);
    y += 11;
    ssd1306_SetCursor(2, y);
    ssd1306_WriteString("The Lazy Dog", Font_7x10, White);

    uint32_t u32_refresh_timestamp = HAL_GetTick();
    ssd1306_UpdateScreen();
    uint32_t u32_tick = HAL_GetTick();
    u32_refresh_timestamp = u32_tick - u32_refresh_timestamp;
    u32_render_timestamp = u32_tick - u32_render_timestamp;
    printf("Display refresh time: %lu mS\r\n"
           "Total render time:    %lu mS\r\n",
           u32_refresh_timestamp,
           u32_render_timestamp);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_gps_terminal(void)
{
    uint32_t u32_timestamp = HAL_GetTick();
    uint16_t u16_rx_count;
    uint8_t u8_status;
    bool b_done = false;
    uint8_t rxbuf[16];

    printf("GPS terminal - [ESC] to exit\r\n");

    while (!b_done)
    {
        u16_rx_count = sizeof(rxbuf);
        u8_status = u8_uart_rx_multi(&GPS_UART_HANDLE, rxbuf, &u16_rx_count, 1);
        if ((u8_status < 2) && (u16_rx_count > 0))
        {
            u8_uart_tx_multi(&DEBUG_UART_HANDLE, rxbuf, &u16_rx_count, 10);
        }

        u16_rx_count = sizeof(rxbuf);
        u8_status = u8_uart_rx_multi(&DEBUG_UART_HANDLE, rxbuf, &u16_rx_count, 1);
        if ((u8_status < 2) && (u16_rx_count > 0))
        {
            if ( (u16_rx_count == 1)
                 && (rxbuf[0] == 0x1B)
                 && (ELAPSED_TIME(u32_timestamp) >= 1000) )
            {
                b_done = true;
            }
            else
            {
                u32_timestamp = HAL_GetTick();
                u8_uart_tx_multi(&GPS_UART_HANDLE, rxbuf, &u16_rx_count, 10);
            }
        }
    }

    printf("\r\nGPS terminal exit\r\n");
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_no_operation(void)
{
    printf("Did nothing.\r\n");
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_mcu_reset(void)
{
    printf("Resetting MCU...\r\n");
    x_nvm_commit(&g_x_nvm_param);
    HAL_Delay(100);
    HAL_NVIC_SystemReset();
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_watchdog_test(void)
{
    uint32_t u32_timestamp;
    uint32_t u32_elapsed;
    uint16_t u16_count = 0;
    int i_key;

    printf("Waiting for IWDG reset...\r\n");
    u32_timestamp = HAL_GetTick();
    do
    {
        printf("%u\r", u16_count);
        do
        {
            u32_elapsed = ELAPSED_TIME(u32_timestamp);
        }
        while (u32_elapsed < 1000);
        u32_timestamp += 1000;
        u16_count++;
        i_key = getchar();
    }
    while (i_key != 0x1B);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_show_clocks(void)
{
    uint32_t u32_sysclk_freq = HAL_RCC_GetSysClockFreq();
    uint32_t u32_hclk_freq = HAL_RCC_GetHCLKFreq();
    uint32_t u32_pclk1_freq = HAL_RCC_GetPCLK1Freq();

    printf("SYSCLK freq: %lu\r\n"
           "HCLK freq  : %lu\r\n"
           "PCLK1 freq : %lu\r\n",
           u32_sysclk_freq,
           u32_hclk_freq,
           u32_pclk1_freq);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_show_gpio(void)
{
#if 0
    printf("[O] MTR_STEP     %u\r\n"
           "[O] MTR_DIR      %u\r\n"
           "[O] MTR_EN       %u\r\n"
           "[O] MTR_NRESET   %u\r\n"
           "[O] MTR_NSLEEP   %u\r\n"
           "[O] MTR_MS1      %u\r\n"
           "[O] MTR_MS2      %u\r\n"
           "[O] MTR_MS3      %u\r\n"
           "[I] MTR_INDEX    %u\r\n"
           "[I] MTR_FAULT    %u\r\n"
           "[O] TMR_EN       %u\r\n"
           "[I] TMR_INT      %u\r\n"
           "[I] SWITCH1_INT  %u\r\n"
           "[I] SWITCH2_INT  %u\r\n"
           "[I] SWITCH3_INT  %u\r\n"
           "[I] SWITCH4_INT  %u\r\n"
           "[I] NUCLEO_BTN   %u\r\n"
           ,(uint16_t) MTR_STEP_IN_LEVEL()
           ,(uint16_t) MTR_DIR_IN_LEVEL()
           ,(uint16_t) MTR_EN_IN_LEVEL()
           ,(uint16_t) MTR_NRESET_IN_LEVEL()
           ,(uint16_t) MTR_NSLEEP_IN_LEVEL()
           ,(uint16_t) MTR_MS1_IN_LEVEL()
           ,(uint16_t) MTR_MS2_IN_LEVEL()
           ,(uint16_t) MTR_MS3_IN_LEVEL()
           ,(uint16_t) MTR_INDEX_LEVEL()
           ,(uint16_t) MTR_FAULT_LEVEL()
           ,(uint16_t) TMR_EN_IN_LEVEL()
           ,(uint16_t) TMR_INT_LEVEL()
           ,(uint16_t) SWITCH1_INT_LEVEL()
           ,(uint16_t) SWITCH2_INT_LEVEL()
           ,(uint16_t) SWITCH3_INT_LEVEL()
           ,(uint16_t) SWITCH4_INT_LEVEL()
           ,(uint16_t) NUCLEO_BUTTON_LEVEL()
          );
#endif
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_endian_check(void)
{
    uint32_t u32_value = 0x12345678;
    uint32_t u32_netvalue;
    uint8_t *p_u8_ptr = (uint8_t *) &u32_value;

    printf("Byte ordering test: 0x%08lX = [0]:%02X [1]:%02X [2]:%02X [3]:%02X\r\n",
           u32_value, p_u8_ptr[0], p_u8_ptr[1], p_u8_ptr[2], p_u8_ptr[3]);

    u32_netvalue = __htonl(u32_value);
    printf("Original:%08lx htonl:%08lx\r\n", u32_value, u32_netvalue);

    printf("According to GCC, this processor is a "
#if LITTLE_ENDIAN
           "LITTLE"
#elif BIG_ENDIAN
           "BIG"
#else
           "UNKNOWN"
#endif
            "-endian machine\r\n");
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_quick_test(void)
{
    printf("Quick test function\r\n");
}

#if 0
//------------------------------------------------------------------------------
// Submenu: Example to show how a submenu is set up
// Use this as a template to set up your own submenu(s)
//------------------------------------------------------------------------------

static const menu_item_t x_example_menu[] =
{
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "\r\n--- Submenu Example ---\r\n"
    },
    {
        .item_type = MENU_ITEM_HELP,
        .key = '?',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key = '\r',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'x',
        .text = "No operation",
        .function = v_debug_no_operation
    },
    {
        .item_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .key = 0x1B,
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_END_OF_LIST,
    }
};
#endif

//------------------------------------------------------------------------------
// Submenu: Parameters
//------------------------------------------------------------------------------

static const menu_item_t x_param_menu[] =
{
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "\r\n--- Parameters ---\r\n"
    },
    {
        .item_type = MENU_ITEM_HELP,
        .key = '?',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key = '\r',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_HELP_TEXT_VARIABLE,
        .key = 0,
        .help_text_function = v_debug_list_parameters,
        .text = "\r\nParameter selection:\r\n"
    },
    {
        .item_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .key = 0,
        .text = "0123456789abcdef",
        .key_list_function = v_debug_enter_parameter
    },
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "Other operations:\r\n"
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'z',
        .text = "Input test",
        .function = v_debug_input_test
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '*',
        .text = "ERASE & DEFAULT NVM SETTINGS (resets MCU)",
        .function = v_debug_erase_nvm
    },
    {
        .item_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .key = 0x1B,
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_END_OF_LIST,
    }
};

//------------------------------------------------------------------------------
// Submenu: LED / indication tests
//------------------------------------------------------------------------------

static const menu_item_t x_led_ind_menu[] =
{
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "\r\n--- LED/Indication Tests ---\r\n"
    },
    {
        .item_type = MENU_ITEM_HELP,
        .key = '?',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key = '\r',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = ',',
        .text = "BLUE LED toggle",
        .function = v_debug_blue_led_toggle
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '.',
        .text = "RED LED toggle",
        .function = v_debug_red_led_toggle
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '/',
        .text = "DEBUG LED cycle",
        .function = v_debug_debug_led_cycle
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = ';',
        .text = "Nucleo LED toggle",
        .function = v_debug_nucleo_led_toggle
    },
    {
        .item_type = MENU_ITEM_END_OF_LIST,
    }
};

//------------------------------------------------------------------------------
// Submenu: SSD1306 display tests
//------------------------------------------------------------------------------

static const menu_item_t x_ssd1306_display_menu[] =
{
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "\r\n--- SSD1306 display test menu ---\r\n"
    },
    {
        .item_type = MENU_ITEM_HELP,
        .key = '?',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key = '\r',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'd',
        .text = "Run SSD1306 API test routine",
        .function = v_debug_ssd1306_test
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'c',
        .text = "Clear display",
        .function = v_debug_display_clear
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 't',
        .text = "Text display test",
        .function = v_debug_display_text
    },
    {
        .item_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .key = 0x1B,
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_END_OF_LIST,
    }
};

//------------------------------------------------------------------------------
// Debug Main Menu
//------------------------------------------------------------------------------

static const menu_item_t x_debug_top_menu[] =
{
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "\r\n--- " PRODUCT_NAME " v" FIRMWARE_VERSION " Test/Debug Main Menu ---\r\n"
    },
    {
        .item_type = MENU_ITEM_HELP,
        .key = '?',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key = '\r',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '!',
        .text = "MCU RESET",
        .function = v_debug_mcu_reset
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '#',
        .text = "Suspend and wait for watchdog reset",
        .function = v_debug_watchdog_test
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '%',
        .text = "Show system clock frequencies",
        .function = v_debug_show_clocks
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '/',
        .text = "Show GPIO levels",
        .function = v_debug_show_gpio
    },
    {
        .item_type = MENU_ITEM_CALL_MENU,
        .key = 'i',
        .text = "LED/indication tests",
        .menu = x_led_ind_menu
    },
    {
        .item_type = MENU_ITEM_CALL_MENU,
        .key = 'd',
        .text = "SSD1306 display tests",
        .menu = x_ssd1306_display_menu
    },
    {
        .item_type = MENU_ITEM_CALL_MENU,
        .key = 'p',
        .text = "View/set parameters",
        .menu = x_param_menu
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'g',
        .text = "GPS terminal",
        .function = v_debug_gps_terminal
    },
#if 0
    {
        .item_type = MENU_ITEM_CALL_MENU,
        .key = 'x',
        .text = "Example submenu (demo, not used at present)",
        .menu = x_example_menu
    },
#endif
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'q',
        .text = "Quick test function",
        .function = v_debug_quick_test
    },
    {
        .item_type = MENU_ITEM_END_OF_LIST,
    }
};

/******************************************************************************
 *
 ******************************************************************************/

static void *x_debug_menu_stack[4];
static menu_control_t x_debug_menu_control;
#define DEBUG_MENU_STACK_DEPTH  (sizeof(x_debug_menu_stack) / sizeof (void *))

void v_debug_menu_init(void)
{
    v_menu_init(&x_debug_menu_control,
                x_debug_top_menu,
                &x_debug_menu_stack[0],
                DEBUG_MENU_STACK_DEPTH);

    // key param == 0xFF to request help printout
    v_menu_exec(&x_debug_menu_control, 0xFF);
//    u16_debug_activity_count = 0;
}

/******************************************************************************
 *
 ******************************************************************************/

static void v_debug_menu_exec(char c_key)
{
    if (x_debug_menu_control.menu_stack == NULL)
    {
        v_debug_menu_init();
    }
    v_menu_exec(&x_debug_menu_control, c_key);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_menu_service(void)
{
    static uint8_t u8_reentry_lock;
    int i_key;
    char str_key[4];

    if (u8_reentry_lock)
    {
        return;
    }
    u8_reentry_lock = 1;

    do
    {
        i_key = getchar();
        if (i_key < 0)
        {
            break;
        }

        p_c_char_to_str((char) i_key, str_key);

        printf("Cmd [%s]\r\n", str_key);
        v_debug_menu_exec((char) i_key);
    }
    while (1);

    u8_reentry_lock = 0;
}

//------------------------------------------------------------------------------

#else // DEBUG && DEBUG_MENU

void v_debug_menu_init(void)
{

}

void v_debug_menu_service(void)
{

}

#endif
