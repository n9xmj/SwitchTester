/******************************************************************************
 *
 * File         : ANSI.h
 * Description  : ANSI / VT100 terminal escape codes
 * Author       : MSchultz
 * Date         : 24 May 2020
 *
 ******************************************************************************/

// ANSI cursor control sequences

#define ESC                         0x1B
#define ESC_S                       "\x1B"
#define CSI_S                       "\x1B["     // Control Sequence Introducer

#define ANSI_CURSOR_UP(c)           CSI_S #c "A"
#define ANSI_CURSOR_UP_FMT          CSI_S "%uA"

#define ANSI_CURSOR_DOWN(c)         CSI_S #c "B"
#define ANSI_CURSOR_DOWN_FMT        CSI_S "%uB"

#define ANSI_CURSOR_RIGHT(c)        CSI_S #c "C"
#define ANSI_CURSOR_RIGHT_FMT       CSI_S "%uC"

#define ANSI_CURSOR_LEFT(c)         CSI_S #c "D"
#define ANSI_CURSOR_LEFT_FMT        CSI_S "%uD"

#define ANSI_NEXT_LINE(c)           CSI_S #c "E"    // Move down <c> lines at column 1
#define ANSI_NEXT_LINE_FMT          CSI_S "%uE"
#define ANSI_PREVIOUS_LINE(c)       CSI_S #c "F"    // Move up <c> lines at column 1
#define ANSI_PREVIOUS_LINE_FMT      CSI_S "%uF"
#define ANSI_HORIZONTAL_ABS(c)      CSI_S #c "G"    // Move to column <c> on current line
#define ANSI_HORIZONTAL_ABS_FMT     CSI_S "%uG"

#define ANSI_MOVE_CURSOR(r,c)       CSI_S #r ";" #c "H" // Move cursor, editor function
#define ANSI_MOVE_CURSOR_FMT        CSI_S "%u;%uH"
#define ANSI_HVP(r,c)               CSI_S #r ";" #c "f" // Move cursor, format effector
#define ANSI_HVP_FMT                CSI_S "%u;%uf"

#define ANSI_HOME_CURSOR            CSI_S "H"
#define ANSI_SAVE_CURSOR            ESC_S "7"       // Alternate VT320: CSI_S "s"
#define ANSI_RESTORE_CURSOR         ESC_S "8"       // Alternate VT320: CSI_S "u"
#define ANSI_GET_CURSOR             CSI_S "6n"      // Terminal response: ESC [ Py ; Px R

#define ANSI_HIDE_CURSOR            CSI_S "?25l"
#define ANSI_SHOW_CURSOR            CSI_S "?25h"

#define ANSI_RESET                  ESC_S "c"       // Terminal full reset

// ANSI display editing sequences
// (insert/delete chars and lines of text)

#define ANSI_DEL_CHAR(c)            CSI_S #c "P"
#define ANSI_DEL_CHAR_FMT           CSI_S "%uP"

#define ANSI_INSERT_LINE(n)         CSI_S #n "L"
#define ANSI_INSERT_LINE_FMT        CSI_S "%uL"
#define ANSI_DELETE_LINE(n)         CSI_S #n "M"
#define ANSI_DELETE_LINE_FMT        CSI_S "%uM"

#define ANSI_CLEAR_EOL              CSI_S "K"       // Cursor to end of line
#define ANSI_CLEAR_BOL              CSI_S "1K"      // Beginning of line to cursor
#define ANSI_CLEAR_LINE             CSI_S "2K"      // Clear entire line cursor is on
#define ANSI_CLEAR_EOS              CSI_S "0J"      // Cursor to end of screen
#define ANSI_CLEAR_BOS              CSI_S "1J"      // Beginning of screen to cursor
#define ANSI_CLEAR_SCREEN           CSI_S "2J"      // Clear entire screen
#define ANSI_CLEAR_SCROLLBACK       CSI_S "3J"      // Same as ANSI_CLEAR_SCREEN but also clears scrollback buffer
#define ANSI_CLEAR_AND_HOME         ANSI_CLEAR_SCREEN ANSI_HOME_CURSOR

#define ANSI_SCROLL_UP(n)           CSI_S #n "S"
#define ANSI_SCROLL_UP_FMT          CSI_S "%uS"
#define ANSI_SCROLL_DOWN(n)         CSI_S #n "T"
#define ANSI_SCROLL_DOWN_FMT        CSI_S "%uT"

#define ANSI_SET_MODE(m)            CSI_S #m "h"
#define ANSI_SET_MODE_FMT           CSI_S "%uh"

#define ANSI_RESET_MODE(m)          CSI_S #m "l"
#define ANSI_RESET_MODE_FMT         CSI_S "%ul"

