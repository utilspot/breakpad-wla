#define _GNU_SOURCE
#include "config.h"

#include <libnetq/Array.h>
#include <libnetq/web/WebManifest.h>
#include <libnetq/Library.h>
#include <libnetq/fs/Path.h>
#include <libnetq/Malloc.h>
#include <libnetq/ErrorCode.h>
#include <libnetq/http/HttpHeader.h>
#include <libnetq/http/MediaType.h>
#include <libnetq/Module.h>
#include <libnetq/string/Sprintf.h>
#include <libnetq/web/WebServer.h>
#include <libnetq/web/WebRequest.h>
#include <libnetq/web/WebResponse.h>
#include <libnetq/json/JSONWriter.h>
#include <libnetq/Assert.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "BreakpadStore.h"
#include "BreakpadBody.h"
#include "BreakpadTool.h"

typedef struct WebBreakpadExecutor WebBreakpadExecutor;

typedef struct BreakpadApiRoute BreakpadApiRoute;
struct BreakpadApiRoute {
  struct NQWebRequestListener listener;
  WebBreakpadExecutor* breakpad;
  const struct BreakpadApiRouteInfo* info;
};

#define BREAKPAD_API_ROUTE_COUNT 3

struct WebBreakpadExecutor {
  NQWebExecutor executor;
  NQWebManifestListeners manifestListeners;
  BreakpadStore store;
  NQPath* toolDir;
  BreakpadApiRoute routes[BREAKPAD_API_ROUTE_COUNT];
  size_t routeCount;
};

#define HTTP_UNPROCESSABLE_ENTITY 422

/* Header naming the file of a raw (non-multipart) upload. */
#define HEADER_FILE_NAME "X-File-Name"
/* Data directory, in the server's working directory, when BREAKPAD_DATA_DIR is not set at build time. */
#define DATA_DIR_DEFAULT "breakpad-data"

typedef struct ApiRequest ApiRequest;

typedef int (ApiHandler) (WebBreakpadExecutor*, ApiRequest*, NQWebRequest*, NQWebResponse*);

struct ApiRequest {
  WebBreakpadExecutor* breakpad;
  ApiHandler* handler;
  BreakpadBody body;
};

struct BreakpadApiRouteInfo {
  const char* method;
  const char* path;
  ApiHandler* handler;
};

static bool responseWriteJson(void* userdata, const char* characters, size_t size)
{
  NQWebResponse* response = (NQWebResponse*)userdata;
  int n = NQWebResponse_write(response, characters, size);
  return n < 0 ? false : true;
}

static void setJsonHeaders(NQWebResponse* response)
{
  NQWebResponse_setHeader(response, NQHTTP_HEADER_CONTENT_TYPE, NQ_MEDIATYPE_APPLICATION_JSON);
  NQWebResponse_setHeader(response, NQHTTP_HEADER_CACHE_CONTROL, "no-store");
}

/* Writes `{"error": message}`; returns `status`. */
static int writeError(NQWebResponse* response, int status, const char* message)
{
  NQJSONWriter json;
  setJsonHeaders(response);
  NQJSONWriter_init(&json, responseWriteJson, response);
  NQJSONWriter_writeObjectBegin(&json);
  NQJSONWriter_writeKeyString(&json, "error", message);
  NQJSONWriter_writeObjectEnd(&json);
  NQJSONWriter_finalize(&json);
  return status;
}

static const char* jsonString(NQJSON* json, const char* key)
{
  const char* value = NULL;
  if (!NQJSON_objectGetString(json, key, &value) || value == NULL || value[0] == '\0')
    return NULL;
  return value;
}

static bool jsonBool(NQJSON* json, const char* key)
{
  bool value = false;
  return NQJSON_objectGetBool(json, key, &value) && value;
}

/* Form fields arrive as text: "1", "true" and "on" (an HTML checkbox) mean yes. */
static bool fieldBool(const char* value)
{
  return value != NULL && (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0);
}

/* Parses the JSON options of a POST; answers 400 itself when they are invalid. */
static NQJSON* requestOptions(ApiRequest* api, NQWebResponse* response, int* status)
{
  NQJSON* options = BreakpadBody_json(&api->body);
  if (options == NULL)
    *status = writeError(response, NQ_HTTP_BAD_REQUEST, "Request body must be a JSON object");
  return options;
}

