/******************************************************************************
 *
 ******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "menusystem.h"

/******************************************************************************
 *
 ******************************************************************************/

char * p_c_char_to_str(char ch, char *p_c_str)
{
    if (p_c_str == NULL)
    {
        return NULL;
    }

    ch &= 0x7F;
    if (ch == '\r')
    {
        strcpy(p_c_str, "Ent");
    }
    else if (ch == 0x1B)
    {
        strcpy(p_c_str, "ESC");
    }
    else if (ch < 0x20)
    {
        p_c_str[0] = '^';
        p_c_str[1] = ch + '@';
        p_c_str[2] = 0;
    }
    else
    {
        p_c_str[0] = ch;
        p_c_str[1] = 0;
    }

    return p_c_str;
}

/******************************************************************************
 *
 ******************************************************************************/

static const char * const p_c_no_description = "--- no description ---";

void v_menu_init(menu_control_t *p_x_menu_control,
                 const menu_item_t *p_x_home_menu,
                 void *p_x_menu_stack,
                 uint8_t u8_menu_stack_depth)
{
    if ( (p_x_menu_control == NULL) ||
         (p_x_home_menu == NULL) )
    {
        return;
    }

    if (u8_menu_stack_depth == 0)
    {
        u8_menu_stack_depth = 1;
    }
    if (p_x_menu_stack == NULL)
    {
        p_x_menu_stack = calloc(u8_menu_stack_depth, sizeof(void *));
    }

    memset(p_x_menu_control, 0, sizeof(menu_control_t));
    p_x_menu_control->menu_stack_index = 0;
    p_x_menu_control->menu_stack = p_x_menu_stack;
    p_x_menu_control->menu_stack[0] = p_x_home_menu;
    p_x_menu_control->menu_stack_depth = u8_menu_stack_depth;
}

/******************************************************************************
 *
 ******************************************************************************/

bool b_menu_key_conflict_check(const menu_item_t *menu)
{
    uint16_t u16_outer_index;
    uint16_t u16_inner_index;
    bool b_key_conflict = false;
    const char *p_c_text_outer;
    const char *p_c_text_inner;

    u16_outer_index = 0;
    while (menu[u16_outer_index].item_type != MENU_ITEM_END_OF_LIST)
    {
        if (menu[u16_outer_index].key != 0)
        {
            u16_inner_index = u16_outer_index + 1;
            while (menu[u16_inner_index].item_type != MENU_ITEM_END_OF_LIST)
            {
                if (menu[u16_inner_index].key != 0)
                {
                    if (menu[u16_outer_index].key == menu[u16_inner_index].key)
                    {
                        b_key_conflict = true;
                        p_c_text_outer = menu[u16_outer_index].text;
                        if (p_c_text_outer == NULL)
                        {
                            p_c_text_outer = p_c_no_description;
                        }
                        p_c_text_inner = menu[u16_inner_index].text;
                        if (p_c_text_inner == NULL)
                        {
                            p_c_text_inner = p_c_no_description;
                        }
                        printf("WARNING: Menu items share the same key definition [%c]:\r\n"
                               "%s\r\n"
                               "%s\r\n",
                               menu[u16_outer_index].key,
                               p_c_text_outer,
                               p_c_text_inner);
                    }
                }
                u16_inner_index++;
            }
        }
        u16_outer_index++;
    }

    return b_key_conflict;
}

/******************************************************************************
 * Emit CR/LF unless the item asked to suppress it (no_newline), so consecutive
 * items can be rendered on one line.
 ******************************************************************************/

