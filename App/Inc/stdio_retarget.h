/******************************************************************************
 *
 ******************************************************************************/

#ifndef STDIO_RETARGET_H
#define STDIO_RETARGET_H

#include "stm32g0xx_hal.h"
#include <sys/stat.h>

void v_stdio_retarget(UART_HandleTypeDef *p_x_uart);
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
