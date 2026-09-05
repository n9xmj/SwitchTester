#pragma once

/**
 * @file    menusystem.h
 * @brief   Tiny, allocation-free console-menu framework.
 *
 * @details
 * You describe a menu as a flat, @c const array of @ref menu_item_t entries -
 * title text, key-dispatched command functions, submenus - and this module
 * prints the help listing, checks for duplicate keys, and routes a keypress to
 * the right handler. Submenus form a stack (goto / call / return), so a menu
 * tree needs no heap and no back-pointers.
 *
 * It is the simplest kind of vendored module: **C standard library only - no
 * config header, no port source, nothing to satisfy.** Its only outputs are
 * @c printf / @c putchar, which reach the console through whatever stdio
 * retargeting the application already has; the module neither knows nor cares
 * how. See @c README.md in this directory for the adoption walk-through.
 *
 * @section menusystem_null_rule The NULL-pointer conventions
 *
 * Two independent "NULL means..." rules run through the API, and they are not
 * the same rule:
 *
 * - **@c .p_c_text == NULL suppresses the entry's help line.** The operation
 *   still works; it is just absent from the listing (used for hidden aliases,
 *   and for a home-menu return that should function without being advertised).
 *   Item types that have a sensible default caption (@c HELP, @c GOTO_MENU,
 *   @c CALL_MENU) substitute it instead of hiding.
 * - **A NULL handler / menu pointer means "do nothing".** A @c FUNCTION whose
 *   @c pfn_function is NULL, or a @c GOTO_MENU whose @c p_x_menu is NULL, is
 *   reported as "not implemented" rather than dispatched.
 */

#include <stdint.h>
#include <stdbool.h>

/*==============================================================================
 * Configuration
 *============================================================================*/

/**
 * @brief Prompt string emitted after every menu command execution attempt.
 *
 * @c #ifndef-guarded, so a project wanting a different prompt can @c -D it or
 * define it ahead of the include. Define it to @c "" for no prompt.
 */
#ifndef MENUSYSTEM_PROMPT
#define MENUSYSTEM_PROMPT       "{Ready}:"
#endif

/*==============================================================================
 * Menu-item description
 *============================================================================*/

/**
 * @brief What a @ref menu_item_t is - selects both dispatch and help rendering.
 *
 * @note @c __attribute__((packed)) with an explicit @c 0xFF sentinel pins the
 *       enum to one byte, so a @ref menu_item_t stays compact.
 */
typedef enum __attribute__((packed))
{
    MENU_ITEM_END_OF_LIST,          /**< Array terminator. MUST be first (value 0) and last in every menu. */
    MENU_ITEM_IGNORE,               /**< Placeholder; never printed, never dispatched.                     */
    MENU_ITEM_HELP_TEXT_FIXED,      /**< Static title / section text printed in the listing.               */
    MENU_ITEM_HELP_TEXT_VARIABLE,   /**< Fixed text followed by output from @c pfn_help_text_function.      */
    MENU_ITEM_HELP_TEXT_VARIABLE_VALUE, /**< As above, but the emitter receives @c u8_value.                */
    MENU_ITEM_HELP,                 /**< Reprints the whole menu (the classic @c '?').                     */
    MENU_ITEM_HELP_HIDDEN,          /**< Reprints the menu but stays off the listing (e.g. a bare Enter).   */
    MENU_ITEM_FUNCTION,             /**< Runs @c pfn_function (@c void(void)).                              */
    MENU_ITEM_VALUE_FUNCTION,       /**< Runs @c pfn_value_function, passing @c u8_value.                   */
    MENU_ITEM_KEY_FUNCTION,         /**< Runs @c pfn_key_function, passing the key that selected it.        */
    MENU_ITEM_KEY_LIST_FUNCTION,    /**< One handler bound to several keys listed in @c p_c_key_list.       */
    MENU_ITEM_GOTO_MENU,            /**< Replaces the current menu with @c p_x_menu (no stack push).        */
    MENU_ITEM_CALL_MENU,            /**< Pushes and enters @c p_x_menu (a nested submenu).                  */
    MENU_ITEM_RETURN_TO_PREVIOUS_MENU, /**< Pops one level back toward the home menu.                       */
    MENU_ITEM_RETURN_TO_HOME_MENU,  /**< Pops all the way back to the home menu.                            */
    MENU_ITEM_MAX_VALUE_FOR_SIZEOF_1 = 0xFF /**< Forces single-byte storage; not a usable item type.        */
}
menu_item_type_t;

