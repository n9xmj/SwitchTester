/******************************************************************************
 *
 ******************************************************************************/

#ifndef STDIO_RETARGET_H
#define STDIO_RETARGET_H

#include "stm32g0xx_hal.h"
#include <sys/stat.h>

#include "uart_stream.h"

void v_stdio_retarget(UART_HandleTypeDef *p_x_uart);

/* Switch stdio onto uart_stream's interrupt-driven rings. Until this is called,
 * stdio uses a blocking HAL fallback so early output is not lost. */
void v_stdio_retarget_attach_stream(uart_stream_h_t h_stream);

/* Console stream handle, or UART_STREAM_HANDLE_INVALID while still HAL-backed. */
uart_stream_h_t h_stdio_retarget_get_stream(void);

/* stdout gating -- see stdio_retarget.c. v_stdout_mute(1) discards STDOUT until
 * v_stdout_mute(0); STDERR is unaffected (the always-through echo/error channel).
 * General-purpose: any subsystem may bracket a section with it. */
void v_stdout_mute(uint8_t u8_mute);
uint8_t u8_stdout_is_muted(void);

int _isatty(int i_fd);
int _write(int i_fd, char* p_c_ptr, int i_len);
unsigned int ui_stdout_chars_after_crlf(void);
int _close(int i_fd);
int _lseek(int i_fd, int i_ptr, int i_dir);
int _read(int i_fd, char* p_c_ptr, int i_len);
int _fstat(int i_fd, struct stat *p_x_st);

int _getpid(void);
int _kill(int pid, int sig);
void _exit (int status);

#endif // STDIO_RETARGET_H