#define ANSI_INS_MODE               CSI_S "4h"
#define ANSI_OVR_MODE               CSI_S "4l"

#define ANSI_SGR(m)                 CSI_S #m "m"    // Set Graphics Rendition (display attributes)
#define ANSI_SGR_FMT                CSI_S "%um"

// Text attributes

#define ANSI_ATTR_OFF               CSI_S "0m"      // All attributes off, normal text attribute and color
#define ANSI_NORMAL                 ANSI_ATTR_OFF   // Alias for ANSI_ATTR_OFF
#define ANSI_BOLD                   CSI_S "1m"
#define ANSI_DIM                    CSI_S "2m"
#define ANSI_UNDERLINE              CSI_S "4m"
#define ANSI_BLINK                  CSI_S "5m"
#define ANSI_REVERSE                CSI_S "7m"
#define ANSI_HIDDEN                 CSI_S "8m"
#define ANSI_STRIKEOUT              CSI_S "9m"

#define ANSI_NORMAL_INTENSITY       CSI_S "22m"     // BOLD/DIM off
#define ANSI_UNDERLINE_OFF          CSI_S "24m"
#define ANSI_BLINK_OFF              CSI_S "25m"
#define ANSI_REVERSE_OFF            CSI_S "27m"
#define ANSI_HIDDEN_OFF             CSI_S "28m"
#define ANSI_STRIKEOUT_OFF          CSI_S "29m"

// Text colors

#define ANSI_FG_BLACK               CSI_S "38;5;0m"
#define ANSI_FG_RED                 CSI_S "38;5;1m"
#define ANSI_FG_GREEN               CSI_S "38;5;2m"
#define ANSI_FG_YELLOW              CSI_S "38;5;3m"
#define ANSI_FG_BLUE                CSI_S "38;5;4m"
#define ANSI_FG_MAGENTA             CSI_S "38;5;5m"
#define ANSI_FG_CYAN                CSI_S "38;5;6m"
#define ANSI_FG_WHITE               CSI_S "38;5;7m"

#define ANSI_FG_BRIGHT_BLACK        CSI_S "38;5;8m"
#define ANSI_FG_BRIGHT_RED          CSI_S "38;5;9m"
#define ANSI_FG_BRIGHT_GREEN        CSI_S "38;5;10m"
#define ANSI_FG_BRIGHT_YELLOW       CSI_S "38;5;11m"
#define ANSI_FG_BRIGHT_BLUE         CSI_S "38;5;12m"
#define ANSI_FG_BRIGHT_MAGENTA      CSI_S "38;5;13m"
#define ANSI_FG_BRIGHT_CYAN         CSI_S "38;5;14m"
#define ANSI_FG_BRIGHT_WHITE        CSI_S "38;5;15m"

#define ANSI_FG_FMT                 CSI_S "38;5;%um"

#define ANSI_FG_RGB(r,g,b)          CSI_S "38;2;" #r ";" #g ";" #b
#define ANSI_FG_RGB_FMT             CSI_S "38;2;%u;%u;%u"

#define ANSI_BG_BLACK               CSI_S "48;5;0m"
#define ANSI_BG_RED                 CSI_S "48;5;1m"
#define ANSI_BG_GREEN               CSI_S "48;5;2m"
#define ANSI_BG_YELLOW              CSI_S "48;5;3m"
#define ANSI_BG_BLUE                CSI_S "48;5;4m"
#define ANSI_BG_MAGENTA             CSI_S "48;5;5m"
#define ANSI_BG_CYAN                CSI_S "48;5;6m"
#define ANSI_BG_WHITE               CSI_S "48;5;7m"

#define ANSI_BG_BRIGHT_BLACK        CSI_S "48;5;8m"
#define ANSI_BG_BRIGHT_RED          CSI_S "48;5;9m"
#define ANSI_BG_BRIGHT_GREEN        CSI_S "48;5;10m"
#define ANSI_BG_BRIGHT_YELLOW       CSI_S "48;5;11m"
#define ANSI_BG_BRIGHT_BLUE         CSI_S "48;5;12m"
#define ANSI_BG_BRIGHT_MAGENTA      CSI_S "48;5;13m"
#define ANSI_BG_BRIGHT_CYAN         CSI_S "48;5;14m"
#define ANSI_BG_BRIGHT_WHITE        CSI_S "48;5;15m"

#define ANSI_BG_RGB(r,g,b)          CSI_S "48;2;" #r ";" #g ";" #b
#define ANSI_BG_RGB_FMT             CSI_S "48;2;%u;%u;%u"

#define ANSI_DEFAULT_COLOR          ANSI_ATTR_OFF
