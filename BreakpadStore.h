#ifndef _BREAKPADSTORE_H
#define _BREAKPADSTORE_H

#include <libnetq/Basic.h>
#include <libnetq/json/JSON.h>

#include <limits.h>

#define BREAKPAD_ID_MAX        64

/*
 * Scratch space for requests. The service keeps no user files: minidumps and
 * symbol files live in the user's browser and arrive with each request. Each
 * request gets its own directory, removed when the request ends:
 *
 *   tmp/job-XXXXXX/part-N/<file>              uploaded files
 *   tmp/job-XXXXXX/symbols/<module>/<id>/...  the request's symbol files, in
 *                                             Breakpad's layout for the
 *                                             stackwalk tools
 */
typedef struct BreakpadStore BreakpadStore;
struct BreakpadStore {
  char dataDir[PATH_MAX];
  char tmpDir[PATH_MAX];
};

int BreakpadStore_init(BreakpadStore*, const char* dataDir);

/* True for a single path component made of [A-Za-z0-9._+@-] not starting with a dot. */
bool BreakpadStore_isSafeName(const char* name);

/* Creates a fresh scratch directory under tmp/. */
bool BreakpadStore_makeJobDir(const BreakpadStore*, char* path, size_t size);
/* Removes a file or a directory tree; missing paths are fine. */
void BreakpadStore_removeTree(const char* path);

/* True if the file starts with the minidump signature "MDMP". */
bool BreakpadStore_isMinidumpFile(const char* path);

typedef struct BreakpadSymbolModule BreakpadSymbolModule;
struct BreakpadSymbolModule {
  char os[64];
  char arch[64];
  char id[BREAKPAD_ID_MAX];
  char name[NAME_MAX + 1];
};

/* Parses the `MODULE <os> <arch> <id> <name>` header of a symbol file. */
bool BreakpadStore_readSymbolModule(const char* path, BreakpadSymbolModule* module);
/* Where `symbolsDir/<name>/<id>/` keeps the symbol file the stackwalk tools look for; false for unsafe names. */
bool BreakpadStore_symbolPath(const char* symbolsDir, const char* name, const char* id, char* path, size_t size);
/*
 * Moves the symbol file `source` into `symbolsDir`, under the module named by
 * its header, replacing a file for the same module and id.
 */
bool BreakpadStore_addSymbol(const char* symbolsDir, const char* source, BreakpadSymbolModule* module, char* error, size_t errorSize);

#endif /* _BREAKPADSTORE_H */
