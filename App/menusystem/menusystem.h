/******************************************************************************
 *
 ******************************************************************************/

#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

// MENUSYSTEM_PROMPT will be output after every menu command execution attempt.
// It may be un-defined (commented out) if no prompt is desired.

#define MENUSYSTEM_PROMPT       "{Ready}:"

//------------------------------------------------------------------------------

typedef enum __attribute__((packed))
{
    MENU_ITEM_END_OF_LIST,          // This should be 1st enum listed (0)
    MENU_ITEM_IGNORE,
    MENU_ITEM_HELP_TEXT_FIXED,
    MENU_ITEM_HELP_TEXT_VARIABLE,
    MENU_ITEM_HELP,
    MENU_ITEM_HELP_HIDDEN,
    MENU_ITEM_FUNCTION,
    MENU_ITEM_KEY_FUNCTION,
    MENU_ITEM_KEY_LIST_FUNCTION,
    MENU_ITEM_GOTO_MENU,
    MENU_ITEM_CALL_MENU,
    MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
    MENU_ITEM_RETURN_TO_HOME_MENU,
    MENU_ITEM_MAX_VALUE_FOR_SIZEOF_1 = 0xFF
}
menu_item_type_t;

typedef struct menu_item_s menu_item_t;

struct menu_item_s
{
    menu_item_type_t    item_type;  // Should be 8-bit value
    char                key;
    uint8_t             not_implemented;
    uint8_t             no_newline; // Suppress the trailing CR/LF after this item
    union
    {
        const char      *text;
        const char      *key_list;
    };
    union
    {
        void (*function)(void);
        void (*key_function)(char);
        void (*key_list_function)(char,uint8_t);
        void (*help_text_function)(void);
        const menu_item_t *menu;
    };
};

typedef struct
{
    const menu_item_t   **menu_stack;
    uint8_t             menu_stack_depth;
    uint8_t             menu_stack_index;
    uint8_t             _reserved1;
    uint8_t             _reserved2;
}
menu_control_t;

//------------------------------------------------------------------------------

extern char * p_c_char_to_str(char ch, char *p_c_str);

extern  void v_menu_init(menu_control_t *p_x_menu_control,
                         const menu_item_t *p_x_home_menu,
                         void *p_x_menu_stack,
                         uint8_t u8_menu_stack_depth);
extern void v_menu_exec(menu_control_t *p_x_menu_control, char c_key);

#endif
