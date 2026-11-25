package=boost
$(package)_version=1.89.0
$(package)_download_path = https://github.com/boostorg/boost/releases/download/boost-$($(package)_version)
$(package)_file_name = boost-$($(package)_version)-cmake.tar.gz
$(package)_sha256_hash=954a01219bf818c7fb850fa610c2c8c71a4fa28fa32a1900056bcb6ff58cf908
$(package)_patches = skip_compiled_targets.patch
$(package)_build_subdir = build
$(package)_dependencies := zlib

define $(package)_set_vars
  # Pinkcoin: the libraries src/ includes (headers) or links (filesystem,
  # program_options, thread, chrono, test). system is header-only.
  $(package)_config_opts = -DBOOST_INCLUDE_LIBRARIES="algorithm;array;asio;assert;assign;bind;chrono;config;date_time;filesystem;foreach;function;interprocess;iostreams;lexical_cast;optional;preprocessor;program_options;random;signals2;smart_ptr;spirit;system;test;thread;tuple;type_traits;variant"
  $(package)_config_opts += -DBOOST_TEST_HEADERS_ONLY=OFF
  $(package)_config_opts += -DBOOST_ENABLE_MPI=OFF
  $(package)_config_opts += -DBOOST_ENABLE_PYTHON=OFF
  $(package)_config_opts += -DBOOST_INSTALL_LAYOUT=system
  $(package)_config_opts += -DBUILD_TESTING=OFF
  $(package)_config_opts += -DCMAKE_DISABLE_FIND_PACKAGE_ICU=ON
  $(package)_config_opts += -DCMAKE_DISABLE_FIND_PACKAGE_zstd=ON
  $(package)_config_opts += -DZLIB_INCLUDE_DIR=$(host_prefix)/include
  $(package)_config_opts += -DZLIB_LIBRARY=$(host_prefix)/lib/libz.a
  # Pinkcoin: no bzip2 or xz in depends. Boost.Iostreams keeps its zlib filter
  # only, and must not find the build machine's libbz2/liblzma/libzstd.
  $(package)_config_opts += -DBOOST_IOSTREAMS_ENABLE_BZIP2=OFF
  $(package)_config_opts += -DBOOST_IOSTREAMS_ENABLE_LZMA=OFF
  $(package)_config_opts += -DBOOST_IOSTREAMS_ENABLE_ZSTD=OFF
endef

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/skip_compiled_targets.patch
endef

define $(package)_config_cmds
  $($(package)_cmake) -S .. -B . $($(package)_config_opts)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
