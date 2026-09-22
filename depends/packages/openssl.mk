package=openssl
$(package)_version=3.5.8
$(package)_download_path=https://www.openssl.org/source
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
# mingwarm64 target (Windows ARM64) backported from upstream/MSYS2; our 3.5.8
# only carries mingw/ming64. Verified to apply cleanly (-p1 dry-run).
$(package)_patches=mingwarm64-target.patch

define $(package)_set_vars
$(package)_config_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CC="$($(package)_cc)"
# NOTE: --libdir=lib is mandatory. OpenSSL 3.x defaults to GNUInstallDirs
# (lib64 on x86_64), which would split host_prefix (Boost and friends
# install to lib) and break BOOST_LDFLAGS probing.
$(package)_config_opts=--prefix=$(host_prefix) --openssldir=$(host_prefix)/etc/openssl --libdir=lib
$(package)_config_opts+=no-shared
# NOTE: no-module is mandatory for mingw: even with no-shared, OpenSSL still
# builds providers as DSOs (ossl-modules/legacy.so), whose version-resource
# step calls an unprefixed `windres` that does not exist in the Guix container
# (Error 127). We link libcrypto/libssl statically with builtin providers only,
# so modules are dead weight on every host (also drops legacy.so on linux).
$(package)_config_opts+=no-module
# NOTE: no-apps: the apps/openssl CLI version-resource step also calls an
# unprefixed `windres` on mingw (same Error 127 as modules). We never ship or
# use the CLI (native postprocess already deletes bin/), so don't build it.
$(package)_config_opts+=no-apps
$(package)_config_opts+=no-docs
$(package)_config_opts+=no-tests
$(package)_config_opts+=no-ssl3
$(package)_config_opts+=no-comp
$(package)_config_opts+=no-idea
$(package)_config_opts+=no-mdc2
$(package)_config_opts+=no-rc5
$(package)_config_opts+=no-zlib
$(package)_config_opts+=$($(package)_cflags) $($(package)_cppflags)
$(package)_config_opts_linux=-fPIC -Wa,--noexecstack
$(package)_config_opts_x86_64_linux=linux-x86_64
$(package)_config_opts_aarch64_linux=linux-aarch64
$(package)_config_opts_x86_64_darwin=darwin64-x86_64-cc
$(package)_config_opts_aarch64_darwin=darwin64-arm64-cc
$(package)_config_opts_x86_64_mingw32=mingw64
# aarch64 mingw uses the mingwarm64 target (aarch64 asm, win64 perlasm);
# mingw64 is x86_64-only and would miscompile.
$(package)_config_opts_aarch64_mingw32=mingwarm64
endef

# NOTE: without _preprocess_cmds, _patches are only COPIED to .patches-*
# (funcs.mk:175-177) and never applied — openssl.mk historically needed no
# patches (upstream mingw64), so this was missing when mingwarm64 was added.
define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/mingwarm64-target.patch
endef

# NOTE: invoked as `perl ./Configure` (not `./Configure`) because the
# 3.x Configure is a perl script with a `#!/usr/bin/env perl` shebang and
# /usr/bin/env does not exist in the Guix container (1.0.1k worked by
# accident: its Configure was a shell script). perl is a manifest input.
define $(package)_config_cmds
  perl ./Configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE) -j1 build_libs
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) -j1 install_sw
endef

define $(package)_postprocess_cmds
  rm -rf share bin etc
endef