/**
 * @brief OR-mask values for @ref menu_item_t::u8_options.
 *
 * Overlay of the named option bits, so a menu may set flags either by field
 * (@c .b_no_newline @c = @c 1) or by mask (@c .u8_options @c = @c MOPT_NO_NEWLINE).
 * GCC packs the @c bool bitfields LSB-first, so the mask values below match the
 * bit each field occupies.
 */
typedef enum
{
    MOPT_NONE            = 0x00,     /**< No options set.                                    */
    MOPT_NOT_IMPLEMENTED = 0x01,     /**< bit 0 - overlays @c b_not_implemented.             */
    MOPT_NO_NEWLINE      = 0x02      /**< bit 1 - overlays @c b_no_newline.                  */
}
menu_option_mask_t;

typedef struct menu_item_s menu_item_t;

/**
 * @brief One menu entry: a type, a selecting key, option flags, text and a target.
 *
 * The two trailing unions overlay by @ref menu_item_type_t: the first is the
 * help text (or, for @c KEY_LIST_FUNCTION, the list of keys); the second is the
 * target (a handler pointer, or a submenu pointer for @c GOTO / @c CALL).
 */
struct menu_item_s
{
    menu_item_type_t    x_type;         /**< What this entry is (dispatch + help).            */
    char                c_key;          /**< Key that selects it; @c 0 = no key.              */

    /**
     * @brief Per-item option flags, as one byte or as individual bits.
     *
     * Anonymous union: assign the whole byte with a @ref menu_option_mask_t
     * OR-mask via @c u8_options, or set flags individually by their bitfield
     * names. Named identically to the former standalone @c uint8_t fields, so
     * existing @c .b_not_implemented / @c .b_no_newline initialisers are
     * unchanged.
     */
    union
    {
        uint8_t         u8_options;                 /**< All option bits at once (MOPT_* mask).       */
        struct
        {
            bool        b_not_implemented : 1;      /**< Report "not implemented"; skip the handler.  */
            bool        b_no_newline      : 1;      /**< Suppress the trailing CR/LF after this item.  */
        };
    };

    /**
     * @brief Free-form byte handed to the @c *_VALUE handlers; ignored by every
     *        other item type.
     *
     * The module attaches no meaning to it - it is whatever small integer the
     * menu author finds useful (a channel number, a parameter index, a bit
     * mask, a non-sequential tag). It lets one handler serve many entries, the
     * way @c KEY_LIST_FUNCTION's index does, but chosen per entry rather than
     * derived from a key's position in a list.
     *
     * @note It costs nothing: this slot is the padding byte ahead of the
     *       4-byte-aligned pointer unions below, which is why it sits here
     *       rather than at the end. The @c _Static_assert after this struct
     *       pins that down.
     */
    uint8_t             u8_value;

    union
    {
        const char      *p_c_text;      /**< Help text; @c NULL suppresses the listing line.  */
        const char      *p_c_key_list;  /**< @c KEY_LIST_FUNCTION: NUL-terminated keys to match. */
    };
    union
    {
        void (*pfn_function)(void);                 /**< @c FUNCTION handler.                          */
        void (*pfn_value_function)(uint8_t);        /**< @c VALUE_FUNCTION handler (receives @c u8_value). */
        void (*pfn_key_function)(char);             /**< @c KEY_FUNCTION handler (receives the key).   */
        void (*pfn_key_list_function)(char, uint8_t); /**< @c KEY_LIST_FUNCTION handler (key + index).  */
        void (*pfn_help_text_function)(void);       /**< @c HELP_TEXT_VARIABLE text emitter.           */
        void (*pfn_help_text_value_function)(uint8_t); /**< @c HELP_TEXT_VARIABLE_VALUE text emitter.  */
        const menu_item_t *p_x_menu;                /**< @c GOTO_MENU / @c CALL_MENU target.           */
    };
};

