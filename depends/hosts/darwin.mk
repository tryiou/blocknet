OSX_MIN_VERSION=14.0
OSX_SDK_VERSION=14.0
XCODE_VERSION=26.1.1
XCODE_BUILD_ID=17B100
LLD_VERSION=711
OSX_SDK=$(SDK_PATH)/Xcode-$(XCODE_VERSION)-$(XCODE_BUILD_ID)-extracted-SDK-with-libcxx-headers

# Pure-LLVM Darwin cross toolchain (no cctools): clang/lld/llvm-binutils come
# from the Guix manifest. Follows upstream bitcoin (LLVM-era darwin.mk).
clang_prog=$(shell command -v clang)
clangxx_prog=$(shell command -v clang++)

darwin_AR=$(shell command -v llvm-ar)
darwin_NM=$(shell command -v llvm-nm)
darwin_OBJCOPY=$(shell command -v llvm-objcopy)
darwin_OBJDUMP=$(shell command -v llvm-objdump)
darwin_RANLIB=$(shell command -v llvm-ranlib)
darwin_STRIP=$(shell command -v llvm-strip)
# Apple-compatible archiver (LLVM drop-in replacement for cctools libtool,
# used by b2 Boost and miniupnpc for `-static` archives).
darwin_LIBTOOL=llvm-libtool-darwin

# Flag explanations (see upstream bitcoin depends/hosts/darwin.mk):
#
#     env -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH ...
#
#         Scrub Guix native search-path vars (v25-era technique). Clang honors
#         CPLUS_INCLUDE_PATH/C_INCLUDE_PATH BEFORE its own -stdlib=libc++
#         headers, so the native gcc-12/glibc headers would shadow the macOS
#         SDK otherwise.
#
#     -isysroot$(OSX_SDK) -nostdlibinc
#
#         Disable default include paths built into the compiler as well as
#         those normally included for libc and libc++. The only path that
#         remains implicitly is the clang resource dir.
#
#     -iwithsysroot / -iframeworkwithsysroot
#
#         Adds the desired paths from the SDK.
#
#     -mlinker-version
#
#         Ensures that modern linker features are enabled.
#
#     -platform_version / -no_adhoc_codesign
#
#         Indicate platform/min version/SDK to the linker; disable adhoc
#         codesigning (non-determinism in the Identifier field).
#
#     -Xclang -fno-cxx-modules
#
#         Disable C++ modules: unused, and new SDKs use __has_feature(modules)
#         to define USE_CLANG_TYPES (used as an include guard), which breaks
#         compilation (upstream bitcoin/bitcoin#34036).
#     Apple ld delivery (see build.sh)
#
#         The Apple linker (ld64.lld) is provided as ${HOST}-ld on PATH by
#         build.sh. Two traps make anything else fail (both verified):
#         (1) Guix clang's sibling bindir `ld` is GNU and wins over -B dirs,
#         so -B cannot steer the linker; (2) lld is a multi-call binary
#         dispatching on argv[0] -- a shim named bare `ld` runs in ELF mode.
darwin_CC=env -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH -u OBJC_INCLUDE_PATH -u OBJCPLUS_INCLUDE_PATH -u CPATH -u LIBRARY_PATH \
  $(clang_prog) --target=$(host) -mmacosx-version-min=$(OSX_MIN_VERSION) \
  -isysroot$(OSX_SDK) -nostdlibinc \
  -iwithsysroot/usr/include -iframeworkwithsysroot/System/Library/Frameworks
darwin_CXX=env -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH -u OBJC_INCLUDE_PATH -u OBJCPLUS_INCLUDE_PATH -u CPATH -u LIBRARY_PATH \
  $(clangxx_prog) --target=$(host) -mmacosx-version-min=$(OSX_MIN_VERSION) \
  -isysroot$(OSX_SDK) -nostdlibinc -stdlib=libc++ \
  -iwithsysroot/usr/include/c++/v1 \
  -iwithsysroot/usr/include -iframeworkwithsysroot/System/Library/Frameworks

darwin_CFLAGS=-pipe
darwin_CXXFLAGS=$(darwin_CFLAGS) -Xclang -fno-cxx-modules

darwin_release_CFLAGS=-O2
darwin_release_CXXFLAGS=$(darwin_release_CFLAGS)

darwin_debug_CFLAGS=-O1
darwin_debug_CXXFLAGS=$(darwin_debug_CFLAGS)

ifneq ($(build_os),darwin)
darwin_CFLAGS += -mlinker-version=$(LLD_VERSION)
darwin_CXXFLAGS += -mlinker-version=$(LLD_VERSION)
# NOTE: no -fuse-ld here: this Guix clang rejects LLVM -fuse-ld names
# ("invalid linker name"). The driver finds Apple ld (${HOST}-ld on PATH,
# see build.sh) by itself.
darwin_LDFLAGS=-Wl,-platform_version,macos,$(OSX_MIN_VERSION),$(OSX_SDK_VERSION) -Wl,-no_adhoc_codesign
endif
