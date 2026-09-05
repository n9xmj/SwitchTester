/**
 * @file    menusystem.c
 * @brief   Dispatch, help printer and key-conflict checker for @ref menusystem.h.
 */

#include "menusystem.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>


/**
 * @brief Render a key as a short printable string. See @ref pc_char_to_str decl.
 *
 * @param c_ch    Key to render (masked to 7 bits).
 * @param p_c_str Caller's buffer; up to 3 chars plus NUL.
 * @return @p p_c_str, or @c NULL when @p p_c_str is @c NULL.
 */
char *pc_char_to_str(char c_ch, char *p_c_str)
{
    if (p_c_str == NULL)
    {
        return NULL;
    }

    c_ch &= 0x7F;
    if (c_ch == '\r')
    {
        strcpy(p_c_str, "Ent");
    }
    else if (c_ch == 0x1B)
    {
        strcpy(p_c_str, "ESC");
    }
    else if (c_ch < 0x20)
    {
        p_c_str[0] = '^';
        p_c_str[1] = (char) (c_ch + '@');
        p_c_str[2] = 0;
    }
    else
    {
        p_c_str[0] = c_ch;
        p_c_str[1] = 0;
    }

    return p_c_str;
}

/** @brief Caption substituted for a keyed entry that supplies no text of its own. */
static const char * const p_c_no_description = "--- no description ---";

/**
 * @brief Initialise a control block and select the home menu.
 *
 * See @ref v_menu_init declaration for the contract.
 *
 * @param p_x_menu_control Control block to initialise.
 * @param p_x_home_menu    Top-level (home) menu array.
 * @param p_v_menu_stack   Storage for @p u8_stack_depth menu pointers, or
 *                         @c NULL to allocate here.
 * @param u8_stack_depth   Stack capacity; forced to at least 1.
 */
void v_menu_init(menu_control_t *p_x_menu_control,
                 const menu_item_t *p_x_home_menu,
                 void *p_v_menu_stack,
                 uint8_t u8_stack_depth)
{
    if ( (p_x_menu_control == NULL) ||
         (p_x_home_menu == NULL) )
    {
        return;
    }

    if (u8_stack_depth == 0)
    {
        u8_stack_depth = 1;
    }
    if (p_v_menu_stack == NULL)
    {
        p_v_menu_stack = calloc(u8_stack_depth, sizeof(void *));
    }

    memset(p_x_menu_control, 0, sizeof(menu_control_t));
    p_x_menu_control->u8_stack_index = 0;
    p_x_menu_control->pap_x_menu = (const menu_item_t **) p_v_menu_stack;
    p_x_menu_control->pap_x_menu[0] = p_x_home_menu;
    p_x_menu_control->u8_stack_depth = u8_stack_depth;
}

/**
 * @brief Warn about any two entries in one menu that share a selecting key.
 *
 * Runs from the help printer; duplicate keys are a data error caught at run
 * time, not compile time. The warning is monochrome by design (no ANSI), which
 * is what keeps this module dependency-free.
 *
 * @param menu Menu array to scan.
 * @retval true  At least one key collision was found (and reported).
 * @retval false No collisions.
 */
