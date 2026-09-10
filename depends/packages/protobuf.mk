package=protobuf
$(package)_version=$(native_$(package)_version)
$(package)_download_path=$(native_$(package)_download_path)
$(package)_file_name=$(native_$(package)_file_name)
$(package)_sha256_hash=$(native_$(package)_sha256_hash)
$(package)_dependencies=native_$(package)
$(package)_cxxflags=-std=c++17

define $(package)_set_vars
  $(package)_config_opts=--disable-shared --with-protoc=$(build_prefix)/bin/protoc
  $(package)_config_opts += --disable-dependency-tracking --enable-option-checking
  $(package)_config_opts_linux=--with-pic
endef

# NOTE: 21.x moved gtest to third_party/googletest (no gtest/build-aux);
# only refresh top-level autotools helpers.
define $(package)_preprocess_cmds
   cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub .
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -C src libprotobuf.la
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) -C src install-libLTLIBRARIES install-nobase_includeHEADERS &&\
  $(MAKE) DESTDIR=$($(package)_staging_dir) install-pkgconfigDATA
endef

define $(package)_postprocess_cmds
  rm lib/libprotoc.a
endef