/* `{"error": message, "result": {...}}` for a tool that ran but failed. */
static int toolFailure(NQWebResponse* response, const char* message, const BreakpadToolResult* result)
{
  NQJSONWriter json;
  setJsonHeaders(response);
  NQJSONWriter_init(&json, responseWriteJson, response);
  NQJSONWriter_writeObjectBegin(&json);
  NQJSONWriter_writeKeyString(&json, "error", message);
  NQJSONWriter_writeKey(&json, "result");
  BreakpadToolResult_writeJson(result, &json);
  NQJSONWriter_writeObjectEnd(&json);
  NQJSONWriter_finalize(&json);
  return HTTP_UNPROCESSABLE_ENTITY;
}

/* `{"result": {...}}`: the tool's own exit status tells how it went. */
static int toolResponse(NQWebResponse* response, const BreakpadToolResult* result)
{
  NQJSONWriter json;
  setJsonHeaders(response);
  NQJSONWriter_init(&json, responseWriteJson, response);
  NQJSONWriter_writeObjectBegin(&json);
  NQJSONWriter_writeKey(&json, "result");
  BreakpadToolResult_writeJson(result, &json);
  NQJSONWriter_writeObjectEnd(&json);
  NQJSONWriter_finalize(&json);
  return NQ_HTTP_OK;
}

static int runTool(WebBreakpadExecutor* breakpad, const char* tool, const char* const* args, const BreakpadToolOptions* options, BreakpadToolResult* result, NQWebResponse* response)
{
  if (!BreakpadTool_run(NQPath_characters(breakpad->toolDir), tool, args, options, result)) {
    BreakpadToolResult_finalize(result);
    return writeError(response, NQ_HTTP_INTERNAL_SERVER_ERROR, "Out of memory running the tool");
  }
  return 0;
}

/* Streams a file as a download; `name` must already be safe for a header. */
static int sendFile(NQWebResponse* response, const char* path, const char* contentType, const char* name)
{
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return writeError(response, NQ_HTTP_NOT_FOUND, "File not found");

  char disposition[512];
  NQSnprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", name);
  NQWebResponse_setHeader(response, NQHTTP_HEADER_CONTENT_TYPE, contentType);
  NQWebResponse_setHeader(response, NQHTTP_HEADER_CONTENT_DISPOSITION, disposition);
  NQWebResponse_setHeader(response, NQHTTP_HEADER_CACHE_CONTROL, "no-store");

  char buffer[65536];
  for (;;) {
    ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    if (NQWebResponse_write(response, buffer, (size_t)n) < 0)
      break;
  }
  close(fd);
  return NQ_HTTP_OK;
}

/* ---- minidumps --------------------------------------------------------- */

/* The multipart text field `options` as a JSON object; absent means `{}`. Answers 400 itself when invalid. */
static NQJSON* formOptions(ApiRequest* api, NQWebResponse* response, int* status)
{
  const char* text = BreakpadBody_field(&api->body, "options");
  NQJSON* options = text != NULL && text[0] != '\0' ? NQJSON_parse(text) : NQJSON_createObjectRef();
  if (options != NULL && !NQJSON_isObject(options)) {
    NQJSON_release(options);
    options = NULL;
  }
  if (options == NULL)
    *status = writeError(response, NQ_HTTP_BAD_REQUEST, "The options field must be a JSON object");
  return options;
}

/*
 * Lays out the request's `symbols` file parts (Breakpad .sym files) in
 * `<job>/symbols/<module>/<id>/`, the layout the stackwalk tools read, and
 * puts that directory in `dir`. Symbol files are the user's like the dumps:
 * they arrive with the request and go when it ends. Answers 400 itself for a
 * part that is not a symbol file.
 */
static bool requestSymbols(ApiRequest* api, NQWebResponse* response, char* dir, size_t size, int* status)
{
  const char* jobDir = BreakpadBody_jobDir(&api->body);
  if (jobDir == NULL || NQSnprintf(dir, size, "%s/symbols", jobDir) >= (int)size || mkdir(dir, 0755) != 0) {
    *status = writeError(response, NQ_HTTP_INTERNAL_SERVER_ERROR, "Cannot create a scratch directory");
    return false;
  }

  for (NQListHead* iter = api->body.parts.next; iter != &api->body.parts; iter = iter->next) {
    BreakpadPart* part = NQ_CONTAINER_OF(iter, BreakpadPart, list);
    if (part->filename == NULL || strcmp(part->name, "symbols") != 0)
      continue;

    char error[256];
    char message[512];
    BreakpadSymbolModule module;
    if (!BreakpadStore_addSymbol(dir, part->path, &module, error, sizeof(error))) {
      NQSnprintf(message, sizeof(message), "%s: %s", part->filename, error);
      *status = writeError(response, NQ_HTTP_BAD_REQUEST, message);
      return false;
    }
  }
  return true;
}

