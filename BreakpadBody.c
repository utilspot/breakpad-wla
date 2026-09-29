#define _GNU_SOURCE
#include "BreakpadBody.h"
#include "BreakpadCType.h"

#include <libnetq/Malloc.h>
#include <libnetq/string/Sprintf.h>
#include <libnetq/string/String.h>
#include <libnetq/http/HttpStatus.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
  kBodyNone,
  kBodyRaw,
  kBodyJson,
  kBodyMultipart,
};

#define MAX_BODY_BYTES   (UINT64_C(512) * 1024 * 1024)
#define MAX_FIELD_BYTES  (8u * 1024 * 1024)
#define MAX_JSON_BYTES   (1u * 1024 * 1024)
#define DEFAULT_FILENAME "upload.bin"

static void setError(BreakpadBody* thiz, int status, const char* message)
{
  if (thiz->errorStatus != 0)
    return;
  thiz->errorStatus = status;
  NQSnprintf(thiz->error, sizeof(thiz->error), "%s", message);
}

static char* duplicate(const char* text, size_t length)
{
  char* copy = (char*)NQMalloc(length + 1);
  if (copy != NULL) {
    memcpy(copy, text, length);
    copy[length] = '\0';
  }
  return copy;
}

/*
 * Keeps the last path component of a client supplied file name and replaces
 * anything outside the store's safe set, so it can be used on disk while
 * staying recognisable (dump_syms names the module after it).
 */
static char* sanitizeFilename(const char* filename)
{
  if (filename == NULL)
    return duplicate(DEFAULT_FILENAME, sizeof(DEFAULT_FILENAME) - 1);

  const char* base = filename;
  for (const char* ch = filename; *ch; ch++) {
    if (*ch == '/' || *ch == '\\')
      base = ch + 1;
  }
  while (*base == '.')
    base++;

  size_t length = strlen(base);
  if (length > 200)
    length = 200;
  if (length == 0)
    return duplicate(DEFAULT_FILENAME, sizeof(DEFAULT_FILENAME) - 1);

  char* copy = duplicate(base, length);
  if (copy == NULL)
    return NULL;
  for (char* ch = copy; *ch; ch++) {
    if (!BreakpadIsSafeNameChar(*ch))
      *ch = '_';
  }
  return copy;
}

static void freePart(BreakpadPart* part)
{
  NQFree(part->name);
  NQFree(part->filename);
  NQFree(part->path);
  NQFree(part->value);
  NQFree(part);
}

const char* BreakpadBody_jobDir(BreakpadBody* thiz)
{
  if (thiz->jobDir[0] != '\0')
    return thiz->jobDir;
  if (!BreakpadStore_makeJobDir(thiz->store, thiz->jobDir, sizeof(thiz->jobDir))) {
    thiz->jobDir[0] = '\0';
    return NULL;
  }
  return thiz->jobDir;
}