static void v_menu_newline(uint8_t u8_no_newline)
{
    if (!u8_no_newline)
    {
        putchar('\r');
        putchar('\n');
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_menu_help(const menu_item_t *p_x_menu_list)
{
    const menu_item_t *p_x_entry;
    const char *p_c_text;
    bool b_print_entry;
    char str_key[4];

    if (p_x_menu_list == NULL)
    {
        return;
    }

    p_x_entry = p_x_menu_list;
    while (p_x_entry->item_type != MENU_ITEM_END_OF_LIST)
    {
        p_c_text = p_x_entry->text;
        p_c_char_to_str(p_x_entry->key, str_key);
        b_print_entry = true;

        switch (p_x_entry->item_type)
        {
            case MENU_ITEM_HELP_TEXT_FIXED:
                b_print_entry = false;
                if (p_x_entry->text != NULL)
                {
                    printf("%s", p_c_text);
                    v_menu_newline(p_x_entry->no_newline);
                }
                break;

            case MENU_ITEM_HELP_TEXT_VARIABLE:
                b_print_entry = false;
                if (p_x_entry->text != NULL)
                {
                    printf("%s", p_c_text);
                    v_menu_newline(p_x_entry->no_newline);
                }
                if (p_x_entry->help_text_function != NULL)
                {
                    p_x_entry->help_text_function();
                }
                break;

            case MENU_ITEM_HELP:
                if (p_x_entry->text == NULL)
                {
                    p_c_text = "Help - show this menu";
                }
                break;

            case MENU_ITEM_HELP_HIDDEN:
                if (p_x_entry->text == NULL)
                {
                    b_print_entry = false;
                }
                break;

            case MENU_ITEM_FUNCTION:
            case MENU_ITEM_KEY_FUNCTION:
                if (p_x_entry->text == NULL)
                {
                    b_print_entry = false;
                }
                break;

            case MENU_ITEM_KEY_LIST_FUNCTION:
                b_print_entry = false;
                break;

            case MENU_ITEM_GOTO_MENU:
                if (p_x_entry->text == NULL)
                {
                    p_c_text = "Go to another menu";
                }
                break;

            case MENU_ITEM_CALL_MENU:
                if (p_x_entry->text == NULL)
                {
                    p_c_text = "Go to submenu";
                }
                break;

            case MENU_ITEM_RETURN_TO_PREVIOUS_MENU:
                if (p_x_entry->text == NULL)
                {
                    p_c_text = "Return to previous menu";
                }
                break;

            case MENU_ITEM_RETURN_TO_HOME_MENU:
                if (p_x_entry->text == NULL)
                {
                    p_c_text = "Return to home (main) menu";
                }
                break;

            case MENU_ITEM_IGNORE:
            default:
                b_print_entry = false;
                break;
        }

        if (b_print_entry)
        {
            printf("[%s] %s", str_key, p_c_text);
            v_menu_newline(p_x_entry->no_newline);
        }

        p_x_entry++;
    }

    b_menu_key_conflict_check(p_x_menu_list);
    printf("\r\n");
}

/******************************************************************************
 *
 ******************************************************************************/

void v_menu_exec(menu_control_t *p_x_menu_control, char c_key)
{
    const menu_item_t *p_x_current_menu;
    const menu_item_t *p_x_entry;
    bool b_found_match = false;
    bool b_report_not_implemented = false;
    uint8_t u8_index;

    if (p_x_menu_control == NULL)
    {
        return;
    }

    p_x_current_menu = p_x_menu_control->menu_stack[p_x_menu_control->menu_stack_index];

    if (p_x_current_menu == NULL)
    {
        return;
    }

    if (c_key == 0xFF)
    {
        v_menu_help(p_x_current_menu);
        goto POST_EXEC_PROMPT;
    }

    c_key &= 0x7F;
    p_x_entry = p_x_current_menu;

    while ((p_x_entry->item_type != MENU_ITEM_END_OF_LIST) && !b_found_match)
    {
        // Special handling for MENU_ITEM_KEY_LIST_FUNCTION
        // Scan p_x_entry->text (aka key_list) string for match with c_key
        if ( (p_x_entry->item_type == MENU_ITEM_KEY_LIST_FUNCTION)
             && (p_x_entry->key_list != NULL) )
        {
            u8_index = 0;
            while ((u8_index < 0xFF) && (p_x_entry->key_list[u8_index] != 0))
            {
                if (p_x_entry->key_list[u8_index] == c_key)
                {
                    b_found_match = true;
                    break;
                }
                u8_index++;
            }
            if (b_found_match)
            {
                if (p_x_entry->not_implemented
                    || (p_x_entry->key_function == NULL) )
                {
                    b_report_not_implemented = true;
                }
                else
                {
                    p_x_entry->key_list_function(c_key,u8_index);
                }
            }
        }

        if ((p_x_entry->key != 0) && (p_x_entry->key == c_key))
        {
            b_found_match = true;
            switch (p_x_entry->item_type)
            {
                case MENU_ITEM_HELP:
                case MENU_ITEM_HELP_HIDDEN:
                    v_menu_help(p_x_current_menu);
                    break;

                case MENU_ITEM_FUNCTION:
                    if (p_x_entry->not_implemented || (p_x_entry->function == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_entry->function();
                    }
                    break;

                case MENU_ITEM_KEY_FUNCTION:
                    if (p_x_entry->not_implemented || (p_x_entry->function == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_entry->key_function(c_key);
                    }
                    break;

                case MENU_ITEM_KEY_LIST_FUNCTION:
                    break;

                case MENU_ITEM_GOTO_MENU:
                    if (p_x_entry->not_implemented || (p_x_entry->function == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_menu_control->menu_stack[p_x_menu_control->menu_stack_index] = p_x_entry->menu;
                        v_menu_help(p_x_entry->menu);
                    }
                    break;

                case MENU_ITEM_CALL_MENU:
                    if (p_x_entry->not_implemented || (p_x_entry->function == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        if (p_x_menu_control->menu_stack_index < (p_x_menu_control->menu_stack_depth - 1))
                        {
                            p_x_menu_control->menu_stack_index++;
                        }
                        else
                        {
                            printf("WARNING: Menu stack full\r\n");
                        }
                        p_x_menu_control->menu_stack[p_x_menu_control->menu_stack_index] = p_x_entry->menu;
                        v_menu_help(p_x_entry->menu);
                    }
                    break;

                case MENU_ITEM_RETURN_TO_PREVIOUS_MENU:
                    if (p_x_menu_control->menu_stack_index > 0)
                    {
                        p_x_menu_control->menu_stack_index--;
                    }
                    else
                    {
                        printf("WARNING: Menu stack empty\r\n");
                    }
                    v_menu_help(p_x_menu_control->menu_stack[p_x_menu_control->menu_stack_index]);
                    break;

                case MENU_ITEM_RETURN_TO_HOME_MENU:
                    p_x_menu_control->menu_stack_index = 0;
                    v_menu_help(p_x_menu_control->menu_stack[0]);
                    break;

                case MENU_ITEM_IGNORE:
                case MENU_ITEM_HELP_TEXT_FIXED:
                case MENU_ITEM_HELP_TEXT_VARIABLE:
                default:
                    // Do nothing
                    break;
            }

            // Found matching entry for key and executed command
            // Exit from loop
            break;
        }

        p_x_entry++;
    }

    if (! b_found_match)
    {
        char str_key[4];
        p_c_char_to_str(c_key, str_key);
        printf("Selection [%s] not recognized\r\n", str_key);
    }
    else if (b_report_not_implemented)
    {
        printf("Not implemented yet\r\n");
    }
    else
    {
        // No action
    }

POST_EXEC_PROMPT:
#ifdef MENUSYSTEM_PROMPT
    printf("%s", MENUSYSTEM_PROMPT);
#endif
}
