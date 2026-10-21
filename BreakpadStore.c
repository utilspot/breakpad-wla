#define _GNU_SOURCE
#include "BreakpadStore.h"
#include "BreakpadCType.h"

#include <libnetq/Malloc.h>
#include <libnetq/string/Sprintf.h>
#include <libnetq/string/StringPrint.h>
#include <libnetq/ErrorCode.h>

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SYMBOL_EXTENSION ".sym"
#define PDB_EXTENSION    ".pdb"

static bool formatPath(char* path, size_t size, const char* format, ...) NQ_ATTRIBUTE_PRINTF(3, 4);

static bool formatPath(char* path, size_t size, const char* format, ...)
{
  va_list args;
  va_start(args, format);
  int n = vsnprintf(path, size, format, args);
  va_end(args);
  return n >= 0 && (size_t)n < size;
}

static bool makeDirs(const char* path)
{
  char buffer[PATH_MAX];
  if (!formatPath(buffer, sizeof(buffer), "%s", path))
    return false;

  for (char* ch = buffer + 1; *ch; ch++) {
    if (*ch != '/')
      continue;
    *ch = '\0';
    if (mkdir(buffer, 0755) != 0 && errno != EEXIST)
      return false;
    *ch = '/';
  }
  return mkdir(buffer, 0755) == 0 || errno == EEXIST;
}

static bool copyFile(const char* source, const char* target)
{
  int in = open(source, O_RDONLY | O_CLOEXEC);
  if (in < 0)
    return false;
  int out = open(target, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (out < 0) {
    close(in);
    return false;
  }

  bool ok = true;
  char buffer[65536];
  for (;;) {
    ssize_t n = read(in, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      ok = n == 0;
      break;
    }
    for (ssize_t done = 0; done < n;) {
      ssize_t written = write(out, buffer + done, (size_t)(n - done));
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0) {
        ok = false;
        break;
      }
      done += written;
    }
    if (!ok)
      break;
  }

  close(in);
  if (close(out) != 0)
    ok = false;
  if (!ok)
    unlink(target);
  return ok;
}

/* rename(), falling back to copy and delete across file systems. */
static bool moveFile(const char* source, const char* target)
{
  if (rename(source, target) == 0)
    return true;
  if (errno != EXDEV)
    return false;
  if (!copyFile(source, target))
    return false;
  unlink(source);
  return true;
}

/*
 * Nothing is created here: tmp/ and the data directory are made by the first
 * request that stores a file (BreakpadStore_makeJobDir), so they belong to the
 * user that handles requests, not to a server that starts as root.
 */
int BreakpadStore_init(BreakpadStore* thiz, const char* dataDir)
{
  if (dataDir[0] == '/') {
    if (!formatPath(thiz->dataDir, sizeof(thiz->dataDir), "%s", dataDir))
      return -ENAMETOOLONG;
  }
  else {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
      return -NQGetLastError();
    if (!formatPath(thiz->dataDir, sizeof(thiz->dataDir), "%s/%s", cwd, dataDir))
      return -ENAMETOOLONG;
  }

  if (!formatPath(thiz->tmpDir, sizeof(thiz->tmpDir), "%s/tmp", thiz->dataDir))
    return -ENAMETOOLONG;

  // Scratch directories of requests cut short by a restart are of no use.
  BreakpadStore_removeTree(thiz->tmpDir);
  return 0;
}

bool BreakpadStore_isSafeName(const char* name)
{
  if (name == NULL || name[0] == '\0' || name[0] == '.')
    return false;

  size_t length = 0;
  for (const char* ch = name; *ch; ch++, length++) {
    if (!BreakpadIsSafeNameChar(*ch))
      return false;
  }
  return length <= NAME_MAX - 8;
}

static bool isHexId(const char* id)
{
  size_t length = 0;
  for (const char* ch = id; *ch; ch++, length++) {
    if (!NQIsHexDigit(*ch))
      return false;
  }
  return length != 0 && length < BREAKPAD_ID_MAX;
}

