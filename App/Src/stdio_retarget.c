/******************************************************************************
 * stdio_retarget.c
 *
 * Minimal newlib stdio retargeting to a polled HAL UART.
 *
 * Provides strong _write / _read that override the weak versions in
 * Core/Src/syscalls.c, routing stdout/stderr/stdin to the console UART passed
 * to v_stdio_retarget(). Reads are NON-BLOCKING: _read returns -1 when no byte
 * is available, so getchar() yields EOF and cooperative pollers (i_getline in
 * utils.c) can keep the main loop serviced while waiting for input.
 *
 * All other libc syscall stubs (_getpid/_kill/_exit/_close/_lseek/_fstat/
 * _isatty/...) live in Core/Src/syscalls.c -- do not duplicate them here.
 ******************************************************************************/

/*============================================================================
 * INCLUDES
 *==========================================================================*/

#include <errno.h>
#include <stdio.h>
#include "main.h"                   /* UART_HandleTypeDef, HAL UART API */
#include "stdio_retarget.h"

#if !defined(OS_USE_SEMIHOSTING)

/*============================================================================
 * CONSTANTS AND MACROS
 *==========================================================================*/

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/*============================================================================
 * PRIVATE VARIABLES
 *==========================================================================*/

static UART_HandleTypeDef *p_x_stdio_uart;
static unsigned int ui_stdout_after_crlf_char_count;

/*============================================================================
 * PUBLIC FUNCTIONS
 *==========================================================================*/

void v_stdio_retarget(UART_HandleTypeDef *p_x_uart)
{
    p_x_stdio_uart = p_x_uart;
    ui_stdout_after_crlf_char_count = 0;

    /* Unbuffered stdio so characters are emitted as soon as they are printed. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

int _write(int i_fd, char *p_c_ptr, int i_len)
{
    if ((i_fd == STDOUT_FILENO) || (i_fd == STDERR_FILENO))
    {
        if (i_fd == STDOUT_FILENO)
        {
            /* Track how many chars have been printed since the last CR/LF, so
             * utils.c can format output cleanly (ui_stdout_chars_after_crlf). */
            for (int i = 0; i < i_len; i++)
            {
                char ch = p_c_ptr[i];
                if (ch == '\r')
                {
                    ui_stdout_after_crlf_char_count = 0;
                }
                else if (ch != '\n')
                {
                    ui_stdout_after_crlf_char_count++;
                }
            }
        }

        HAL_StatusTypeDef x_status =
            HAL_UART_Transmit(p_x_stdio_uart, (uint8_t *) p_c_ptr, i_len, HAL_MAX_DELAY);
        return (x_status == HAL_OK) ? i_len : EIO;
    }

    errno = EBADF;
    return -1;
}

unsigned int ui_stdout_chars_after_crlf(void)
{
    return ui_stdout_after_crlf_char_count;
}

int _read(int i_fd, char *p_c_ptr, int i_len)
{
    (void) i_len;

    if (i_fd == STDIN_FILENO)
    {
        /* Non-blocking single-byte read (timeout 0). Return -1 when idle so
         * getchar() reports EOF and callers can poll cooperatively rather
         * than blocking the main loop. */
        HAL_StatusTypeDef x_status =
            HAL_UART_Receive(p_x_stdio_uart, (uint8_t *) p_c_ptr, 1, 0);
        if (x_status == HAL_OK)
        {
            return 1;
        }

        errno = 0;
        return -1;
    }

    errno = EBADF;
    return -1;
}

#endif /* OS_USE_SEMIHOSTING */
