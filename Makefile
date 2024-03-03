.PHONY: all clean format debug release duckdb_debug duckdb_release pull update

all: release

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
PROJ_DIR := $(dir $(MKFILE_PATH))

OSX_BUILD_UNIVERSAL_FLAG=
ifeq (${OSX_BUILD_UNIVERSAL}, 1)
	OSX_BUILD_UNIVERSAL_FLAG=-DOSX_BUILD_UNIVERSAL=1
endif
ifeq (${STATIC_LIBCPP}, 1)
	STATIC_LIBCPP=-DSTATIC_LIBCPP=TRUE
endif

ifeq ($(GEN),ninja)
	GENERATOR=-G "Ninja"
	FORCE_COLOR=-DFORCE_COLORED_OUTPUT=1
endif

BUILD_FLAGS=-DBUILD_EXTENSIONS="json" -DEXTENSION_STATIC_BUILD=1 -DBUILD_TPCH_EXTENSION=0 -DBUILD_PARQUET_EXTENSION=0 ${OSX_BUILD_UNIVERSAL_FLAG} ${STATIC_LIBCPP}

CLIENT_FLAGS :=

# These flags will make DuckDB build the extension
EXTENSION_NAME=FAISS
EXTENSION_NAME_LOWER=faiss
EXTENSION_FLAGS=-DDUCKDB_EXTENSION_NAMES="${EXTENSION_NAME_LOWER}" -DDUCKDB_EXTENSION_${EXTENSION_NAME}_PATH="$(PROJ_DIR)" -DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK="TRUE" -DDUCKDB_EXTENSION_${EXTENSION_NAME}_INCLUDE_PATH="$(PROJ_DIR)src/include"

pull:
	git submodule init
	git submodule update --recursive --remote

clean:
	rm -rf build
	rm -rf testext
	cd duckdb && make clean
	cd duckdb && make clean-python
	rm -rf libomp

# Main build
debug:
	mkdir -p  build/debug && \
	cmake $(GENERATOR) $(FORCE_COLOR) $(EXTENSION_FLAGS) ${CLIENT_FLAGS} -DEXTENSION_STATIC_BUILD=1 -DCMAKE_BUILD_TYPE=Debug ${BUILD_FLAGS} -S ./duckdb/ -B build/debug && \
	cmake --build build/debug --config Debug

release:
	mkdir -p build/release && \
	cmake $(GENERATOR) $(FORCE_COLOR) $(EXTENSION_FLAGS) ${CLIENT_FLAGS} -DEXTENSION_STATIC_BUILD=1 -DCMAKE_BUILD_TYPE=Release ${BUILD_FLAGS} -S ./duckdb/ -B build/release && \
	cmake --build build/release --config Release

reldebug:
	mkdir -p build/reldebug && \
	cmake $(GENERATOR) $(FORCE_COLOR) $(EXTENSION_FLAGS) ${CLIENT_FLAGS} -DEXTENSION_STATIC_BUILD=1 -DCMAKE_BUILD_TYPE=RelWithDebInfo ${BUILD_FLAGS} -S ./duckdb/ -B build/reldebug && \
	cmake --build build/reldebug --config RelWithDebInfo

# Client build
debug_js: CLIENT_FLAGS=-DBUILD_NODE=1 -DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK=0
debug_js: debug

debug_go: CLIENT_FLAGS=-DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK=0  -DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK="FALSE"
debug_go: debug

debug_python: CLIENT_FLAGS=-DBUILD_PYTHON=1 -DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK=0
debug_python: debug

release_js: CLIENT_FLAGS=-DBUILD_NODE=1 -DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK=0
release_js: release

release_go: CLIENT_FLAGS=-DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK=0  -DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK="FALSE"
release_go: release

reldebug_go: CLIENT_FLAGS=-DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK=0  -DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK="FALSE"
reldebug_go: reldebug

release_python: CLIENT_FLAGS=-DBUILD_PYTHON=1 -DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK=0
release_python: release

# Main tests
test: test_release

test_release: release
	./build/release/test/unittest --test-dir . "[sql]"

test_debug: debug
	./build/debug/test/unittest --test-dir . "[sql]"

# Client tests
DEBUG_EXT_PATH='$(PROJ_DIR)build/debug/extension/${EXTENSION_NAME_LOWER}/${EXTENSION_NAME_LOWER}.duckdb_extension'
RELEASE_EXT_PATH='$(PROJ_DIR)build/release/extension/${EXTENSION_NAME_LOWER}/${EXTENSION_NAME_LOWER}.duckdb_extension'
test_js: test_debug_js
test_debug_js: debug_js
	cd duckdb/tools/nodejs && ${EXTENSION_NAME}_EXTENSION_BINARY_PATH=$(DEBUG_EXT_PATH) npm run test-path -- "../../../test/nodejs/**/*.js"

test_release_js: release_js
	cd duckdb/tools/nodejs && ${EXTENSION_NAME}_EXTENSION_BINARY_PATH=$(RELEASE_EXT_PATH) npm run test-path -- "../../../test/nodejs/**/*.js"

test_python: test_debug_python
test_debug_python: debug_python
	cd test/python && ${EXTENSION_NAME}_EXTENSION_BINARY_PATH=$(DEBUG_EXT_PATH) python3 -m pytest

test_release_python: release_python
	cd test/python && ${EXTENSION_NAME}_EXTENSION_BINARY_PATH=$(RELEASE_EXT_PATH) python3 -m pytest

create_msmarco_index: release_python
	cd conformanceTests && ${EXTENSION_NAME}_EXTENSION_BINARY_PATH=$(RELEASE_EXT_PATH) python make_index.py

conformanceTests/index: create_msmarco_index

run_msmarco_queries: conformanceTests/index
	cd conformanceTests && python execute_queries.py

bench_msmarco_queries: reldebug_go
	cp $(PROJ_DIR)/build/reldebug/src/libduckdb_static.a $(PROJ_DIR)/go/deps/linux_amd64/libduckdb.a
	cp $(PROJ_DIR)/build/reldebug/third_party/**/*.a $(PROJ_DIR)/go/deps/linux_amd64
	cp $(PROJ_DIR)/build/reldebug/extension/**/*.a $(PROJ_DIR)/go/deps/linux_amd64
	cd go && CGO_LDFLAGS="-L$(PROJ_DIR)/go/deps/linux_amd64 -lduckdb -lduckdb_utf8proc -lduckdb_pg_query -lduckdb_re2 -lduckdb_fmt -lduckdb_hyperloglog -lduckdb_fastpforlib -lduckdb_miniz -lduckdb_mbedtls -lduckdb_fsst -ljson_extension -licu_extension -lfts_extension -ljemalloc_extension -lparquet_extension -ltpcds_extension -ltpch_extension -lvisualizer_extension -lfaiss_extension -lomp -lblas -llapack -fsanitize=undefined" go test -benchmem -run="^$$" -bench=. -benchtime=30s -timeout=12h .

install_local: install_release_local
install_release_local: release
	echo "INSTALL \"$(RELEASE_EXT_PATH)\"" | build/release/duckdb 
install_debug_local: debug
	echo "INSTALL \"$(DEBUG_EXT_PATH)\"" | build/release/duckdb 

format:
	find src/ -iname *.hpp -o -iname *.cpp | xargs clang-format --sort-includes=0 -style=file -i
	cmake-format -i CMakeLists.txt

update:
	git submodule update --remote --merge
