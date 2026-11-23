// RUN npx bitmake build

const prefix = "/package";
const destDir = "${binaryRoot}";
const environment = {};

export default {
  "bundle:zlib": {
    sourceUrl: "https://www.zlib.net/fossils/zlib-1.3.2.tar.gz",
    action: "configure",
    variables: {
      prefix,
    },
    features: [
      "static",
    ],
    environment,
    destDir,
  },
  "bundle:openssl": {
    sourceUrl: "https://github.com/openssl/openssl/releases/download/openssl-3.4.0/openssl-3.4.0.tar.gz",
    action: [
      {
        action: "process",
        command: "./Configure",
        args: [
          "zlib",
          "threads",
          "no-shared",
          "no-legacy",
          "no-apps",
          "no-docs",
          "no-tests",
          `--prefix=${prefix}`,
          `--openssldir=${prefix}/etc/ssl`,
          "--with-zlib-include=" + destDir + `${prefix}/include`,
          "--with-zlib-lib=" + destDir + `${prefix}/lib`,
          "--libdir=lib",
        ],
      },
      {
        action: "make",
        args: [ "install" ],
      },
    ],
    binaryDir: "${sourceDir}",
    environment,
    destDir,
  },
  "bundle:civetweb": {
    sourceUrl: "https://github.com/civetweb/civetweb/archive/refs/tags/v1.16.tar.gz",
    action: "cmake",
    cacheVariables: {
      CMAKE_POLICY_VERSION_MINIMUM: "3.5",
      CMAKE_PREFIX_PATH: destDir + prefix,
      CMAKE_INSTALL_PREFIX: prefix,
      BUILD_SHARED_LIBS: false,
      BUILD_TESTING: false,
      CIVETWEB_ENABLE_ASAN: false,
      CIVETWEB_BUILD_TESTING: false,
      CIVETWEB_ENABLE_SERVER_EXECUTABLE: false,
      CIVETWEB_ENABLE_WEBSOCKETS: true,
      CIVETWEB_ENABLE_ZLIB: true,
    },
    environment,
    destDir,
  },
  "bundle:jansson": {
    sourceUrl: "https://github.com/akheron/jansson/releases/download/v2.15.0/jansson-2.15.0.tar.gz",
    action: "cmake",
    cacheVariables: {
      CMAKE_PREFIX_PATH: destDir + prefix,
      CMAKE_INSTALL_PREFIX: prefix,
      JANSSON_BUILD_SHARED_LIBS: false,
      JANSSON_BUILD_DOCS: false,
      JANSSON_EXAMPLES: false,
    },
    environment,
    destDir,
  },
  "bundle:breakpad": {
    sourceUrl: {
      ".": "https://chromium.googlesource.com/breakpad/breakpad/+archive/refs/heads/main.tar.gz",
      "./src/testing": "https://github.com/google/googletest/archive/refs/heads/main.tar.gz",
      "./src/third_party/lss": "https://chromium.googlesource.com/linux-syscall-support/+archive/refs/heads/main.tar.gz",
    },
    action: "configure",
    variables: {
      prefix: prefix,
      "with-zlib": destDir + prefix,
    },
    environment: {
      ...environment,
      CPPFLAGS: "-I" + destDir + prefix + "/include",
      LDFLAGS: "-L" + destDir + prefix + "/lib",
    },
    // Breakpad installs libbreakpad.a (the processor) but neither the code of
    // dump_syms, nor libdisasm (which the processor needs), nor the
    // processor's public headers. The module links the three libraries
    // statically, so they are packed from the build tree into the prefix.
    postAction: {
      action: "process",
      command: "sh",
      args: [
        "-c",
        [
          "set -e",
          "lib='" + destDir + prefix + "/lib'",
          "include='" + destDir + prefix + "/include/breakpad'",
          "rm -f \"$lib/libbreakpad_dump_syms.a\"",
          "ar rcs \"$lib/libbreakpad_dump_syms.a\" src/common/tools_linux_dump_syms_dump_syms-*.o src/common/dwarf/tools_linux_dump_syms_dump_syms-*.o src/common/linux/tools_linux_dump_syms_dump_syms-*.o",
          "cp src/third_party/libdisasm/libdisasm.a \"$lib/libdisasm.a\"",
          "mkdir -p \"$include/google_breakpad/processor\"",
          "cp ${bundle:breakpad.sourceDir}/src/google_breakpad/processor/*.h \"$include/google_breakpad/processor/\"",
        ].join("\n"),
      ],
    },
    destDir,
  },
  "bundle:libnetq": {
    sourceUrl: "https://github.com/libnetq/libnetq/archive/refs/heads/20261011_dev120.tar.gz",
    action: "cmake",
    cacheVariables: {
      CMAKE_PREFIX_PATH: destDir + prefix,
      CMAKE_INSTALL_PREFIX: prefix,
      LNQ_LIBRARY_TYPE: "SHARED",
      LNQ_WITH_ZLIB: true,
      LNQ_WITH_OPENSSL: true,
      LNQ_WITH_CIVETWEB: true,
      LNQ_JSON_BACKEND: "JANSSON",
    },
    buildType: "Debug",
    environment,
    destDir,
  },
  "bundle:output": {
    action: "cmake",
    cacheVariables: {
      CMAKE_MODULE_PATH: destDir + `${prefix}/share/libnetq/cmake`,
      CMAKE_PREFIX_PATH: destDir + prefix,
      CMAKE_INSTALL_PREFIX: prefix,
      BREAKPAD_BASE_URL: "/",
    },
    sourceDir: "${sourceRoot}",
    buildType: "Debug",
    rebuild: true,
    environment,
    destDir,
  },
};
