#ifndef _BREAKPADBODY_H
#define _BREAKPADBODY_H

#include <libnetq/Basic.h>
#include <libnetq/List.h>
#include <libnetq/json/JSON.h>
#include <libnetq/http/HttpMultiPartParser.h>
#include <libnetq/string/StringPrint.h>

#include "BreakpadStore.h"

/*
 * Request body of an API call, received while it streams in:
 *
 *   multipart/form-data  every part with a filename is written to its own file
 *                        under the request's job directory, keeping the
 *                        uploaded file name; other parts are kept as fields.
 *                        This is what minidump_upload, sym_upload, Crashpad
 *                        and browsers send.
 *   application/json     kept in memory, see BreakpadBody_json().
 *   anything else        the whole body is one file part named "file".
 */
typedef struct BreakpadPart BreakpadPart;
struct BreakpadPart {
  NQListHead list;
  char* name;
  /* NULL for a plain form field. */
  char* filename;
  /* File parts: where the bytes were written. */
  char* path;
  /* Plain fields: the value, NUL terminated. */
  char* value;
  uint64_t size;
};

typedef struct BreakpadBody BreakpadBody;
struct BreakpadBody {
  const BreakpadStore* store;
  int kind;
  char jobDir[PATH_MAX];
  NQListHead parts;
  unsigned partCount;
  uint64_t received;

  NQHTTPMultiPartParser parser;
  char headerName[32];
  char* partName;
  char* partFilename;
  bool partStarted;
  BreakpadPart* current;
  int fd;

  NQStringPrint text;
  char* rawFilename;

  int errorStatus;
  char error[192];
};

/* `rawFilename` names the file of a non-multipart body and may be NULL. */
void BreakpadBody_init(BreakpadBody*, const BreakpadStore* store, const char* contentType, const char* rawFilename);
void BreakpadBody_finalize(BreakpadBody*);

void BreakpadBody_append(BreakpadBody*, const char* data, size_t size);
/* Completes the body; false (with errorStatus and error set) if it was bad. */
bool BreakpadBody_finish(BreakpadBody*);

/* The request's scratch directory, created on first use; NULL on failure. */
const char* BreakpadBody_jobDir(BreakpadBody*);

bool BreakpadBody_isJson(const BreakpadBody*);
/* Parses a JSON body; an empty body is an empty object. NULL if invalid. */
NQJSON* BreakpadBody_json(const BreakpadBody*);

/* First file part named `name`, or the first file part at all when `name` is NULL. */
BreakpadPart* BreakpadBody_file(const BreakpadBody*, const char* name);
const char* BreakpadBody_field(const BreakpadBody*, const char* name);

/* Form fields as a JSON object of strings, for storing with an upload. */
NQJSON* BreakpadBody_fieldsJson(const BreakpadBody*);

#endif /* _BREAKPADBODY_H */