/*
 * Minidumps are kept by the user's browser, never by the server: every
 * request carries the dump itself, as the multipart file `minidump` (or the
 * whole raw body), plus an optional `options` field holding a JSON object
 * and, for the stackwalk, the symbol files as `symbols` parts.
 * The dump only exists in the request's job directory, which is removed when
 * the request ends.
 */
static bool minidumpRequest(ApiRequest* api, NQWebResponse* response, BreakpadPart** dump, NQJSON** options, int* status)
{
  // "file" is the whole body of a raw (non-multipart) request.
  *dump = BreakpadBody_file(&api->body, "minidump");
  if (*dump == NULL)
    *dump = BreakpadBody_file(&api->body, "file");
  if (*dump == NULL) {
    *status = writeError(response, NQ_HTTP_BAD_REQUEST, "No minidump in the request (form part 'minidump')");
    return false;
  }
  if (!BreakpadStore_isMinidumpFile((*dump)->path)) {
    *status = writeError(response, NQ_HTTP_BAD_REQUEST, "Not a minidump: the file does not start with 'MDMP'");
    return false;
  }

  *options = formOptions(api, response, status);
  return *options != NULL;
}

static int handleMinidumpStackwalk(WebBreakpadExecutor* breakpad, ApiRequest* api, NQWebRequest* request, NQWebResponse* response)
{
  NQ_UNUSED_PARAM(request);

  int status;
  BreakpadPart* dump;
  NQJSON* options;
  if (!minidumpRequest(api, response, &dump, &options, &status))
    return status;
  const char* path = dump->path;

  char symbolsDir[PATH_MAX];
  if (!requestSymbols(api, response, symbolsDir, sizeof(symbolsDir), &status)) {
    NQJSON_release(options);
    return status;
  }

  const char* args[16];
  size_t argc = 0;
  const char* format = jsonString(options, "format");
  args[argc++] = "-f";
  args[argc++] = format != NULL && NQStrcmp(format, "machine") == 0 ? "machine" : "human";
  if (jsonBool(options, "stack"))
    args[argc++] = "-s";
  if (jsonBool(options, "crashOnly"))
    args[argc++] = "-c";
  if (jsonBool(options, "brief"))
    args[argc++] = "-b";
  if (jsonBool(options, "dumpPointers"))
    args[argc++] = "-d";
  char thread[24];
  int64_t threadIndex;
  if (NQJSON_objectGetInt64(options, "thread", &threadIndex) && threadIndex >= 0) {
    NQSnprintf(thread, sizeof(thread), "%lld", (long long)threadIndex);
    args[argc++] = "-t";
    args[argc++] = thread;
  }
  args[argc++] = path;
  args[argc++] = symbolsDir;
  args[argc] = NULL;

  BreakpadToolResult result;
  status = runTool(breakpad, BREAKPAD_TOOL_MINIDUMP_STACKWALK, args, NULL, &result, response);
  NQJSON_release(options);
  if (status != 0)
    return status;
  status = toolResponse(response, &result);
  BreakpadToolResult_finalize(&result);
  return status;
}

/* ---- symbols ----------------------------------------------------------- */

static bool isElfFile(const char* path)
{
  char magic[4];
  FILE* file = fopen(path, "rb");
  if (file == NULL)
    return false;
  bool ok = fread(magic, 1, sizeof(magic), file) == sizeof(magic) && memcmp(magic, "\x7f" "ELF", 4) == 0;
  fclose(file);
  return ok;
}

/*
 * Runs dump_syms on an uploaded ELF file and answers with the symbol file
 * itself; nothing is stored. The form holds the binary as `binary`, an
 * optional separate debug file as `debug` (dump_syms finds it through the
 * binary's .gnu_debuglink, so it must keep its original name) and the dump_syms
 * options as the JSON field `options`.
 */
