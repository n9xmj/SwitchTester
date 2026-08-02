/******************************************************************************
 * stdio_retarget.c
 *
 * Minimal newlib stdio retargeting to the console UART.
 *
 * Provides strong _write / _read that override the weak versions in
 * Core/Src/syscalls.c. Reads are NON-BLOCKING: _read returns -1 when no byte is
 * available, so getchar() yields EOF and cooperative pollers (i_getline in
 * utils.c) can keep the main loop serviced while waiting for input.
 *
 * Transport: once the console UART has been bound to uart_stream (see
 * v_stdio_retarget_attach_stream), stdio runs on its interrupt-driven rings --
 * _write only stalls when the TX ring is full, and RX is captured by the ISR
 * rather than polled. Before that binding happens, stdio falls back to the
 * blocking HAL path so that early output (notably the start-up banner, printed
 * before v_hardware_init runs) is not lost.
 *
 * The fallback is safe precisely because it is only reachable while unbound:
 * binding sets the HAL handle to HAL_UART_STATE_BUSY, after which every
 * HAL_UART_* call would return HAL_BUSY rather than touching the peripheral.
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
#include "device_config.h"          /* DEV_CONFIG_CONSOLE_* */
#include "stdio_retarget.h"
#include "uart_stream.h"

#if !defined(OS_USE_SEMIHOSTING)

/*============================================================================
 * CONSTANTS AND MACROS
 *==========================================================================*/

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* Deadline for a single _write when the TX ring is full. Sized well above the
 * time to drain a full ring at the console baud: 1023 bytes at 921600 8N1 is
 * about 11 ms. */
#define STDIO_TX_TIMEOUT_MS     100U

/*============================================================================
 * PRIVATE VARIABLES
 *==========================================================================*/

static UART_HandleTypeDef *p_x_stdio_uart;
static uart_stream_h_t     h_stdio_stream = UART_STREAM_HANDLE_INVALID;
static unsigned int        ui_stdout_after_crlf_char_count;

/*============================================================================
 * PUBLIC FUNCTIONS
 *==========================================================================*/

void v_stdio_retarget(UART_HandleTypeDef *p_x_uart)
{
    p_x_stdio_uart = p_x_uart;
    h_stdio_stream = UART_STREAM_HANDLE_INVALID;
    ui_stdout_after_crlf_char_count = 0;

    /* Unbuffered stdio so characters are emitted as soon as they are printed. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

void v_stdio_retarget_attach_stream(uart_stream_h_t h_stream)
{
    h_stdio_stream = h_stream;
}

uart_stream_h_t h_stdio_retarget_get_stream(void)
{
    return h_stdio_stream;
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

        if (h_stdio_stream != UART_STREAM_HANDLE_INVALID)
        {
            uint16_t u16_sent =
                u16_uart_stream_tx_multi_blocking(h_stdio_stream,
                                                  (const uint8_t *) p_c_ptr,
                                                  (uint16_t) i_len,
                                                  STDIO_TX_TIMEOUT_MS);
            return (u16_sent == (uint16_t) i_len) ? i_len : EIO;
        }

        /* Pre-bind fallback: blocking HAL. Only reachable while the console is
         * still HAL-owned. */
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
        /* Non-blocking single-byte read. Return -1 when idle so getchar()
         * reports EOF and callers can poll cooperatively rather than blocking
         * the main loop. */
        if (h_stdio_stream != UART_STREAM_HANDLE_INVALID)
        {
            int16_t i16_byte = i16_uart_stream_rx_byte(h_stdio_stream);

            if (i16_byte >= 0)
            {
                *p_c_ptr = (char) i16_byte;
                return 1;
            }
            errno = 0;
            return -1;
        }

        /* Pre-bind fallback: polled HAL, timeout 0. */
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
