#include <_ansi.h>
#include <_syslist.h>
#include <errno.h>
//#include <sys/time.h>
//#include <sys/times.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include "uart_int.h"
#include "stdio_retarget.h"

#if !defined(OS_USE_SEMIHOSTING)

// Comment this out to use HAL polled UART routines
#define USE_UART_INT    1

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

static UART_HandleTypeDef *p_x_stdio_uart;
static unsigned int ui_stdout_after_crlf_char_count;

/******************************************************************************
 *
 ******************************************************************************/

void v_stdio_retarget(UART_HandleTypeDef *p_x_uart)
{
    p_x_stdio_uart = p_x_uart;
    ui_stdout_after_crlf_char_count = 0;

    /* Disable I/O buffering for STDOUT stream, so that
    * chars are sent out as soon as they are printed. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

/******************************************************************************
 *
 ******************************************************************************/

int _isatty(int i_fd)
{
    if ((i_fd >= STDIN_FILENO) && (i_fd <= STDERR_FILENO))
    {
        return 1;
    }

    errno = EBADF;
    return 0;
}

/******************************************************************************
 *
 ******************************************************************************/

int _write(int i_fd, char* p_c_ptr, int i_len)
{
    if ((i_fd == STDOUT_FILENO) || (i_fd == STDERR_FILENO))
    {
        if (i_fd == STDOUT_FILENO)
        {
            // Check for CR/LF chars
            // Reset line char count when CR or LF is seen
            // This is used to assist in clean output formatting
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
#if defined(USE_UART_INT)
        uint16_t u16_size = (uint16_t) i_len;
        u8_uart_tx_multi(p_x_stdio_uart, p_c_ptr, &u16_size, 0xFFFFFFF0);
        return (int) u16_size;
#else
        HAL_StatusTypeDef x_status = HAL_UART_Transmit(p_x_stdio_uart, (uint8_t *) p_c_ptr, i_len, HAL_MAX_DELAY);
        return (x_status == HAL_OK) ? i_len : EIO;
#endif
    }

    errno = EBADF;
    return -1;
}

unsigned int ui_stdout_chars_after_crlf(void)
{
    return ui_stdout_after_crlf_char_count;
}

/******************************************************************************
 *
 ******************************************************************************/

int _close(int i_fd)
{
    if ((i_fd >= STDIN_FILENO) && (i_fd <= STDERR_FILENO))
    {
        return 0;
    }

    errno = EBADF;
    return -1;
}

/******************************************************************************
 *
 ******************************************************************************/

int _lseek(int i_fd, int i_ptr, int i_dir)
{
    (void) i_fd;
    (void) i_ptr;
    (void) i_dir;

    errno = EBADF;
    return -1;
}

/******************************************************************************
 *
 ******************************************************************************/

int _read(int i_fd, char* p_c_ptr, int i_len)
{
    if (i_fd == STDIN_FILENO)
    {
#if defined(USE_UART_INT)
        uint16_t u16_data;
        uint8_t u8_status = u8_uart_rx(p_x_stdio_uart, &u16_data, 0);
        if (u8_status)
        {
            errno = 0;
            return -1;
        }
        else
        {
            *p_c_ptr = (uint8_t) u16_data;
            return 1;
        }
#else
        HAL_StatusTypeDef x_status = HAL_UART_Receive(p_x_stdio_uart, (uint8_t *) p_c_ptr, 1, 0);
        if (x_status == HAL_OK)
        {
            return 1;
        }
        else
        {
            // Don't return 0 if nothing received - doing so will cause STDIN
            // to close or at least fail to try reading again.
            // Returning -1 appears to have the desired effect - operations
            // such as getchar() will return -1 if nothing was read.
            errno = 0;
            return -1;
        }
#endif
    }

    errno = EBADF;
    return -1;
}

/******************************************************************************
 *
 ******************************************************************************/

int _fstat(int i_fd, struct stat *p_x_st)
{
    if ((i_fd >= STDIN_FILENO) && (i_fd <= STDERR_FILENO))
    {
        p_x_st->st_mode = S_IFCHR;
        return 0;
    }

    errno = EBADF;
    return 0;
}

#endif // OS_USE_SEMIHOSTING

/******************************************************************************
 * Stubs for libc system functions that are typically defined in syscalls.c
 ******************************************************************************/

int _getpid(void)
{
  return 1;
}

int _kill(int pid, int sig)
{
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return -1;
}

void _exit (int status)
{
  _kill(status, -1);
  while (1) {}    /* Make sure we hang here */
}
