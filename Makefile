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

clean:
	$(MAKE) -f extension-ci-tools/makefiles/duckdb_extension.Makefile clean

	rm -rf libomp

# reldebug isn't defined by the the duckdb extension template
reldebug:
	mkdir -p build/reldebug && \
	cmake $(GENERATOR) ${BUILD_FLAGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo -S ./duckdb/ -B build/reldebug && \
	cmake --build build/reldebug --config RelWithDebInfo

# Main tests, these don't work in v0.9 yet
test: test_release

test_release: release
	./build/release/test/unittest --test-dir . "[sql]"

test_debug: debug
	./build/debug/test/unittest --test-dir . "[sql]"

# Client tests
DEBUG_EXT_PATH='$(PROJ_DIR)build/debug/extension/${EXT_NAME}/${EXT_NAME}.duckdb_extension'
RELDEBUG_EXT_PATH='$(PROJ_DIR)build/reldebug/extension/${EXT_NAME}/${EXT_NAME}.duckdb_extension'
GOLINKFLAGS=-L$(PROJ_DIR)/go/deps/linux_amd64 -lduckdb -lduckdb_utf8proc -lduckdb_pg_query -lduckdb_re2 -lduckdb_fmt -lduckdb_hyperloglog -lduckdb_fastpforlib -lduckdb_miniz -lduckdb_mbedtls -lduckdb_fsst -ljson_extension -licu_extension -lfts_extension -ljemalloc_extension -lparquet_extension -ltpcds_extension -ltpch_extension -lvisualizer_extension -lfaiss_extension -lomp -lblas -llapack -lm -lstdc++ -fsanitize=undefined

go_binaries: reldebug
	cp $(PROJ_DIR)/build/reldebug/src/libduckdb_static.a $(PROJ_DIR)/go/deps/linux_amd64/libduckdb.a
	cp $(PROJ_DIR)/build/reldebug/third_party/**/*.a $(PROJ_DIR)/go/deps/linux_amd64
	cp $(PROJ_DIR)/build/reldebug/extension/**/*.a $(PROJ_DIR)/go/deps/linux_amd64
	cd go && CGO_LDFLAGS="$(GOLINKFLAGS)" go test -c .
	cd go && CGO_LDFLAGS="$(GOLINKFLAGS)" go build faissextcode/cmd/create_index
	cd go && CGO_LDFLAGS="$(GOLINKFLAGS)" go build faissextcode/cmd/create_trec

go/faissextcode.test: go_binaries
go/create_index: go_binaries
go/create_trec: go_binaries

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