static int handleSymbolDump(WebBreakpadExecutor* breakpad, ApiRequest* api, NQWebRequest* request, NQWebResponse* response)
{
  NQ_UNUSED_PARAM(request);

  BreakpadPart* binary = BreakpadBody_file(&api->body, "binary");
  if (binary == NULL)
    return writeError(response, NQ_HTTP_BAD_REQUEST, "No ELF file in the request (form part 'binary')");
  if (!isElfFile(binary->path))
    return writeError(response, NQ_HTTP_BAD_REQUEST, "Not an ELF file: it does not start with 0x7f 'ELF'");

  int status;
  NQJSON* options = formOptions(api, response, &status);
  if (options == NULL)
    return status;

  const char* jobDir = BreakpadBody_jobDir(&api->body);
  char output[PATH_MAX];
  if (jobDir == NULL || NQSnprintf(output, sizeof(output), "%s/dump_syms.sym", jobDir) >= (int)sizeof(output)) {
    NQJSON_release(options);
    return writeError(response, NQ_HTTP_INTERNAL_SERVER_ERROR, "Cannot create a scratch directory");
  }

  const char* args[20];
  size_t argc = 0;
  if (jsonBool(options, "headerOnly"))
    args[argc++] = "-i";
  bool cfi = true;
  NQJSON_objectGetBool(options, "cfi", &cfi);
  if (!cfi)
    args[argc++] = "-c";
  if (jsonBool(options, "inline"))
    args[argc++] = "-d";
  if (jsonBool(options, "noCrossCu"))
    args[argc++] = "-r";
  if (jsonBool(options, "preserveLoadAddress"))
    args[argc++] = "-a";
  if (jsonBool(options, "multiple"))
    args[argc++] = "-m";
  if (jsonBool(options, "verbose"))
    args[argc++] = "-v";
  const char* id = jsonString(options, "id");
  if (id != NULL) {
    args[argc++] = "-b";
    args[argc++] = id;
  }
  const char* name = jsonString(options, "name");
  if (name != NULL) {
    args[argc++] = "-n";
    args[argc++] = name;
  }
  const char* os = jsonString(options, "os");
  if (os != NULL) {
    args[argc++] = "-o";
    args[argc++] = os;
  }
  args[argc++] = binary->path;

  // The debug file sits alone in its part directory, which becomes the
  // directory dump_syms searches for it.
  char debugDir[PATH_MAX];
  BreakpadPart* debug = BreakpadBody_file(&api->body, "debug");
  if (debug != NULL) {
    NQSnprintf(debugDir, sizeof(debugDir), "%s", debug->path);
    char* slash = strrchr(debugDir, '/');
    if (slash != NULL)
      *slash = '\0';
    args[argc++] = debugDir;
  }
  args[argc] = NULL;

  BreakpadToolOptions toolOptions = { .stdoutFile = output };
  BreakpadToolResult result;
  status = runTool(breakpad, BREAKPAD_TOOL_DUMP_SYMS, args, &toolOptions, &result, response);
  NQJSON_release(options);
  if (status != 0)
    return status;

  BreakpadSymbolModule module;
  if (!BreakpadToolResult_succeeded(&result) || !BreakpadStore_readSymbolModule(output, &module)) {
    status = toolFailure(response, "dump_syms could not read symbols from this file", &result);
  }
  else {
    char filename[NAME_MAX + sizeof(".sym")];
    const char* slash = strrchr(binary->path, '/');
    NQSnprintf(filename, sizeof(filename), "%s.sym", BreakpadStore_isSafeName(module.name) ? module.name : (slash ? slash + 1 : "symbols"));
    status = sendFile(response, output, "text/plain; charset=utf-8", filename);
  }
  BreakpadToolResult_finalize(&result);
  return status;
}

/* ---- microdumps -------------------------------------------------------- */

static bool writeTextFile(const char* path, const char* text)
{
  FILE* file = fopen(path, "wb");
  if (file == NULL)
    return false;
  size_t length = strlen(text);
  bool ok = fwrite(text, 1, length, file) == length;
  return fclose(file) == 0 && ok;
}

/*
 * The microdump comes as a file part `microdump`, as a text field
 * `microdump` (pasted logcat output), as JSON {"microdump": "..."} or as the
 * raw body. Options: machine (-m) and stack (-s). Symbol files come as
 * `symbols` parts, as for minidumps.
 */
