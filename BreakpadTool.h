#ifndef _BREAKPADTOOL_H
#define _BREAKPADTOOL_H

#include <libnetq/Basic.h>

#include <libnetq/json/JSONWriter.h>

#include "BreakpadBuiltin.h"

typedef struct BreakpadToolOptions BreakpadToolOptions;
struct BreakpadToolOptions {
  /* When set, the tool's stdout goes to this file instead of being captured. */
  const char* stdoutFile;
};

typedef struct BreakpadToolResult BreakpadToolResult;
struct BreakpadToolResult {
  char* command;
  /* errno when the tool could not be started (unknown tool, output file), 0 when it ran. */
  int spawnError;
  /* What the tool's main returned. */
  int exitCode;
  int64_t durationMs;

  char* out;
  size_t outLength;
  bool outTruncated;

  char* err;
  size_t errLength;
  bool errTruncated;
};

void BreakpadToolResult_init(BreakpadToolResult*);
void BreakpadToolResult_finalize(BreakpadToolResult*);

static inline bool BreakpadToolResult_succeeded(const BreakpadToolResult* thiz)
{
  return thiz->spawnError == 0 && thiz->exitCode == 0;
}

/*
 * Runs the built-in `tool` with `args` on the calling thread, capturing its
 * stdout and stderr. `args` is NULL terminated and excludes the program name.
 * Returns false only when the result could not be recorded at all (out of
 * memory); a tool that cannot start or fails is reported in `result`.
 */
bool BreakpadTool_run(const char* tool, const char* const* args, const BreakpadToolOptions* options, BreakpadToolResult* result);

/* Writes `result` as a JSON object value. */
void BreakpadToolResult_writeJson(const BreakpadToolResult*, NQJSONWriter* json);

#endif /* _BREAKPADTOOL_H */
