.PHONY: all clean format debug release pull update

all: release

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
PROJ_DIR := $(dir $(MKFILE_PATH))

# GENERATOR is defined by the ci-tools makefiles

# These flags will make DuckDB build the extension
EXT_NAME=faiss
EXT_NAME_UPPER=FAISS
EXT_CONFIG=${PROJ_DIR}extension_config.cmake
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# reldebug isn't defined by the the duckdb extension template
# x86_64-w64-mingw32-cmake --trace-expand --debug-trycompile $(GENERATOR) ${BUILD_FLAGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo -S ./duckdb/ -B build/reldebug && \

reldebug:
	mkdir -p build/reldebug && \
	cmake $(GENERATOR) ${BUILD_FLAGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo -S ./duckdb/ -B build/reldebug --debug-trycompile --debug-find && \
	cmake --build build/reldebug --config RelWithDebInfo

# Client tests
DEBUG_EXT_PATH='$(PROJ_DIR)build/debug/extension/${EXT_NAME}/${EXT_NAME}.duckdb_extension'
RELDEBUG_EXT_PATH='$(PROJ_DIR)build/reldebug/extension/${EXT_NAME}/${EXT_NAME}.duckdb_extension'
GOLINKFLAGS=-L$(PROJ_DIR)/go/deps/linux_amd64 -lduckdb -lduckdb_utf8proc -lduckdb_pg_query -lduckdb_re2 -lduckdb_fmt -lduckdb_hyperloglog -lduckdb_fastpforlib -lduckdb_miniz -lduckdb_mbedtls -lduckdb_fsst -lduckdb_skiplistlib -ljson_extension -licu_extension -lfts_extension -ljemalloc_extension -lparquet_extension -ltpcds_extension -ltpch_extension -lvisualizer_extension -lfaiss_extension -lomp -lblas -llapack -lm -lstdc++ -fsanitize=undefined

go_setup: reldebug
	cp $(PROJ_DIR)/build/reldebug/src/libduckdb_static.a $(PROJ_DIR)/go/deps/linux_amd64/libduckdb.a
	cp $(PROJ_DIR)/build/reldebug/third_party/**/*.a $(PROJ_DIR)/go/deps/linux_amd64
	cp $(PROJ_DIR)/build/reldebug/extension/**/*.a $(PROJ_DIR)/go/deps/linux_amd64

go/faissextcode.test: go_setup
	cd go && CGO_LDFLAGS="$(GOLINKFLAGS)" go test -c .
go/create_index: go_setup
	cd go && CGO_LDFLAGS="$(GOLINKFLAGS)" go build faissextcode/cmd/create_index
go/create_trec: go_setup
	cd go && CGO_LDFLAGS="$(GOLINKFLAGS)" go build faissextcode/cmd/create_trec

indices/%:
	mkdir -p "indices"
	${EXT_NAME_UPPER}_EXTENSION_BINARY_PATH=$(RELDEBUG_EXT_PATH) go/create_index $(notdir $@) "$@.index"

create_indices: indices/IDMap,HNSW128,Flat indices/IVF2048_HNSW128,Flat

benchmark: go/faissextcode.test
	go/faissextcode.test -test.run="^$$" -test.bench=. -test.benchtime=30s -test.timeout=12h | tee results

install_local: install_release_local
install_release_local: release
	echo "INSTALL \"$(RELEASE_EXT_PATH)\"" | build/release/duckdb 
install_debug_local: debug
	echo "INSTALL \"$(DEBUG_EXT_PATH)\"" | build/release/duckdb 
