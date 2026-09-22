package=expat
$(package)_version=2.6.4
$(package)_download_path=https://github.com/libexpat/libexpat/releases/download/R_$(subst .,_,$($(package)_version))/
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=a695629dae047055b37d50a0ff4776d1d45d0a4c842cf4ccee158441f55ff7ee

# -D_DEFAULT_SOURCE defines __USE_MISC, which exposes additional
# definitions in endian.h, which are required for a working
# endianess check in configure when building with -flto.
# NOTE: --disable-shared (static, PIC) is REQUIRED, matching upstream:
# shared libfontconfig.so would carry a DT_NEEDED on libexpat.so.1, but
# Qt's fontconfig probe links only `-lfontconfig -lfreetype` (Requires.private
# is ignored for shared linking) and fails with undefined XML_* refs.
# Static expat is embedded into fontconfig/dbus — no DT_NEEDED, probe passes.
define $(package)_set_vars
  $(package)_config_opts=--disable-shared --without-docbook --without-tests --without-examples
  $(package)_config_opts += --disable-dependency-tracking --enable-option-checking
  $(package)_config_opts += --without-xmlwf
  $(package)_config_opts_linux=--with-pic
  $(package)_cppflags += -D_DEFAULT_SOURCE
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
  rm -rf share lib/cmake lib/*.la
endef
