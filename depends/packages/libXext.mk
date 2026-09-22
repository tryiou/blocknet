package=libXext
$(package)_version=1.3.2
$(package)_download_path=https://xorg.freedesktop.org/releases/individual/lib/
$(package)_file_name=$(package)-$($(package)_version).tar.bz2
$(package)_sha256_hash=f829075bc646cdc085fa25d98d5885d83b1759ceb355933127c257e8e50432e0
$(package)_dependencies=xproto xextproto libX11 libXau

define $(package)_set_vars
  $(package)_config_opts=--disable-static
endef

define $(package)_preprocess_cmds
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub . && \
  printf '#ifndef HAVE__XEATDATAWORDS\n#define _XEatDataWords(dpy,n) _XEatData((dpy), (unsigned long)4 * (n))\n#endif\n' > src/eat.h
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
