#ifndef _BREAKPADTOOL_H
#define _BREAKPADTOOL_H

#include <libnetq/Basic.h>

#include <libnetq/json/JSONWriter.h>

/* The breakpad command line tools this service knows how to run. */
#define BREAKPAD_TOOL_CORE2MD             "core2md"
#define BREAKPAD_TOOL_DUMP_SYMS           "dump_syms"
#define BREAKPAD_TOOL_MICRODUMP_STACKWALK "microdump_stackwalk"
#define BREAKPAD_TOOL_MINIDUMP_2_CORE     "minidump-2-core"
#define BREAKPAD_TOOL_MINIDUMP_DUMP       "minidump_dump"
#define BREAKPAD_TOOL_MINIDUMP_STACKWALK  "minidump_stackwalk"
#define BREAKPAD_TOOL_MINIDUMP_UPLOAD     "minidump_upload"
#define BREAKPAD_TOOL_PID2MD              "pid2md"
#define BREAKPAD_TOOL_SYM_UPLOAD          "sym_upload"

typedef struct BreakpadToolInfo BreakpadToolInfo;
struct BreakpadToolInfo {
  const char* name;
  const char* description;
};

extern const BreakpadToolInfo kBreakpadTools[];
extern const size_t kBreakpadToolCount;

typedef struct BreakpadToolOptions BreakpadToolOptions;
struct BreakpadToolOptions {
  /* When set, the tool's stdout goes to this file instead of being captured. */
  const char* stdoutFile;
  /* Working directory of the tool; inherited when NULL. */
  const char* workDir;
  /* 0 selects the default. */
  int timeoutMs;
};

typedef struct BreakpadToolResult BreakpadToolResult;
struct BreakpadToolResult {
  char* command;
  /* errno of a failed spawn, 0 when the tool started. */
  int spawnError;
  /* Exit status, or -1 when the tool was killed by a signal. */
  int exitCode;
  int signal;
  bool timedOut;
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
  return thiz->spawnError == 0 && !thiz->timedOut && thiz->exitCode == 0;
}

/*
 * Runs `<toolDir>/<tool> args...` and waits for it, capturing stdout and
 * stderr. `args` is NULL terminated and excludes the program name. Returns
 * false only when the result could not be recorded at all (out of memory);
 * a tool that fails to start or exits non-zero is reported in `result`.
 */
bool BreakpadTool_run(const char* toolDir, const char* tool, const char* const* args, const BreakpadToolOptions* options, BreakpadToolResult* result);

/* Writes `result` as a JSON object value. */
void BreakpadToolResult_writeJson(const BreakpadToolResult*, NQJSONWriter* json);

#endif /* _BREAKPADTOOL_H */
