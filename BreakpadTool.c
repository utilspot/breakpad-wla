#define _GNU_SOURCE
#include "BreakpadTool.h"

#include <libnetq/Malloc.h>
#include <libnetq/string/StringPrint.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

#define DEFAULT_TIMEOUT_MS (5 * 60 * 1000)
#define MAX_STDOUT_BYTES   (64u * 1024 * 1024)
#define MAX_STDERR_BYTES   (4u * 1024 * 1024)

const BreakpadToolInfo kBreakpadTools[] = {
  { BREAKPAD_TOOL_MINIDUMP_STACKWALK,  "Symbolized stack traces of every thread in a minidump" },
  { BREAKPAD_TOOL_MINIDUMP_DUMP,       "Raw contents of every stream in a minidump" },
  { BREAKPAD_TOOL_MINIDUMP_2_CORE,     "Converts a minidump into an ELF core file for gdb" },
  { BREAKPAD_TOOL_MINIDUMP_UPLOAD,     "Sends a minidump to a crash collection server" },
  { BREAKPAD_TOOL_MICRODUMP_STACKWALK, "Stack traces from an Android microdump" },
  { BREAKPAD_TOOL_DUMP_SYMS,           "Generates Breakpad symbols from a binary with debug info" },
  { BREAKPAD_TOOL_SYM_UPLOAD,          "Sends a symbol file to a symbol server" },
  { BREAKPAD_TOOL_PID2MD,              "Writes a minidump of a running process on the server" },
  { BREAKPAD_TOOL_CORE2MD,             "Converts a core file plus its /proc files into a minidump" },
};

const size_t kBreakpadToolCount = sizeof(kBreakpadTools) / sizeof(kBreakpadTools[0]);

typedef struct Capture Capture;
struct Capture {
  int fd;
  NQStringPrint buffer;
  size_t limit;
  bool truncated;
};

static int64_t monotonicMs(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* pipe2(O_CLOEXEC) where available; macOS only has it from 27.0. */
static int pipeCloexec(int fds[2])
{
#if defined(__APPLE__)
  if (pipe(fds) != 0)
    return -1;
  if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl(fds[1], F_SETFD, FD_CLOEXEC) != 0) {
    int saved = errno;
    close(fds[0]);
    close(fds[1]);
    fds[0] = fds[1] = -1;
    errno = saved;
    return -1;
  }
  return 0;
#else
  return pipe2(fds, O_CLOEXEC);
#endif
}

