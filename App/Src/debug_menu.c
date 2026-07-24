/******************************************************************************
 * debug_menu.c
 *
 * Bare-bones console debug menu built on the menusystem framework.
 *
 * Skeleton content only: [?] help plus two no-op quick-test stubs. Build the
 * menu up by adding menu_item_t entries to x_debug_top_menu (and sub-menus)
 * and pointing them at your own command handlers.
 ******************************************************************************/

/*============================================================================
 * INCLUDES
 *==========================================================================*/

#include "device_config.h"          /* stdint/stdio, macros.h (SYSTEM_TICK), main.h */
#include "menusystem.h"
#include "debug_menu.h"

/*============================================================================
 * PRIVATE PROTOTYPES
 *==========================================================================*/

static void v_quick_test_1(void);
static void v_quick_test_2(void);
static void v_debug_menu_exec(char c_key);

/*============================================================================
 * PRIVATE FUNCTIONS (menu command handlers)
 *==========================================================================*/

static void v_quick_test_1(void)
{
    printf("Quick test function 1 (stub)\r\n");
}

static void v_quick_test_2(void)
{
    printf("Quick test function 2 (stub)\r\n");
}

/*============================================================================
 * MENU DEFINITION
 *==========================================================================*/

static const menu_item_t x_debug_top_menu[] =
{
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "\r\n--- " PRODUCT_NAME " v" FIRMWARE_VERSION " Main Menu ---\r\n"
    },
    {
        .item_type = MENU_ITEM_HELP,
        .key = '?',
        .text = NULL
    },
    {
        /* Bare <Enter> re-prints the menu without logging an unknown key. */
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key = '\r',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'q',
        .text = "Quick test function 1",
        .function = v_quick_test_1
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'Q',
        .text = "Quick test function 2",
        .function = v_quick_test_2
    },
    {
        .item_type = MENU_ITEM_END_OF_LIST,
    }
};

/*============================================================================
 * MENU CONTROL + SERVICE
 *==========================================================================*/

static void *x_debug_menu_stack[4];
static menu_control_t x_debug_menu_control;
#define DEBUG_MENU_STACK_DEPTH  (sizeof(x_debug_menu_stack) / sizeof(void *))

void v_debug_menu_init(void)
{
    v_menu_init(&x_debug_menu_control,
                x_debug_top_menu,
                &x_debug_menu_stack[0],
                DEBUG_MENU_STACK_DEPTH);

    /* key == 0xFF requests the initial help printout. */
    v_menu_exec(&x_debug_menu_control, 0xFF);
}

static void v_debug_menu_exec(char c_key)
{
    if (x_debug_menu_control.menu_stack == NULL)
    {
        v_debug_menu_init();
    }
    v_menu_exec(&x_debug_menu_control, c_key);
}

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
            break;              /* no input pending */
        }

        p_c_char_to_str((char) i_key, str_key);
        printf("Cmd [%s]\r\n", str_key);
        v_debug_menu_exec((char) i_key);
    }
    while (1);

    u8_reentry_lock = 0;
}

void v_debug_delay(uint32_t u32_delay)
{
    /* Cooperative delay: keep the console menu responsive while waiting. */
    uint32_t u32_start = SYSTEM_TICK();
    while (ELAPSED_TIME(u32_start) < u32_delay)
    {
        v_debug_menu_service();
    }
}