/* Starts a part; file parts get their own directory so names never clash. */
static bool startPart(BreakpadBody* thiz, const char* name, const char* filename)
{
  BreakpadPart* part = (BreakpadPart*)NQZalloc(sizeof(BreakpadPart));
  if (part == NULL) {
    setError(thiz, NQ_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
    return false;
  }
  NQListHead_init(&part->list);
  part->name = duplicate(name, strlen(name));

  if (filename != NULL) {
    part->filename = duplicate(filename, strlen(filename));
    char* safeName = sanitizeFilename(filename);
    const char* jobDir = BreakpadBody_jobDir(thiz);
    char dir[PATH_MAX];
    char path[PATH_MAX];
    bool ok = part->name != NULL && part->filename != NULL && safeName != NULL && jobDir != NULL
      && NQSnprintf(dir, sizeof(dir), "%s/part-%u", jobDir, thiz->partCount) < (int)sizeof(dir)
      && mkdir(dir, 0755) == 0
      && NQSnprintf(path, sizeof(path), "%s/%s", dir, safeName) < (int)sizeof(path);
    NQFree(safeName);
    if (ok) {
      part->path = duplicate(path, strlen(path));
      thiz->fd = part->path ? open(part->path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
      ok = thiz->fd >= 0;
    }
    if (!ok) {
      freePart(part);
      setError(thiz, NQ_HTTP_INTERNAL_SERVER_ERROR, "Cannot store the uploaded file");
      return false;
    }
  }
  else {
    if (part->name == NULL) {
      freePart(part);
      setError(thiz, NQ_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
      return false;
    }
    NQStringPrint_reset(&thiz->text);
  }

  thiz->partCount++;
  thiz->current = part;
  thiz->partStarted = true;
  return true;
}

static void writePart(BreakpadBody* thiz, const char* data, size_t size)
{
  BreakpadPart* part = thiz->current;
  if (part == NULL || size == 0)
    return;

  part->size += size;
  if (part->filename != NULL) {
    while (size != 0) {
      ssize_t n = write(thiz->fd, data, size);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0) {
        setError(thiz, NQ_HTTP_INTERNAL_SERVER_ERROR, "Cannot write the uploaded file");
        return;
      }
      data += n;
      size -= (size_t)n;
    }
  }
  else if (part->size > MAX_FIELD_BYTES) {
    setError(thiz, NQ_HTTP_REQUEST_ENTITY_TOO_LARGE, "Form field is too large; upload it as a file");
  }
  else if (!NQStringPrint_writeAll(&thiz->text, data, size)) {
    setError(thiz, NQ_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
  }
}

static void endPart(BreakpadBody* thiz)
{
  BreakpadPart* part = thiz->current;
  thiz->current = NULL;
  thiz->partStarted = false;
  if (part == NULL)
    return;

  if (thiz->fd >= 0) {
    if (close(thiz->fd) != 0)
      setError(thiz, NQ_HTTP_INTERNAL_SERVER_ERROR, "Cannot write the uploaded file");
    thiz->fd = -1;
  }
  if (part->filename == NULL) {
    part->value = duplicate(NQStringPrint_characters(&thiz->text), NQStringPrint_length(&thiz->text));
    if (part->value == NULL) {
      freePart(part);
      setError(thiz, NQ_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
      return;
    }
  }
  // A file input left empty in a browser form arrives as a nameless, empty file.
  if (part->filename != NULL && part->filename[0] == '\0' && part->size == 0) {
    unlink(part->path);
    freePart(part);
    return;
  }
  NQListHead_addBack(&thiz->parts, &part->list);
}

static void resetPartHeaders(BreakpadBody* thiz)
{
  NQFree(thiz->partName);
  NQFree(thiz->partFilename);
  thiz->partName = NULL;
  thiz->partFilename = NULL;
  thiz->headerName[0] = '\0';
}

/* Extracts `key="value"` (or key=value) from a header value such as Content-Disposition. */
static char* headerParam(const char* header, size_t length, const char* key)
{
  size_t keyLength = strlen(key);
  const char* end = header + length;
  const char* ch = header;

  while (ch < end) {
    while (ch < end && (*ch == ';' || *ch == ' ' || *ch == '\t'))
      ch++;
    const char* nameStart = ch;
    while (ch < end && *ch != '=' && *ch != ';')
      ch++;
    const char* nameEnd = ch;
    while (nameEnd > nameStart && (nameEnd[-1] == ' ' || nameEnd[-1] == '\t'))
      nameEnd--;
    if (ch >= end || *ch == ';')
      continue;
    ch++; // '='

    const char* valueStart;
    const char* valueEnd;
    if (ch < end && *ch == '"') {
      valueStart = ++ch;
      while (ch < end && *ch != '"') {
        if (*ch == '\\' && ch + 1 < end)
          ch++;
        ch++;
      }
      valueEnd = ch;
      if (ch < end)
        ch++;
    }
    else {
      valueStart = ch;
      while (ch < end && *ch != ';')
        ch++;
      valueEnd = ch;
      while (valueEnd > valueStart && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t'))
        valueEnd--;
    }

    if ((size_t)(nameEnd - nameStart) == keyLength && strncasecmp(nameStart, key, keyLength) == 0)
      return duplicate(valueStart, (size_t)(valueEnd - valueStart));
  }
  return NULL;
}

static bool multipartCallback(void* userdata, NQHTTPMultiPartType type, const char* data, size_t size)
{
  BreakpadBody* thiz = (BreakpadBody*)userdata;
  if (thiz->errorStatus != 0)
    return false;

  switch (type) {
  case kNQHTTPMultiPartBegin:
    break;

  case kNQHTTPMultiPartBoundary:
    endPart(thiz);
    resetPartHeaders(thiz);
    break;

  case kNQHTTPMultiPartFieldName: {
    size_t length = size < sizeof(thiz->headerName) - 1 ? size : sizeof(thiz->headerName) - 1;
    for (size_t index = 0; index < length; index++)
      thiz->headerName[index] = BreakpadToLower(data[index]);
    thiz->headerName[length] = '\0';
    break;
  }

  case kNQHTTPMultiPartFieldValue:
    if (strcmp(thiz->headerName, "content-disposition") == 0) {
      NQFree(thiz->partName);
      NQFree(thiz->partFilename);
      thiz->partName = headerParam(data, size, "name");
      thiz->partFilename = headerParam(data, size, "filename");
    }
    break;

  case kNQHTTPMultiPartBodyPart:
    if (!thiz->partStarted && !startPart(thiz, thiz->partName ? thiz->partName : "", thiz->partFilename))
      return false;
    writePart(thiz, data, size);
    break;

  case kNQHTTPMultiPartFinish:
    endPart(thiz);
    resetPartHeaders(thiz);
    break;
  }

  return thiz->errorStatus == 0;
}

/* The boundary parameter of a multipart Content-Type, or NULL. */
static char* multipartBoundary(const char* contentType)
{
  const char* params = strchr(contentType, ';');
  if (params == NULL)
    return NULL;
  return headerParam(params, strlen(params), "boundary");
}

static bool hasMediaType(const char* contentType, const char* mediaType)
{
  size_t length = strlen(mediaType);
  if (strncasecmp(contentType, mediaType, length) != 0)
    return false;
  char next = contentType[length];
  return next == '\0' || next == ';' || next == ' ';
}

void BreakpadBody_init(BreakpadBody* thiz, const BreakpadStore* store, const char* contentType, const char* rawFilename)
{
  memset(thiz, 0, sizeof(*thiz));
  thiz->store = store;
  thiz->kind = kBodyRaw;
  thiz->fd = -1;
  NQListHead_init(&thiz->parts);
  NQStringPrint_init(&thiz->text);
  if (rawFilename != NULL && rawFilename[0] != '\0')
    thiz->rawFilename = duplicate(rawFilename, strlen(rawFilename));

  if (contentType == NULL)
    return;

  if (hasMediaType(contentType, "multipart/form-data")) {
    thiz->kind = kBodyMultipart;
    char* boundary = multipartBoundary(contentType);
    NQHTTPMultiPartParser_init(&thiz->parser, boundary, multipartCallback, thiz);
    if (boundary == NULL)
      setError(thiz, NQ_HTTP_BAD_REQUEST, "multipart/form-data without a boundary");
    NQFree(boundary);
  }
  else if (hasMediaType(contentType, "application/json")) {
    thiz->kind = kBodyJson;
  }
}

void BreakpadBody_finalize(BreakpadBody* thiz)
{
  if (thiz->fd >= 0)
    close(thiz->fd);
  if (thiz->current != NULL)
    freePart(thiz->current);
  while (!NQListHead_isEmpty(&thiz->parts)) {
    BreakpadPart* part = NQ_CONTAINER_OF(thiz->parts.next, BreakpadPart, list);
    NQListHead_remove(&part->list);
    freePart(part);
  }
  resetPartHeaders(thiz);
  NQFree(thiz->rawFilename);
  NQStringPrint_finalize(&thiz->text);
  if (thiz->jobDir[0] != '\0')
    BreakpadStore_removeTree(thiz->jobDir);
  memset(thiz, 0, sizeof(*thiz));
  thiz->fd = -1;
}

void BreakpadBody_append(BreakpadBody* thiz, const char* data, size_t size)
{
  if (thiz->errorStatus != 0 || size == 0)
    return;

  thiz->received += size;
  if (thiz->received > MAX_BODY_BYTES) {
    setError(thiz, NQ_HTTP_REQUEST_ENTITY_TOO_LARGE, "Request body is too large");
    return;
  }

  switch (thiz->kind) {
  case kBodyMultipart:
    if (!NQHTTPMultiPartParser_append(&thiz->parser, data, size))
      setError(thiz, NQ_HTTP_BAD_REQUEST, "Malformed multipart/form-data body");
    break;

  case kBodyJson:
    if (thiz->received > MAX_JSON_BYTES)
      setError(thiz, NQ_HTTP_REQUEST_ENTITY_TOO_LARGE, "JSON body is too large");
    else if (!NQStringPrint_writeAll(&thiz->text, data, size))
      setError(thiz, NQ_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
    break;

  default:
    if (thiz->current == NULL) {
      char* filename = sanitizeFilename(thiz->rawFilename);
      bool ok = filename != NULL && startPart(thiz, "file", filename);
      NQFree(filename);
      if (!ok)
        return;
    }
    writePart(thiz, data, size);
    break;
  }
}

bool BreakpadBody_finish(BreakpadBody* thiz)
{
  if (thiz->errorStatus == 0) {
    if (thiz->kind == kBodyMultipart) {
      if (thiz->received != 0 && !NQHTTPMultiPartParser_finish(&thiz->parser))
        setError(thiz, NQ_HTTP_BAD_REQUEST, "Incomplete multipart/form-data body");
    }
    else if (thiz->kind == kBodyRaw) {
      endPart(thiz);
    }
  }
  return thiz->errorStatus == 0;
}

bool BreakpadBody_isJson(const BreakpadBody* thiz)
{
  return thiz->kind == kBodyJson;
}

NQJSON* BreakpadBody_json(const BreakpadBody* thiz)
{
  size_t length = NQStringPrint_length(&thiz->text);
  if (thiz->kind != kBodyJson || length == 0)
    return NQJSON_createObjectRef();

  NQJSON* json = NQJSON_parse2(NQStringPrint_characters(&thiz->text), length);
  if (json != NULL && !NQJSON_isObject(json)) {
    NQJSON_release(json);
    return NULL;
  }
  return json;
}

BreakpadPart* BreakpadBody_file(const BreakpadBody* thiz, const char* name)
{
  for (NQListHead* iter = thiz->parts.next; iter != &thiz->parts; iter = iter->next) {
    BreakpadPart* part = NQ_CONTAINER_OF(iter, BreakpadPart, list);
    if (part->filename != NULL && (name == NULL || strcmp(part->name, name) == 0))
      return part;
  }
  return NULL;
}

const char* BreakpadBody_field(const BreakpadBody* thiz, const char* name)
{
  for (NQListHead* iter = thiz->parts.next; iter != &thiz->parts; iter = iter->next) {
    BreakpadPart* part = NQ_CONTAINER_OF(iter, BreakpadPart, list);
    if (part->filename == NULL && strcmp(part->name, name) == 0)
      return part->value;
  }
  return NULL;
}

NQJSON* BreakpadBody_fieldsJson(const BreakpadBody* thiz)
{
  NQJSON* json = NQJSON_createObjectRef();
  if (json == NULL)
    return NULL;
  for (NQListHead* iter = thiz->parts.next; iter != &thiz->parts; iter = iter->next) {
    BreakpadPart* part = NQ_CONTAINER_OF(iter, BreakpadPart, list);
    // Values that are not valid UTF-8 cannot be kept in JSON and are dropped.
    if (part->filename == NULL && part->name[0] != '\0')
      NQJSON_objectSetString(json, part->name, part->value);
  }
  return json;
}