/* Reads what is available; returns false at end of stream. */
static bool captureRead(Capture* capture)
{
  char buffer[16384];
  ssize_t n = read(capture->fd, buffer, sizeof(buffer));
  if (n < 0 && (errno == EINTR || errno == EAGAIN))
    return true;
  if (n <= 0)
    return false;

  size_t length = NQStringPrint_length(&capture->buffer);
  size_t room = length < capture->limit ? capture->limit - length : 0;
  size_t take = (size_t)n < room ? (size_t)n : room;
  if (take < (size_t)n)
    capture->truncated = true;
  // Past the limit the output is drained and dropped so the tool never blocks.
  if (take != 0 && !NQStringPrint_writeAll(&capture->buffer, buffer, take))
    capture->truncated = true;
  return true;
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

static char* formatCommand(const char* path, const char* const* args)
{
  NQStringPrint print;
  NQStringPrint_init(&print);

  const char* arg = path;
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

bool BreakpadTool_run(const char* toolDir, const char* tool, const char* const* args, const BreakpadToolOptions* options, BreakpadToolResult* result)
{
  BreakpadToolResult_init(result);

  NQStringPrint pathPrint;
  NQStringPrint_init(&pathPrint);
  NQStringPrint_printf(&pathPrint, "%s/%s", toolDir, tool);
  size_t pathLength;
  char* path = takePrint(&pathPrint, &pathLength);
  if (path == NULL)
    return false;

  result->command = formatCommand(path, args);
  if (result->command == NULL) {
    NQFree(path);
    return false;
  }

  size_t argc = 0;
  while (args[argc] != NULL)
    argc++;
  char** argv = (char**)NQMalloc((argc + 2) * sizeof(char*));
  if (argv == NULL) {
    NQFree(path);
    return false;
  }
  argv[0] = (char*)tool;
  for (size_t index = 0; index < argc; index++)
    argv[index + 1] = (char*)args[index];
  argv[argc + 1] = NULL;

  Capture out = { .fd = -1, .limit = MAX_STDOUT_BYTES };
  Capture err = { .fd = -1, .limit = MAX_STDERR_BYTES };
  NQStringPrint_init(&out.buffer);
  NQStringPrint_init(&err.buffer);

  int outPipe[2] = { -1, -1 };
  int errPipe[2] = { -1, -1 };
  bool captureOut = options == NULL || options->stdoutFile == NULL;
  int timeoutMs = options != NULL && options->timeoutMs > 0 ? options->timeoutMs : DEFAULT_TIMEOUT_MS;

  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attr;
  posix_spawn_file_actions_init(&actions);
  posix_spawnattr_init(&attr);

  int spawnError = 0;
  if ((captureOut && pipeCloexec(outPipe) != 0) || pipeCloexec(errPipe) != 0)
    spawnError = errno;

  if (spawnError == 0) {
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (captureOut)
      posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
    else
      posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, options->stdoutFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, errPipe[1], STDERR_FILENO);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
    if (options != NULL && options->workDir != NULL)
      posix_spawn_file_actions_addchdir_np(&actions, options->workDir);
#endif

    // The web server ignores SIGPIPE and may block signals in its worker
    // threads; the tool gets the defaults back.
    sigset_t defaults;
    sigset_t empty;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigemptyset(&empty);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setsigmask(&attr, &empty);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
  }

  int64_t startMs = monotonicMs();
  pid_t pid = -1;
  if (spawnError == 0)
    spawnError = posix_spawn(&pid, path, &actions, &attr, argv, environ);

  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attr);
  NQFree(argv);
  NQFree(path);

  if (outPipe[1] >= 0)
    close(outPipe[1]);
  if (errPipe[1] >= 0)
    close(errPipe[1]);
  out.fd = outPipe[0];
  err.fd = errPipe[0];

  if (spawnError == 0) {
    int64_t deadline = startMs + timeoutMs;
    while (out.fd >= 0 || err.fd >= 0) {
      struct pollfd fds[2];
      nfds_t count = 0;
      if (out.fd >= 0)
        fds[count++] = (struct pollfd){ .fd = out.fd, .events = POLLIN };
      if (err.fd >= 0)
        fds[count++] = (struct pollfd){ .fd = err.fd, .events = POLLIN };

      int64_t remaining = deadline - monotonicMs();
      if (remaining <= 0) {
        result->timedOut = true;
        kill(pid, SIGKILL);
        break;
      }

      int ready = poll(fds, count, (int)remaining);
      if (ready < 0 && errno != EINTR)
        break;
      for (nfds_t index = 0; ready > 0 && index < count; index++) {
        if (fds[index].revents == 0)
          continue;
        Capture* capture = fds[index].fd == out.fd ? &out : &err;
        if (!captureRead(capture)) {
          close(capture->fd);
          capture->fd = -1;
        }
      }
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status)) {
      result->exitCode = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status)) {
      result->exitCode = -1;
      result->signal = WTERMSIG(status);
    }
  }

  result->spawnError = spawnError;
  result->durationMs = monotonicMs() - startMs;

  if (out.fd >= 0)
    close(out.fd);
  if (err.fd >= 0)
    close(err.fd);

  result->outTruncated = out.truncated;
  result->errTruncated = err.truncated;
  result->out = takePrint(&out.buffer, &result->outLength);
  result->err = takePrint(&err.buffer, &result->errLength);
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
  if (thiz->spawnError == 0 && thiz->signal == 0 && !thiz->timedOut)
    NQJSONWriter_writeInt64(json, thiz->exitCode);
  else
    NQJSONWriter_writeNull(json);
  NQJSONWriter_writeKey(json, "signal");
  if (thiz->signal != 0 && !thiz->timedOut)
    NQJSONWriter_writeString(json, strsignal(thiz->signal));
  else
    NQJSONWriter_writeNull(json);
  NQJSONWriter_writeKeyBool(json, "timedOut", thiz->timedOut);
  NQJSONWriter_writeKeyInt64(json, "durationMs", thiz->durationMs);
  NQJSONWriter_writeKey(json, "stdout");
  NQJSONWriter_writeString2(json, thiz->out ? thiz->out : "", thiz->outLength);
  NQJSONWriter_writeKeyBool(json, "stdoutTruncated", thiz->outTruncated);
  // stderr is still drained so the tool never blocks on it, but not sent.
  NQJSONWriter_writeObjectEnd(json);
}