static int handleMicrodumpStackwalk(WebBreakpadExecutor* breakpad, ApiRequest* api, NQWebRequest* request, NQWebResponse* response)
{
  NQ_UNUSED_PARAM(request);

  int status;
  bool machine;
  bool stack;
  const char* text = NULL;
  NQJSON* options = NULL;

  if (BreakpadBody_isJson(&api->body)) {
    options = requestOptions(api, response, &status);
    if (options == NULL)
      return status;
    text = jsonString(options, "microdump");
    machine = jsonBool(options, "machine");
    stack = jsonBool(options, "stack");
  }
  else {
    text = BreakpadBody_field(&api->body, "microdump");
    machine = fieldBool(BreakpadBody_field(&api->body, "machine"));
    stack = fieldBool(BreakpadBody_field(&api->body, "stack"));
  }

  char path[PATH_MAX];
  BreakpadPart* file = BreakpadBody_file(&api->body, "microdump");
  if (file == NULL)
    file = BreakpadBody_file(&api->body, "file");

  if (file != NULL) {
    NQSnprintf(path, sizeof(path), "%s", file->path);
  }
  else if (text != NULL && text[0] != '\0') {
    const char* jobDir = BreakpadBody_jobDir(&api->body);
    if (jobDir == NULL || NQSnprintf(path, sizeof(path), "%s/microdump.txt", jobDir) >= (int)sizeof(path) || !writeTextFile(path, text)) {
      if (options)
        NQJSON_release(options);
      return writeError(response, NQ_HTTP_INTERNAL_SERVER_ERROR, "Cannot store the microdump");
    }
  }
  else {
    if (options)
      NQJSON_release(options);
    return writeError(response, NQ_HTTP_BAD_REQUEST, "No microdump in the request");
  }

  char symbolsDir[PATH_MAX];
  if (!requestSymbols(api, response, symbolsDir, sizeof(symbolsDir), &status)) {
    if (options)
      NQJSON_release(options);
    return status;
  }

  const char* args[6];
  size_t argc = 0;
  if (machine)
    args[argc++] = "-m";
  if (stack)
    args[argc++] = "-s";
  args[argc++] = path;
  args[argc++] = symbolsDir;
  args[argc] = NULL;

  BreakpadToolResult result;
  status = runTool(breakpad, BREAKPAD_TOOL_MICRODUMP_STACKWALK, args, NULL, &result, response);
  if (options)
    NQJSON_release(options);
  if (status != 0)
    return status;
  status = toolResponse(response, &result);
  BreakpadToolResult_finalize(&result);
  return status;
}

/* ---- routing ----------------------------------------------------------- */

static const struct BreakpadApiRouteInfo kRoutes[BREAKPAD_API_ROUTE_COUNT] = {
  { NQ_HTTP_POST, BREAKPAD_ROOT_URL "minidump_stackwalk",  handleMinidumpStackwalk },
  { NQ_HTTP_POST, BREAKPAD_ROOT_URL "dump_syms",           handleSymbolDump },
  { NQ_HTTP_POST, BREAKPAD_ROOT_URL "microdump_stackwalk", handleMicrodumpStackwalk },
};

static void apiRequestDestroy(ApiRequest* api)
{
  BreakpadBody_finalize(&api->body);
  NQFree(api);
}

static int apiInit(NQWebRequest* request, void* data)
{
  BreakpadApiRoute* route = (BreakpadApiRoute*)data;
  ApiRequest* api = (ApiRequest*)NQMalloc(sizeof(ApiRequest));
  if (api == NULL)
    return -NQ_ENOMEM;

  api->breakpad = route->breakpad;
  api->handler = route->info->handler;
  BreakpadBody_init(&api->body, &route->breakpad->store,
    NQWebRequest_getHeader(request, NQHTTP_HEADER_CONTENT_TYPE),
    NQWebRequest_getHeader(request, HEADER_FILE_NAME));
  request->userdata = api;
  return 0;
}

static size_t apiReceive(NQWebRequest* request, const char* data, size_t size)
{
  ApiRequest* api = (ApiRequest*)request->userdata;
  BreakpadBody_append(&api->body, data, size);
  return size;
}

static int apiHandler(NQWebRequest* request, NQWebResponse* response)
{
  ApiRequest* api = (ApiRequest*)request->userdata;

  int status;
  if (!BreakpadBody_finish(&api->body))
    status = writeError(response, api->body.errorStatus, api->body.error);
  else
    status = api->handler(api->breakpad, api, request, response);

  // CivetWeb never calls release, so the request is cleaned up here.
  apiRequestDestroy(api);
  request->userdata = NULL;
  return status;
}

