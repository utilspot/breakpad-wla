#define _GNU_SOURCE
#include "BreakpadTool.h"

#include <libnetq/Malloc.h>
#include <libnetq/string/StringPrint.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_STDOUT_BYTES (64u * 1024 * 1024)
#define MAX_STDERR_BYTES (4u * 1024 * 1024)

static int64_t monotonicMs(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Moves the printed text into a NUL terminated heap string and finalizes `print`. */
static char* takePrint(NQStringPrint* print, size_t* length)
{
  *length = NQStringPrint_length(print);
  char* copy = (char*)NQMalloc(*length + 1);
  if (copy != NULL) {
    if (*length != 0)
      memcpy(copy, NQStringPrint_characters(print), *length);
    copy[*length] = '\0';
  }
  else {
    *length = 0;
  }
  NQStringPrint_finalize(print);
  return copy;
}

/* An NQMalloc copy of at most `limit` bytes of `text`; `text` is freed. */
static char* takeCapture(char* text, size_t size, size_t limit, size_t* length, bool* truncated)
{
  *truncated = size > limit;
  *length = size > limit ? limit : size;
  char* copy = (char*)NQMalloc(*length + 1);
  if (copy != NULL) {
    if (*length != 0)
      memcpy(copy, text, *length);
    copy[*length] = '\0';
  }
  else {
    *length = 0;
  }
  free(text);
  return copy;
}

static bool needsQuoting(const char* arg)
{
  if (*arg == '\0')
    return true;
  for (; *arg; arg++) {
    if (strchr(" \t\n'\"\\$`;&|<>()*?[]{}!#~", *arg) != NULL)
      return true;
  }
  return false;
}

/* The equivalent command line, for display. */
static char* formatCommand(const char* tool, const char* const* args)
{
  NQStringPrint print;
  NQStringPrint_init(&print);

  const char* arg = tool;
  for (size_t index = 0; arg != NULL; arg = args[index++]) {
    if (index != 0)
      NQStringPrint_writeAll(&print, " ", 1);
    if (!needsQuoting(arg)) {
      NQStringPrint_writeAll(&print, arg, strlen(arg));
      continue;
    }
    NQStringPrint_writeAll(&print, "'", 1);
    for (const char* ch = arg; *ch; ch++) {
      if (*ch == '\'')
        NQStringPrint_writeAll(&print, "'\\''", 4);
      else
        NQStringPrint_writeAll(&print, ch, 1);
    }
    NQStringPrint_writeAll(&print, "'", 1);
  }

  size_t length;
  return takePrint(&print, &length);
}

void BreakpadToolResult_init(BreakpadToolResult* thiz)
{
  memset(thiz, 0, sizeof(*thiz));
  thiz->exitCode = -1;
}

void BreakpadToolResult_finalize(BreakpadToolResult* thiz)
{
  NQFree(thiz->command);
  NQFree(thiz->out);
  NQFree(thiz->err);
  BreakpadToolResult_init(thiz);
}

bool BreakpadTool_run(const char* tool, const char* const* args, const BreakpadToolOptions* options, BreakpadToolResult* result)
{
  BreakpadToolResult_init(result);

  result->command = formatCommand(tool, args);
  if (result->command == NULL)
    return false;

  size_t argc = 0;
  while (args[argc] != NULL)
    argc++;
  const char** argv = (const char**)NQMalloc((argc + 2) * sizeof(char*));
  if (argv == NULL)
    return false;
  argv[0] = tool;
  for (size_t index = 0; index < argc; index++)
    argv[index + 1] = args[index];
  argv[argc + 1] = NULL;

  char* outText = NULL;
  size_t outSize = 0;
  char* errText = NULL;
  size_t errSize = 0;
  FILE* out = NULL;
  FILE* err = NULL;

  const BreakpadBuiltin* builtin = BreakpadBuiltin_find(tool);
  if (builtin == NULL) {
    result->spawnError = ENOENT;
  }
  else {
    if (options != NULL && options->stdoutFile != NULL)
      out = fopen(options->stdoutFile, "we");
    else
      out = open_memstream(&outText, &outSize);
    if (out == NULL)
      result->spawnError = errno;
    else if ((err = open_memstream(&errText, &errSize)) == NULL)
      result->spawnError = errno;
  }

  int64_t startMs = monotonicMs();
  if (result->spawnError == 0)
    result->exitCode = BreakpadBuiltin_run(builtin, (int)argc + 1, argv, out, err);
  result->durationMs = monotonicMs() - startMs;
  NQFree(argv);

  // Closing a memstream sets its buffer and size for good.
  if (out != NULL && fclose(out) != 0 && result->exitCode == 0)
    result->exitCode = 1;
  if (err != NULL)
    fclose(err);

  result->out = takeCapture(outText, outSize, MAX_STDOUT_BYTES, &result->outLength, &result->outTruncated);
  result->err = takeCapture(errText, errSize, MAX_STDERR_BYTES, &result->errLength, &result->errTruncated);
  return result->out != NULL && result->err != NULL;
}

void BreakpadToolResult_writeJson(const BreakpadToolResult* thiz, NQJSONWriter* json)
{
  NQJSONWriter_writeObjectBegin(json);
  NQJSONWriter_writeKeyString(json, "command", thiz->command);
  NQJSONWriter_writeKeyBool(json, "ok", BreakpadToolResult_succeeded(thiz));
  if (thiz->spawnError != 0)
    NQJSONWriter_writeKeyString(json, "spawnError", strerror(thiz->spawnError));
  NQJSONWriter_writeKey(json, "exitCode");
  if (thiz->spawnError == 0)
    NQJSONWriter_writeInt64(json, thiz->exitCode);
  else
    NQJSONWriter_writeNull(json);
  // The tools run in the module now: there is no signal or timeout to report.
  NQJSONWriter_writeKey(json, "signal");
  NQJSONWriter_writeNull(json);
  NQJSONWriter_writeKeyBool(json, "timedOut", false);
  NQJSONWriter_writeKeyInt64(json, "durationMs", thiz->durationMs);
  NQJSONWriter_writeKey(json, "stdout");
  NQJSONWriter_writeString2(json, thiz->out ? thiz->out : "", thiz->outLength);
  NQJSONWriter_writeKeyBool(json, "stdoutTruncated", thiz->outTruncated);
  // stderr is captured for diagnostics but not sent.
  NQJSONWriter_writeObjectEnd(json);
}
