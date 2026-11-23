/*
 * Breakpad's static libraries print to stdout and stderr. The module links
 * with -Wl,--wrap=<function> for each stdio call they make (see
 * BREAKPAD_WRAPPED_STDIO in CMakeLists.txt), so every such call in
 * breakpad_wla.so lands here: inside a tool run, stdout and stderr become
 * that run's own streams (BreakpadBuiltin_stream); elsewhere the call goes
 * through unchanged.
 *
 * The _chk variants are what glibc's _FORTIFY_SOURCE turns printf and
 * friends into; Breakpad is built with it when the compiler enables it.
 */
#include "BreakpadBuiltin.h"

#include <stdarg.h>
#include <stdio.h>

int __real_vfprintf(FILE* stream, const char* format, va_list ap);
int __real_fputs(const char* s, FILE* stream);
int __real_fputc(int c, FILE* stream);
size_t __real_fwrite(const void* ptr, size_t size, size_t count, FILE* stream);

int __wrap_vfprintf(FILE* stream, const char* format, va_list ap)
{
  return __real_vfprintf(BreakpadBuiltin_stream(stream), format, ap);
}

int __wrap___vfprintf_chk(FILE* stream, int flag, const char* format, va_list ap)
{
  (void)flag;
  return __real_vfprintf(BreakpadBuiltin_stream(stream), format, ap);
}

int __wrap_vprintf(const char* format, va_list ap)
{
  return __real_vfprintf(BreakpadBuiltin_stream(stdout), format, ap);
}

int __wrap___vprintf_chk(int flag, const char* format, va_list ap)
{
  (void)flag;
  return __real_vfprintf(BreakpadBuiltin_stream(stdout), format, ap);
}

int __wrap_fprintf(FILE* stream, const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  int n = __real_vfprintf(BreakpadBuiltin_stream(stream), format, ap);
  va_end(ap);
  return n;
}

int __wrap___fprintf_chk(FILE* stream, int flag, const char* format, ...)
{
  (void)flag;
  va_list ap;
  va_start(ap, format);
  int n = __real_vfprintf(BreakpadBuiltin_stream(stream), format, ap);
  va_end(ap);
  return n;
}

int __wrap_printf(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  int n = __real_vfprintf(BreakpadBuiltin_stream(stdout), format, ap);
  va_end(ap);
  return n;
}

int __wrap___printf_chk(int flag, const char* format, ...)
{
  (void)flag;
  va_list ap;
  va_start(ap, format);
  int n = __real_vfprintf(BreakpadBuiltin_stream(stdout), format, ap);
  va_end(ap);
  return n;
}

int __wrap_fputs(const char* s, FILE* stream)
{
  return __real_fputs(s, BreakpadBuiltin_stream(stream));
}

int __wrap_puts(const char* s)
{
  FILE* stream = BreakpadBuiltin_stream(stdout);
  if (__real_fputs(s, stream) == EOF || __real_fputc('\n', stream) == EOF)
    return EOF;
  return 1;
}

int __wrap_fputc(int c, FILE* stream)
{
  return __real_fputc(c, BreakpadBuiltin_stream(stream));
}

int __wrap_putc(int c, FILE* stream)
{
  return __real_fputc(c, BreakpadBuiltin_stream(stream));
}

int __wrap_putchar(int c)
{
  return __real_fputc(c, BreakpadBuiltin_stream(stdout));
}

size_t __wrap_fwrite(const void* ptr, size_t size, size_t count, FILE* stream)
{
  return __real_fwrite(ptr, size, count, BreakpadBuiltin_stream(stream));
}