bool b_menu_key_conflict_check(const menu_item_t *menu)
{
    uint16_t u16_outer_index;
    uint16_t u16_inner_index;
    bool b_key_conflict = false;
    const char *p_c_text_outer;
    const char *p_c_text_inner;

    u16_outer_index = 0;
    while (menu[u16_outer_index].x_type != MENU_ITEM_END_OF_LIST)
    {
        if (menu[u16_outer_index].c_key != 0)
        {
            u16_inner_index = u16_outer_index + 1;
            while (menu[u16_inner_index].x_type != MENU_ITEM_END_OF_LIST)
            {
                if (menu[u16_inner_index].c_key != 0)
                {
                    if (menu[u16_outer_index].c_key == menu[u16_inner_index].c_key)
                    {
                        b_key_conflict = true;
                        p_c_text_outer = menu[u16_outer_index].p_c_text;
                        if (p_c_text_outer == NULL)
                        {
                            p_c_text_outer = p_c_no_description;
                        }
                        p_c_text_inner = menu[u16_inner_index].p_c_text;
                        if (p_c_text_inner == NULL)
                        {
                            p_c_text_inner = p_c_no_description;
                        }
                        printf("WARNING: Menu items share the same key definition [%c]:\r\n"
                               "%s\r\n"
                               "%s\r\n",
                               menu[u16_outer_index].c_key,
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

/**
 * @brief Emit a CR/LF unless the item asked to suppress it.
 * @param u8_no_newline Non-zero to suppress the newline (see @c b_no_newline).
 */
static void v_newline(uint8_t u8_no_newline)
{
    if (!u8_no_newline)
    {
        putchar('\r');
        putchar('\n');
    }
}

/**
 * @brief Print a menu's help listing.
 *
 * Walks the array, printing one line per visible entry (@c "[key] text"), with
 * per-type handling of default captions and hidden entries, then runs the
 * key-conflict check. An entry with @c .p_c_text @c == @c NULL is suppressed
 * from the listing for the item types where that is the convention (hidden
 * aliases, bare function keys and @c RETURN_TO_PREVIOUS_MENU).
 *
 * @param p_x_menu_list Menu array to print; @c NULL is ignored.
 */
void v_menu_help(const menu_item_t *p_x_menu_list)
{
    const menu_item_t *p_x_entry;
    const char *p_c_text;
    bool b_print_entry;
    char ac_key[4];

    if (p_x_menu_list == NULL)
    {
        return;
    }

    p_x_entry = p_x_menu_list;
    while (p_x_entry->x_type != MENU_ITEM_END_OF_LIST)
    {
        p_c_text = p_x_entry->p_c_text;
        pc_char_to_str(p_x_entry->c_key, ac_key);
        b_print_entry = true;

        switch (p_x_entry->x_type)
        {
            case MENU_ITEM_HELP_TEXT_FIXED:
                b_print_entry = false;
                if (p_x_entry->p_c_text != NULL)
                {
                    printf("%s", p_c_text);
                    v_newline(p_x_entry->b_no_newline);
                }
                break;

            case MENU_ITEM_HELP_TEXT_VARIABLE:
                b_print_entry = false;
                if (p_x_entry->p_c_text != NULL)
                {
                    printf("%s", p_c_text);
                    v_newline(p_x_entry->b_no_newline);
                }
                if (p_x_entry->pfn_help_text_function != NULL)
                {
                    p_x_entry->pfn_help_text_function();
                }
                break;

            case MENU_ITEM_HELP_TEXT_VARIABLE_VALUE:
                // As HELP_TEXT_VARIABLE, but the emitter is told which entry
                // called it. Several entries can then share one emitter, each
                // rendering its own slice of a repeated block.
                b_print_entry = false;
                if (p_x_entry->p_c_text != NULL)
                {
                    printf("%s", p_c_text);
                    v_newline(p_x_entry->b_no_newline);
                }
                if (p_x_entry->pfn_help_text_value_function != NULL)
                {
                    p_x_entry->pfn_help_text_value_function(p_x_entry->u8_value);
                }
                break;

            case MENU_ITEM_HELP:
                if (p_x_entry->p_c_text == NULL)
                {
                    p_c_text = "Help - show this menu";
                }
                break;

            case MENU_ITEM_HELP_HIDDEN:
                if (p_x_entry->p_c_text == NULL)
                {
                    b_print_entry = false;
                }
                break;

            case MENU_ITEM_FUNCTION:
            case MENU_ITEM_VALUE_FUNCTION:
            case MENU_ITEM_KEY_FUNCTION:
                if (p_x_entry->p_c_text == NULL)
                {
                    b_print_entry = false;
                }
                break;

            case MENU_ITEM_KEY_LIST_FUNCTION:
                b_print_entry = false;
                break;

            case MENU_ITEM_GOTO_MENU:
                if (p_x_entry->p_c_text == NULL)
                {
                    p_c_text = "Go to another menu";
                }
                break;

            case MENU_ITEM_CALL_MENU:
                if (p_x_entry->p_c_text == NULL)
                {
                    p_c_text = "Go to submenu";
                }
                break;

            case MENU_ITEM_RETURN_TO_PREVIOUS_MENU:
                // A NULL text hides this entry from the listing (as a NULL-text
                // FUNCTION does), while the return still works - the pattern for
                // an unadvertised home-menu return. A visible return supplies
                // its own text.
                if (p_x_entry->p_c_text == NULL)
                {
                    b_print_entry = false;
                }
                break;

            case MENU_ITEM_RETURN_TO_HOME_MENU:
                if (p_x_entry->p_c_text == NULL)
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
            printf("[%s] %s", ac_key, p_c_text);
            v_newline(p_x_entry->b_no_newline);
        }

        p_x_entry++;
    }

    b_menu_key_conflict_check(p_x_menu_list);
    printf("\r\n");
}

/**
 * @brief Dispatch one key against the current menu.
 *
 * See @ref v_menu_exec declaration for the contract. Preserves the optional
 * cleanup callback on the two RETURN item types: the @c pfn_function slot is
 * unused by RETURN entries (GOTO/CALL use @c p_x_menu), so a menu may set it to
 * run tear-down on leaving a submenu without growing @ref menu_item_t.
 *
 * @param p_x_menu_control Control block from @ref v_menu_init.
 * @param c_key            Key to dispatch, or @c 0xFF to just print the menu.
 */
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

    p_x_current_menu = p_x_menu_control->pap_x_menu[p_x_menu_control->u8_stack_index];

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

    while ((p_x_entry->x_type != MENU_ITEM_END_OF_LIST) && !b_found_match)
    {
        // Special handling for MENU_ITEM_KEY_LIST_FUNCTION
        // Scan p_x_entry->p_c_text (aka p_c_key_list) string for match with c_key
        if ( (p_x_entry->x_type == MENU_ITEM_KEY_LIST_FUNCTION)
             && (p_x_entry->p_c_key_list != NULL) )
        {
            u8_index = 0;
            while ((u8_index < 0xFF) && (p_x_entry->p_c_key_list[u8_index] != 0))
            {
                if (p_x_entry->p_c_key_list[u8_index] == c_key)
                {
                    b_found_match = true;
                    break;
                }
                u8_index++;
            }
            if (b_found_match)
            {
                if (p_x_entry->b_not_implemented
                    || (p_x_entry->pfn_key_list_function == NULL))
                {
                    b_report_not_implemented = true;
                }
                else
                {
                    p_x_entry->pfn_key_list_function(c_key, u8_index);
                }
            }
        }

        if ((p_x_entry->c_key != 0) && (p_x_entry->c_key == c_key))
        {
            b_found_match = true;
            switch (p_x_entry->x_type)
            {
                case MENU_ITEM_HELP:
                case MENU_ITEM_HELP_HIDDEN:
                    v_menu_help(p_x_current_menu);
                    break;

                case MENU_ITEM_FUNCTION:
                    if (p_x_entry->b_not_implemented || (p_x_entry->pfn_function == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_entry->pfn_function();
                    }
                    break;

                case MENU_ITEM_VALUE_FUNCTION:
                    // Deliberately NOT merged with MENU_ITEM_FUNCTION above:
                    // both handlers live in the same union slot, so falling
                    // through would call a void(uint8_t) through a void(void)
                    // prototype - which compiles silently.
                    if (p_x_entry->b_not_implemented || (p_x_entry->pfn_value_function == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_entry->pfn_value_function(p_x_entry->u8_value);
                    }
                    break;

                case MENU_ITEM_KEY_FUNCTION:
                    if (p_x_entry->b_not_implemented || (p_x_entry->pfn_key_function == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_entry->pfn_key_function(c_key);
                    }
                    break;

                case MENU_ITEM_KEY_LIST_FUNCTION:
                    break;

                case MENU_ITEM_GOTO_MENU:
                    if (p_x_entry->b_not_implemented || (p_x_entry->p_x_menu == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_menu_control->pap_x_menu[p_x_menu_control->u8_stack_index] = p_x_entry->p_x_menu;
                        v_menu_help(p_x_entry->p_x_menu);
                    }
                    break;

                case MENU_ITEM_CALL_MENU:
                    if (p_x_entry->b_not_implemented || (p_x_entry->p_x_menu == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        if (p_x_menu_control->u8_stack_index < (p_x_menu_control->u8_stack_depth - 1))
                        {
                            p_x_menu_control->u8_stack_index++;
                        }
                        else
                        {
                            printf("WARNING: Menu stack full\r\n");
                        }
                        p_x_menu_control->pap_x_menu[p_x_menu_control->u8_stack_index] = p_x_entry->p_x_menu;
                        v_menu_help(p_x_entry->p_x_menu);
                    }
                    break;

                case MENU_ITEM_RETURN_TO_PREVIOUS_MENU:
                    if (p_x_menu_control->u8_stack_index > 0)
                    {
                        p_x_menu_control->u8_stack_index--;
                    }
                    else
                    {
                        printf("\r\n[At top-level menu]\r\n");
                    }
                    // Optional cleanup / exit callback (e.g. stop active synth on leaving a submenu).
                    // The pfn_function slot is unused by RETURN items (GOTO/CALL use p_x_menu),
                    // so we repurpose it here without changing struct size.
                    if (p_x_entry->pfn_function != NULL)
                    {
                        p_x_entry->pfn_function();
                    }
                    v_menu_help(p_x_menu_control->pap_x_menu[p_x_menu_control->u8_stack_index]);
                    break;

                case MENU_ITEM_RETURN_TO_HOME_MENU:
                    p_x_menu_control->u8_stack_index = 0;
                    // Optional cleanup / exit callback (see comment above).
                    if (p_x_entry->pfn_function != NULL)
                    {
                        p_x_entry->pfn_function();
                    }
                    v_menu_help(p_x_menu_control->pap_x_menu[0]);
                    break;

                case MENU_ITEM_IGNORE:
                case MENU_ITEM_HELP_TEXT_FIXED:
                case MENU_ITEM_HELP_TEXT_VARIABLE:
                case MENU_ITEM_HELP_TEXT_VARIABLE_VALUE:
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
        char ac_key[4];
        pc_char_to_str(c_key, ac_key);
        printf("Selection [%s] not recognized\r\n", ac_key);
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
