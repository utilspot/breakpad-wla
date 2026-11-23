// The breakpad tools the module runs, as functions instead of programs, on
// top of Breakpad's static libraries:
// minidump_stackwalk, microdump_stackwalk and dump_syms. Their mains are
// ported from src/processor/minidump_stackwalk.cc,
// src/processor/microdump_stackwalk.cc and src/tools/linux/dump_syms/dump_syms.cc
// with getopt (not thread safe), exit() and freopen(stderr) taken out.

#include "BreakpadBuiltin.h"

#include <paths.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <streambuf>
#include <string>
#include <type_traits>
#include <vector>

#include "common/linux/dump_symbols.h"
#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/microdump.h"
#include "google_breakpad/processor/microdump_processor.h"
#include "google_breakpad/processor/minidump.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "google_breakpad/processor/stack_frame_symbolizer.h"
#include "processor/logging.h"
#include "processor/simple_symbol_supplier.h"
#include "processor/stackwalk_common.h"


namespace {

// The running tool's stderr, for this file's own messages.
FILE* Err() { return BreakpadBuiltin_stream(stderr); }

thread_local FILE* t_out = nullptr;
thread_local FILE* t_err = nullptr;
thread_local bool t_running = false;

// ---- streams ------------------------------------------------------------

// A streambuf writing to a FILE*, for the std::ostream dump_syms writes to.
class FileStreamBuf : public std::streambuf {
 public:
  explicit FileStreamBuf(FILE* file) : file_(file) {}

 protected:
  int_type overflow(int_type ch) override {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
      return traits_type::not_eof(ch);
    return fputc(ch, file_) == EOF ? traits_type::eof() : ch;
  }
  std::streamsize xsputn(const char* s, std::streamsize n) override {
    return (std::streamsize)fwrite(s, 1, (size_t)n, file_);
  }
  int sync() override { return fflush(file_) == 0 ? 0 : -1; }

 private:
  FILE* file_;
};

// Installed on std::clog and std::cerr, where BPLOG writes: inside a tool
// run the text goes to that run's err (or nowhere), elsewhere to the
// original stream.
class ThreadStreamBuf : public std::streambuf {
 public:
  explicit ThreadStreamBuf(std::streambuf* fallback) : fallback_(fallback) {}
  std::streambuf* fallback() const { return fallback_; }

 protected:
  int_type overflow(int_type ch) override {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
      return traits_type::not_eof(ch);
    if (t_running) {
      if (t_err != nullptr)
        fputc(ch, t_err);
      return ch;
    }
    return fallback_->sputc(traits_type::to_char_type(ch));
  }
  std::streamsize xsputn(const char* s, std::streamsize n) override {
    if (t_running) {
      if (t_err != nullptr)
        fwrite(s, 1, (size_t)n, t_err);
      return n;
    }
    return fallback_->sputn(s, n);
  }
  int sync() override {
    if (t_running)
      return t_err != nullptr && fflush(t_err) != 0 ? -1 : 0;
    return fallback_->pubsync();
  }

 private:
  std::streambuf* fallback_;
};

ThreadStreamBuf* s_clogBuf = nullptr;
ThreadStreamBuf* s_cerrBuf = nullptr;

// ---- arguments ----------------------------------------------------------

// getopt(3) without its globals: short options, values attached or separate.
class Args {
 public:
  Args(int argc, const char* const* argv) : argc_(argc), argv_(argv) {}

  // The next option character, -1 after the last one, '?' for an unknown
  // option or a missing value.
  int Next(const char* optstring, const char** value) {
    *value = nullptr;
    if (next_ == nullptr || *next_ == '\0') {
      if (index_ >= argc_ || argv_[index_][0] != '-' || argv_[index_][1] == '\0')
        return -1;
      if (strcmp(argv_[index_], "--") == 0) {
        index_++;
        return -1;
      }
      next_ = argv_[index_++] + 1;
    }
    char ch = *next_++;
    const char* spec = strchr(optstring, ch);
    if (ch == ':' || spec == nullptr)
      return '?';
    if (spec[1] == ':') {
      if (*next_ != '\0')
        *value = next_;
      else if (index_ < argc_)
        *value = argv_[index_++];
      else
        return '?';
      next_ = nullptr;
    }
    return ch;
  }

  int index() const { return index_; }

