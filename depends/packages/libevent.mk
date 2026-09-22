package=libevent
$(package)_version=2.1.12-stable
$(package)_download_path=https://github.com/libevent/libevent/releases/download/release-$($(package)_version)/
$(package)_file_name=libevent-$($(package)_version).tar.gz
$(package)_sha256_hash=92e6de1be9ec176428fd2367677e61ceffc2ee1cb119035037a27d346b0403bb
$(package)_dependencies=openssl
$(package)_patches=winver_fixup.patch

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/winver_fixup.patch && \
  ./autogen.sh
endef

define $(package)_set_vars
  $(package)_config_opts=--disable-shared --disable-libevent-regress --disable-samples
  $(package)_config_opts_release=--disable-debug-mode
  $(package)_config_opts_linux=--with-pic
  $(package)_cppflags_mingw32=-D_WIN32_WINNT=0x0A00
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
endef