bool BreakpadStore_makeJobDir(const BreakpadStore* thiz, char* path, size_t size)
{
  if (!formatPath(path, size, "%s/job-XXXXXX", thiz->tmpDir))
    return false;
  if (mkdtemp(path) != NULL)
    return true;
  // tmp/ may have been removed while running (a cleaner, a manual rm): make it again.
  if (errno != ENOENT || !makeDirs(thiz->tmpDir))
    return false;
  if (!formatPath(path, size, "%s/job-XXXXXX", thiz->tmpDir))
    return false;
  return mkdtemp(path) != NULL;
}

static int removeEntry(const char* path, const struct stat* st, int flag, struct FTW* ftw)
{
  NQ_UNUSED_PARAM(st);
  NQ_UNUSED_PARAM(flag);
  NQ_UNUSED_PARAM(ftw);
  remove(path);
  return 0;
}

void BreakpadStore_removeTree(const char* path)
{
  nftw(path, removeEntry, 16, FTW_DEPTH | FTW_PHYS);
}

bool BreakpadStore_isMinidumpFile(const char* path)
{
  char signature[4];
  FILE* file = fopen(path, "rb");
  if (file == NULL)
    return false;
  bool ok = fread(signature, 1, sizeof(signature), file) == sizeof(signature) && memcmp(signature, "MDMP", 4) == 0;
  fclose(file);
  return ok;
}

bool BreakpadStore_readSymbolModule(const char* path, BreakpadSymbolModule* module)
{
  char line[1024];
  FILE* file = fopen(path, "rb");
  if (file == NULL)
    return false;
  bool ok = fgets(line, sizeof(line), file) != NULL;
  fclose(file);
  if (!ok)
    return false;

  line[strcspn(line, "\r\n")] = '\0';

  // MODULE <os> <arch> <id> <name>; the name ends at the first space, so a
  // trailing version ("app 1.0.0") is not part of it.
  char* save = NULL;
  char* keyword = strtok_r(line, " ", &save);
  char* os = strtok_r(NULL, " ", &save);
  char* arch = strtok_r(NULL, " ", &save);
  char* id = strtok_r(NULL, " ", &save);
  char* name = strtok_r(NULL, " ", &save);
  if (keyword == NULL || strcmp(keyword, "MODULE") != 0 || os == NULL || arch == NULL || id == NULL || name == NULL)
    return false;

  return formatPath(module->os, sizeof(module->os), "%s", os)
    && formatPath(module->arch, sizeof(module->arch), "%s", arch)
    && formatPath(module->id, sizeof(module->id), "%s", id)
    && formatPath(module->name, sizeof(module->name), "%s", name);
}

bool BreakpadStore_symbolPath(const char* symbolsDir, const char* name, const char* id, char* path, size_t size)
{
  if (!BreakpadStore_isSafeName(name) || !isHexId(id))
    return false;

  // Same file name the stackwalk tools look for: foo.pdb -> foo.sym,
  // libfoo.so -> libfoo.so.sym.
  size_t length = strlen(name);
  size_t pdbLength = sizeof(PDB_EXTENSION) - 1;
  int stem = (int)length;
  if (length > pdbLength && strcasecmp(name + length - pdbLength, PDB_EXTENSION) == 0)
    stem = (int)(length - pdbLength);
  return formatPath(path, size, "%s/%s/%s/%.*s" SYMBOL_EXTENSION, symbolsDir, name, id, stem, name);
}

bool BreakpadStore_addSymbol(const char* symbolsDir, const char* source, BreakpadSymbolModule* module, char* error, size_t errorSize)
{
  if (!BreakpadStore_readSymbolModule(source, module)) {
    NQSnprintf(error, errorSize, "Not a Breakpad symbol file: it must start with 'MODULE <os> <arch> <id> <name>'");
    return false;
  }

  char path[PATH_MAX];
  if (!BreakpadStore_symbolPath(symbolsDir, module->name, module->id, path, sizeof(path))) {
    NQSnprintf(error, errorSize, "Unsupported module name '%s' or id '%s'", module->name, module->id);
    return false;
  }

  char dir[PATH_MAX];
  if (!formatPath(dir, sizeof(dir), "%s/%s/%s", symbolsDir, module->name, module->id) || !makeDirs(dir)) {
    NQSnprintf(error, errorSize, "Cannot create symbol directory: %s", strerror(errno));
    return false;
  }

  if (!moveFile(source, path)) {
    NQSnprintf(error, errorSize, "Cannot place symbol file: %s", strerror(errno));
    return false;
  }
  return true;
}