 private:
  int argc_;
  const char* const* argv_;
  int index_ = 1;
  const char* next_ = nullptr;
};

// ---- minidump_stackwalk -------------------------------------------------

using google_breakpad::BasicSourceLineResolver;
using google_breakpad::Microdump;
using google_breakpad::MicrodumpProcessor;
using google_breakpad::Minidump;
using google_breakpad::MinidumpProcessor;
using google_breakpad::ProcessResult;
using google_breakpad::ProcessState;
using google_breakpad::SimpleSymbolSupplier;
using google_breakpad::StackFrameSymbolizer;

int MinidumpStackwalkMain(int argc, const char* const* argv) {
  bool machine = false;
  bool stack_contents = false;
  bool requesting_thread_only = false;
  bool brief = false;
  bool dump_stack_pointers = false;
  int thread_index = -1;

  Args args(argc, argv);
  const char* value;
  int ch;
  while ((ch = args.Next("bcdf:mst:", &value)) != -1) {
    switch (ch) {
      case 'b': brief = true; break;
      case 'c':
        if (thread_index != -1) {
          fprintf(Err(), "%s: -c and -t cannot be used together.\n", argv[0]);
          return 1;
        }
        requesting_thread_only = true;
        break;
      case 'd': dump_stack_pointers = true; break;
      case 'f':
        if (strcmp(value, "machine") == 0) {
          machine = true;
        } else if (strcmp(value, "human") == 0) {
          machine = false;
        } else {
          fprintf(Err(), "%s: Unknown output format '%s'\n", argv[0], value);
          return 1;
        }
        break;
      case 'm': machine = true; break;
      case 's': stack_contents = true; break;
      case 't':
        if (requesting_thread_only) {
          fprintf(Err(), "%s: -c and -t cannot be used together.\n", argv[0]);
          return 1;
        }
        thread_index = atoi(value);
        break;
      default:
        fprintf(Err(), "Usage: %s [-bcdms] [-f human|machine] [-t index] <minidump-file> [symbol-path ...]\n", argv[0]);
        return 1;
    }
  }
  int index = args.index();
  if (index >= argc) {
    fprintf(Err(), "%s: Missing minidump file\n", argv[0]);
    return 1;
  }
  const char* minidump_file = argv[index++];
  std::vector<std::string> symbol_paths(argv + index, argv + argc);

  std::unique_ptr<SimpleSymbolSupplier> symbol_supplier;
  if (!symbol_paths.empty())
    symbol_supplier.reset(new SimpleSymbolSupplier(symbol_paths));

  BasicSourceLineResolver resolver;
  MinidumpProcessor minidump_processor(symbol_supplier.get(), &resolver);

  Minidump dump(minidump_file);
  if (!dump.Read()) {
    BPLOG(ERROR) << "Minidump " << dump.path() << " could not be read";
    return 1;
  }
  ProcessState process_state;
  if (minidump_processor.Process(&dump, &process_state) != google_breakpad::PROCESS_OK) {
    BPLOG(ERROR) << "MinidumpProcessor::Process failed";
    return 1;
  }

  if (machine) {
    google_breakpad::PrintProcessStateMachineReadable(process_state);
  } else if (brief) {
    google_breakpad::PrintRequestingThreadBrief(process_state);
  } else {
    google_breakpad::PrintProcessState(process_state, stack_contents, dump_stack_pointers,
                                       requesting_thread_only, thread_index, &resolver);
  }
  return 0;
}

// ---- microdump_stackwalk ------------------------------------------------

int MicrodumpStackwalkMain(int argc, const char* const* argv) {
  bool machine = false;
  bool stack_contents = false;

  Args args(argc, argv);
  const char* value;
  int ch;
  while ((ch = args.Next("ms", &value)) != -1) {
    switch (ch) {
      case 'm': machine = true; break;
      case 's': stack_contents = true; break;
      default:
        fprintf(Err(), "Usage: %s [-ms] <microdump-file> [symbol-path ...]\n", argv[0]);
        return 1;
    }
  }
  int index = args.index();
  if (index >= argc) {
    fprintf(Err(), "%s: Missing microdump file\n", argv[0]);
    return 1;
  }
  const char* microdump_file = argv[index++];
  std::vector<std::string> symbol_paths(argv + index, argv + argc);

  std::ifstream file_stream(microdump_file, std::ios::binary);
  std::string microdump_content((std::istreambuf_iterator<char>(file_stream)),
                                std::istreambuf_iterator<char>());
  if (microdump_content.empty()) {
    BPLOG(ERROR) << "Microdump is empty.";
    return 1;
  }

  std::unique_ptr<SimpleSymbolSupplier> symbol_supplier;
  if (!symbol_paths.empty())
    symbol_supplier.reset(new SimpleSymbolSupplier(symbol_paths));

  BasicSourceLineResolver resolver;
  StackFrameSymbolizer frame_symbolizer(symbol_supplier.get(), &resolver);
  ProcessState process_state;
  MicrodumpProcessor microdump_processor(&frame_symbolizer);
  Microdump microdump(microdump_content);
  ProcessResult res = microdump_processor.Process(&microdump, &process_state);
  if (res != google_breakpad::PROCESS_OK) {
    BPLOG(ERROR) << "MicrodumpProcessor::Process failed (code = " << res << ")";
    return 1;
  }

  if (machine) {
    google_breakpad::PrintProcessStateMachineReadable(process_state);
  } else {
    // A microdump has only the crashing thread.
    google_breakpad::PrintProcessState(process_state, stack_contents,
                                       /*dump_stack_pointers=*/false,
                                       /*output_requesting_thread_only=*/false,
                                       /*output_thread_index=*/-1, &resolver);
  }
  return 0;
}

// ---- dump_syms ----------------------------------------------------------

// Breakpad's main branch added a `verbose` flag to DumpOptions (warnings are
// now gated by it rather than by freopen(stderr)); older trees take four
// arguments. A template so the branch that does not fit is never compiled.
template <typename Options = google_breakpad::DumpOptions>
Options MakeDumpOptions(SymbolData symbol_data, bool handle_inter_cu_refs,
                        bool enable_multiple_field, bool preserve_load_address,
                        bool verbose) {
  if constexpr (std::is_constructible_v<Options, SymbolData, bool, bool, bool, bool>) {
    return Options(symbol_data, handle_inter_cu_refs, enable_multiple_field,
                   preserve_load_address, verbose);
  } else {
    (void)verbose;
    return Options(symbol_data, handle_inter_cu_refs, enable_multiple_field,
                   preserve_load_address);
  }
}

int DumpSymsMain(int argc, const char* const* argv) {
  bool preserve_load_address = false;
  bool header_only = false;
  bool cfi = true;
  bool handle_inlines = false;
  bool handle_inter_cu_refs = true;
  bool log_to_stderr = false;
  bool enable_multiple_field = false;
  std::string obj_name;
  std::string module_id;
  const char* obj_os = "Linux";

  Args args(argc, argv);
  const char* value;
  int ch;
  while ((ch = args.Next("iacdrvmb:n:o:", &value)) != -1) {
    switch (ch) {
      case 'i': header_only = true; break;
      case 'a': preserve_load_address = true; break;
      case 'c': cfi = false; break;
      case 'd': handle_inlines = true; break;
      case 'r': handle_inter_cu_refs = false; break;
      case 'v': log_to_stderr = true; break;
      case 'm': enable_multiple_field = true; break;
      case 'b': module_id = value; break;
      case 'n': obj_name = value; break;
      case 'o': obj_os = value; break;
      default:
        fprintf(Err(), "Usage: %s [-iacdrvm] [-b id] [-n name] [-o os] <binary-with-debugging-info> [directories-for-debug-file ...]\n", argv[0]);
        return 1;
    }
  }
  int index = args.index();
  if (index >= argc) {
    fprintf(Err(), "%s: Missing binary\n", argv[0]);
    return 1;
  }
  const char* binary = argv[index++];
  std::vector<std::string> debug_dirs(argv + index, argv + argc);
  if (obj_name.empty())
    obj_name = binary;

  // dump_syms is chatty on stderr; like the tool, keep it only with -v.
  FILE* saved_err = t_err;
  if (!log_to_stderr)
    t_err = nullptr;

  FileStreamBuf out_buf(t_out);
  std::ostream out(&out_buf);
  bool ok;
  if (header_only) {
    ok = google_breakpad::WriteSymbolFileHeader(binary, obj_name, obj_os, module_id, out);
  } else {
    // SymbolData lives in the global namespace (common/symbol_data.h).
    SymbolData symbol_data = (handle_inlines ? INLINES : NO_DATA) |
                             (cfi ? CFI : NO_DATA) | SYMBOLS_AND_FILES;
    google_breakpad::DumpOptions options = MakeDumpOptions(
        symbol_data, handle_inter_cu_refs, enable_multiple_field,
        preserve_load_address, log_to_stderr);
    ok = google_breakpad::WriteSymbolFile(binary, obj_name, obj_os, module_id, debug_dirs,
                                          options, out);
  }
  out.flush();

  t_err = saved_err;
  if (!ok) {
    fprintf(Err(), header_only ? "Failed to process file.\n" : "Failed to write symbol file.\n");
    return 1;
  }
  return 0;
}

}  // namespace