static void apiRelease(NQWebRequest* request)
{
  if (request->userdata != NULL) {
    apiRequestDestroy((ApiRequest*)request->userdata);
    request->userdata = NULL;
  }
}

static const NQWebRequestOperations kApiOps = {
  .init = apiInit,
  .receive = apiReceive,
  .handler = apiHandler,
  .release = apiRelease,
};

static void removeRoutes(WebBreakpadExecutor* breakpad)
{
  while (breakpad->routeCount != 0) {
    BreakpadApiRoute* route = &breakpad->routes[--breakpad->routeCount];
    NQWebExecutor_removeRequestListener(&breakpad->executor, &route->listener);
  }
}

static int addRoutes(WebBreakpadExecutor* breakpad)
{
  breakpad->routeCount = 0;
  for (size_t index = 0; index < BREAKPAD_API_ROUTE_COUNT; index++) {
    BreakpadApiRoute* route = &breakpad->routes[index];
    route->breakpad = breakpad;
    route->info = &kRoutes[index];
    int ret = NQWebExecutor_addRequestListener(&breakpad->executor, &route->listener, &kApiOps, route, route->info->method, "%s", route->info->path);
    if (ret != 0) {
      removeRoutes(breakpad);
      return ret;
    }
    breakpad->routeCount++;
  }
  return 0;
}

/* BREAKPAD_DATA_DIR from the build, or breakpad-data/ in the server's working directory. */
static int initStore(WebBreakpadExecutor* breakpad)
{
#ifdef BREAKPAD_DATA_DIR
  return BreakpadStore_init(&breakpad->store, BREAKPAD_DATA_DIR);
#else
  char dataDir[PATH_MAX];
  NQSnprintf(dataDir, sizeof(dataDir), "%s/%s", NQWebServer_workDir(breakpad->executor.server), DATA_DIR_DEFAULT);
  return BreakpadStore_init(&breakpad->store, dataDir);
#endif
}

static int executorInit(NQWebExecutor* executor, void* data)
{
  NQ_UNUSED_PARAM(data);

  struct WebBreakpadExecutor* breakpad = (struct WebBreakpadExecutor*)executor;

  NQLibraryInfo info;
  int ret = NQLibraryInfoLoad(&info, &executorInit);
  if (ret != 0)
    return ret;

  NQPath* manifest = NQPath_join3(info.filename, "../../" BREAKPAD_ASSETS_DIR, NQ_WEBMANIFEST_FILE);
  if (manifest == NULL) {
    NQLibraryInfoFinalize(&info);
    return -NQ_ENOMEM;
  }
#ifdef BREAKPAD_TOOLS_DIR
  breakpad->toolDir = NQPath_join1(BREAKPAD_TOOLS_DIR);
#else
  breakpad->toolDir = NQPath_join2(info.filename, "..");
#endif
  NQLibraryInfoFinalize(&info);
  if (breakpad->toolDir == NULL) {
    NQPath_destroy(manifest);
    return -NQ_ENOENT;
  }

  ret = initStore(breakpad);
  if (ret != 0) {
    NQPath_destroy(manifest);
    return ret;
  }

  ret = NQWebManifestListenersInit(executor, &breakpad->manifestListeners, NQPath_characters(manifest));
  NQPath_destroy(manifest);
  if (ret != 0) {
    return ret;
  }

  ret = addRoutes(breakpad);
  if (ret != 0) {
    NQWebManifestListenersFinalize(&breakpad->executor, &breakpad->manifestListeners);
    return ret;
  }

  return ret;
}

static void executorRelease(NQWebExecutor* executor)
{
  struct WebBreakpadExecutor* breakpad = (struct WebBreakpadExecutor*)executor;
  removeRoutes(breakpad);
  NQWebManifestListenersFinalize(&breakpad->executor, &breakpad->manifestListeners);
}

static struct NQWebExecutorOperations s_executorOps = {
  .name = "breakpad",
  .init = executorInit,
  .release = executorRelease,
  .size = sizeof(struct WebBreakpadExecutor),
};

static int moduleInit(NQContext* context)
{
  NQWebExecutorRegister(&s_executorOps);
  return 0;
}

static void moduleExit(NQContext* context)
{
  NQWebExecutorUnregister(&s_executorOps);
}

NQ_MODULE_INIT(moduleInit);
NQ_MODULE_EXIT(moduleExit);