/* u8_value must live in padding, not cost a word. Compare against the same
 * struct without it: if a future field reordering pushes the item over, this
 * fails rather than quietly taxing every menu entry in the project. */
_Static_assert(sizeof(menu_item_t) == sizeof(struct {
                   menu_item_type_t x_type;
                   char             c_key;
                   uint8_t          u8_options;
                   const char       *p_c_text;
                   void            (*pfn_function)(void); }),
               "menu_item_t::u8_value must occupy existing padding");

/* The option flags must pack into a single byte. GCC lays bool:1 bitfields out
 * LSB-first into one byte, aliasing u8_options; a compiler that widened them
 * would break the MOPT_* mask overlay, so fail the build loudly instead. */
_Static_assert(sizeof(union { uint8_t u8_options;
                              struct { bool b_not_implemented : 1;
                                       bool b_no_newline      : 1; }; }) == 1,
               "menu_item option flags must pack into a single byte");

/*==============================================================================
 * Menu control block
 *============================================================================*/

/**
 * @brief Run-time state for one menu tree: the current position and its stack.
 *
 * @c CALL_MENU pushes onto @c pap_x_menu and @c RETURN pops; @c u8_stack_depth
 * bounds how deeply submenus may nest. Zero-initialised by @ref v_menu_init.
 */
typedef struct
{
    const menu_item_t   **pap_x_menu;    /**< Menu stack (index 0 is the home menu).           */
    uint8_t             u8_stack_depth;  /**< Capacity of @c pap_x_menu, in entries.           */
    uint8_t             u8_stack_index;  /**< Current depth; 0 = at the home menu.             */
    uint8_t             u8_reserved1;    /**< Padding / future use.                            */
    uint8_t             u8_reserved2;    /**< Padding / future use.                            */
}
menu_control_t;

/*==============================================================================
 * Public API
 *============================================================================*/

/**
 * @brief Render a key as a short printable string.
 *
 * Maps control codes to readable tokens: @c '\r' becomes @c "Ent", @c 0x1B
 * becomes @c "ESC", other control codes become @c "^X" caret notation, and
 * printable keys pass through as a one-character string.
 *
 * @param c_ch    Key to render (masked to 7 bits).
 * @param p_c_str Caller's buffer; needs room for up to 3 chars plus NUL.
 * @return @p p_c_str, or @c NULL if @p p_c_str is @c NULL.
 */
extern char *pc_char_to_str(char c_ch, char *p_c_str);

/**
 * @brief Initialise a control block and select the home menu.
 *
 * @param p_x_menu_control Control block to initialise.
 * @param p_x_home_menu    The top-level (home) menu array.
 * @param p_v_menu_stack   Storage for @p u8_stack_depth menu pointers, or
 *                         @c NULL to allocate it here with @c calloc.
 * @param u8_stack_depth   Stack capacity; forced to at least 1.
 *
 * @note Does nothing if @p p_x_menu_control or @p p_x_home_menu is @c NULL.
 */
extern void v_menu_init(menu_control_t *p_x_menu_control,
                        const menu_item_t *p_x_home_menu,
                        void *p_v_menu_stack,
                        uint8_t u8_stack_depth);

/**
 * @brief Dispatch one key against the current menu.
 *
 * Matches @p c_key to an entry and performs its action (run a handler, enter or
 * leave a submenu, reprint the help). A key of @c 0xFF prints the current menu
 * without dispatching - use it once at start-up to show the initial listing.
 * Unmatched keys print a "not recognized" line. Emits @ref MENUSYSTEM_PROMPT
 * afterwards.
 *
 * @param p_x_menu_control Control block from @ref v_menu_init.
 * @param c_key            Key to dispatch, or @c 0xFF to just print the menu.
 */
extern void v_menu_exec(menu_control_t *p_x_menu_control, char c_key);