// ---- C interface --------------------------------------------------------

extern "C" {

// A sink for stderr while a tool runs with its err discarded.
static FILE* nullFile() {
  static FILE* file = fopen(_PATH_DEVNULL, "w");
  return file;
}

FILE* BreakpadBuiltin_stream(FILE* stream) {
  if (!t_running)
    return stream;
  if (stream == stdout)
    return t_out != nullptr ? t_out : nullFile();
  if (stream == stderr)
    return t_err != nullptr ? t_err : nullFile();
  return stream;
}

const BreakpadBuiltin kBreakpadBuiltins[] = {
  { BREAKPAD_TOOL_MINIDUMP_STACKWALK,  "Symbolized stack traces of every thread in a minidump", MinidumpStackwalkMain },
  { BREAKPAD_TOOL_MICRODUMP_STACKWALK, "Stack traces from an Android microdump", MicrodumpStackwalkMain },
  { BREAKPAD_TOOL_DUMP_SYMS,           "Generates Breakpad symbols from a binary with debug info", DumpSymsMain },
};

const size_t kBreakpadBuiltinCount = sizeof(kBreakpadBuiltins) / sizeof(kBreakpadBuiltins[0]);

const BreakpadBuiltin* BreakpadBuiltin_find(const char* name) {
  for (size_t index = 0; index < kBreakpadBuiltinCount; index++) {
    if (strcmp(kBreakpadBuiltins[index].name, name) == 0)
      return &kBreakpadBuiltins[index];
  }
  return nullptr;
}

int BreakpadBuiltin_run(const BreakpadBuiltin* tool, int argc, const char* const* argv, FILE* out, FILE* err) {
  t_out = out;
  t_err = err;
  t_running = true;

  int status;
  try {
    status = tool->main(argc, argv);
  } catch (const std::bad_alloc&) {
    fprintf(Err(), "%s: out of memory\n", tool->name);
    status = 1;
  } catch (const std::exception& e) {
    fprintf(Err(), "%s: %s\n", tool->name, e.what());
    status = 1;
  } catch (...) {
    fprintf(Err(), "%s: unexpected exception\n", tool->name);
    status = 1;
  }

  if (out != nullptr)
    fflush(out);
  if (err != nullptr)
    fflush(err);
  t_running = false;
  t_out = nullptr;
  t_err = nullptr;
  return status;
}

void BreakpadBuiltin_initialize(void) {
  // The tools raise these limits before every run; they are process wide.
  google_breakpad::MinidumpThreadList::set_max_threads(std::numeric_limits<uint32_t>::max());
  google_breakpad::MinidumpMemoryList::set_max_regions(std::numeric_limits<uint32_t>::max());

  if (s_clogBuf == nullptr) {
    s_clogBuf = new ThreadStreamBuf(std::clog.rdbuf());
    std::clog.rdbuf(s_clogBuf);
  }
  if (s_cerrBuf == nullptr) {
    s_cerrBuf = new ThreadStreamBuf(std::cerr.rdbuf());
    std::cerr.rdbuf(s_cerrBuf);
  }
}

void BreakpadBuiltin_terminate(void) {
  if (s_clogBuf != nullptr) {
    std::clog.rdbuf(s_clogBuf->fallback());
    delete s_clogBuf;
    s_clogBuf = nullptr;
  }
  if (s_cerrBuf != nullptr) {
    std::cerr.rdbuf(s_cerrBuf->fallback());
    delete s_cerrBuf;
    s_cerrBuf = nullptr;
  }
}

}  // extern "C"
