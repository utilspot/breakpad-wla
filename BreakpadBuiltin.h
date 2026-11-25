#ifndef _BREAKPADBUILTIN_H
#define _BREAKPADBUILTIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* The breakpad tools compiled into the module. */
#define BREAKPAD_TOOL_DUMP_SYMS           "dump_syms"
#define BREAKPAD_TOOL_MICRODUMP_STACKWALK "microdump_stackwalk"
#define BREAKPAD_TOOL_MINIDUMP_STACKWALK  "minidump_stackwalk"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The Breakpad command line tools compiled into the module. Each takes the
 * arguments of the tool itself (argv[0] is the tool name) and returns what
 * the tool's main would; it writes to `out` and `err` instead of stdout and
 * stderr, so several can run at once on different threads.
 */
typedef int (BreakpadBuiltinMain)(int argc, const char* const* argv);

typedef struct BreakpadBuiltin BreakpadBuiltin;
struct BreakpadBuiltin {
  const char* name;
  const char* description;
  BreakpadBuiltinMain* main;
};

extern const BreakpadBuiltin kBreakpadBuiltins[];
extern const size_t kBreakpadBuiltinCount;

/* NULL when `name` is not compiled in. */
const BreakpadBuiltin* BreakpadBuiltin_find(const char* name);

/* Runs `tool` on the calling thread; `err` may be NULL to discard it. */
int BreakpadBuiltin_run(const BreakpadBuiltin* tool, int argc, const char* const* argv, FILE* out, FILE* err);

/*
 * Where output written to `stream` goes: inside a tool run on this thread,
 * stdout and stderr become the run's `out` and `err`; anything else is
 * returned unchanged. BreakpadStdio.c applies it to Breakpad's stdio calls.
 */
FILE* BreakpadBuiltin_stream(FILE* stream);

/* Routes Breakpad's logging (std::clog / std::cerr) to the running tool's `err`. */
void BreakpadBuiltin_initialize(void);
void BreakpadBuiltin_terminate(void);

#ifdef __cplusplus
}
#endif

#endif /* _BREAKPADBUILTIN_H */
