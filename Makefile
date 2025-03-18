.PHONY: all clean format debug release pull update

all: release

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
PROJ_DIR := $(dir $(MKFILE_PATH))

# GENERATOR is defined by the ci-tools makefiles

# These flags will make DuckDB build the extension
EXT_NAME=faiss
EXT_NAME_UPPER=FAISS
EXT_CONFIG=${PROJ_DIR}extension_config.cmake
EXT_RELEASE_FLAGS=""

include extension-ci-tools/makefiles/duckdb_extension.Makefile


prebuild:

ifneq ($(DUCKDB_PLATFORM), )
ifeq ($(findstring $(DUCKDB_PLATFORM), linux_amd64 linux_arm64), $(DUCKDB_PLATFORM))

ifeq ($(findstring $(DUCKDB_PLATFORM), linux_amd64), $(DUCKDB_PLATFORM))
EXT_RELEASE_FLAGS:=-DCMAKE_CUDA_COMPILER=/usr/local/cuda-11.6/bin/nvcc
else
EXT_RELEASE_FLAGS:=-DCMAKE_CUDA_COMPILER=/usr/local/cuda-11.6/bin/nvcc -DCMAKE_CUDA_HOST_COMPILER=aarch64-linux-gnu-g++
endif

prebuild:
	touch prebuild
	sed -i '/cmake_minimum_required(VERSION 3.23.1 FATAL_ERROR)/c\\' faiss/CMakeLists.txt
	wget -qO - https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/x86_64/7fa2af80.pub | apt-key add -
	wget -qO - https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/x86_64/3bf863cc.pub | apt-key add -
	bash -c 'echo "deb http://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/cross-linux-sbsa /" >> /etc/apt/sources.list.d/cuda.list'
	bash -c 'echo "deb http://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/x86_64 /" >> /etc/apt/sources.list.d/cuda.list'
	apt-get update
	DEBIAN_FRONTEND=noninteractive apt-get install -y -qq cuda-11-6 cuda-compiler-11.6 cuda-cross-sbsa-11-6
	cd faiss && git apply ../faiss-gpu.patch
else
ifeq ($(findstring $(DUCKDB_PLATFORM), linux_amd64_gcc4), $(DUCKDB_PLATFORM))
EXT_RELEASE_FLAGS:=-DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.1/bin/nvcc
prebuild:
	yum -y install wget
	yum-config-manager --add-repo https://developer.download.nvidia.com/compute/cuda/repos/rhel7/x86_64/cuda-rhel7.repo
#	ubuntu 18.04 sbsa doesn't support cuda versions above this
	yum -y install cuda-11-6 cuda-compiler-11-6
	cd faiss && git apply ../faiss-gpu.patch
endif
endif
ifeq ($(findstring $(DUCKDB_PLATFORM), osx_amd64 osx_arm64), $(DUCKDB_PLATFORM))
export VCPKG_OVERLAY_TRIPLETS=$(pwd)"/overlay_triplets"
prebuild:
	mkdir -p overlay_triplets
	cp vcpkg/triplets/x64-osx.cmake overlay_triplets/x64-osx.cmake
	echo "set(VCPKG_OSX_DEPLOYMENT_TARGET 11.0)" >> overlay_triplets/x64-osx.cmake
endif
ifeq ($(findstring $(DUCKDB_PLATFORM), windows_amd64), $(DUCKDB_PLATFORM))
export VCPKG_OVERLAY_TRIPLETS=$(pwd)"/overlay_triplets"
prebuild:
	mkdir -p overlay_triplets
	cp vcpkg/triplets/x64-osx.cmake overlay_triplets/x64-osx.cmake
	echo "set(VCPKG_PLATFORM_TOOLSET_VERSION 14.40)" >> overlay_triplets/x64-osx.cmake
endif
ifeq ($(findstring $(DUCKDB_PLATFORM), windows_amd64_mingw), windows_amd64_mingw)
prebuild:
	ls -la C:/
	cd faiss && git apply ../faiss.patch
endif
endif

release: prebuild

# Client tests
DEBUG_EXT_PATH='$(PROJ_DIR)build/debug/extension/${EXT_NAME}/${EXT_NAME}.duckdb_extension'
RELDEBUG_EXT_PATH='$(PROJ_DIR)build/reldebug/extension/${EXT_NAME}/${EXT_NAME}.duckdb_extension'
GOLINKFLAGS=-L$(PROJ_DIR)/go/deps/linux_amd64 -lduckdb -lduckdb_utf8proc -lduckdb_pg_query -lduckdb_re2 -lduckdb_fmt -lduckdb_hyperloglog -lduckdb_fastpforlib -lduckdb_miniz -lduckdb_mbedtls -lduckdb_fsst -lduckdb_skiplistlib -ljson_extension -licu_extension -lfts_extension -ljemalloc_extension -lparquet_extension -ltpcds_extension -ltpch_extension -lvisualizer_extension -lfaiss_extension -lomp -lblas -llapack -lm -lstdc++ -fsanitize=undefined

conformanceTests:
	mkdir conformanceTests

conformanceTests/msmarco-passage-openai-ada2: conformanceTests
	wget https://rgw.cs.uwaterloo.ca/pyserini/data/msmarco-passage-openai-ada2.tar -P conformanceTests/ && tar xvf conformanceTests/msmarco-passage-openai-ada2.tar -C conformanceTests/

conformanceTests/anserini-tools: conformanceTests
	cd conformanceTests && git clone https://github.com/castorini/anserini-tools

go_setup: reldebug
	cp $(PROJ_DIR)/build/reldebug/src/libduckdb_static.a $(PROJ_DIR)/go/deps/linux_amd64/libduckdb.a
	cp $(PROJ_DIR)/build/reldebug/third_party/**/*.a $(PROJ_DIR)/go/deps/linux_amd64
	cp $(PROJ_DIR)/build/reldebug/extension/**/*.a $(PROJ_DIR)/go/deps/linux_amd64

go/faissextcode.test: go_setup
	cd go && CGO_LDFLAGS="$(GOLINKFLAGS)" go test -c .
go/create_index: go_setup conformanceTests/msmarco-passage-openai-ada2
	cd go && CGO_LDFLAGS="$(GOLINKFLAGS)" go build faissextcode/cmd/create_index
go/create_trec: go_setup conformanceTests/anserini-tools
	cd go && CGO_LDFLAGS="$(GOLINKFLAGS)" go build faissextcode/cmd/create_trec

indices/%:
	mkdir -p "indices"
	${EXT_NAME_UPPER}_EXTENSION_BINARY_PATH=$(RELDEBUG_EXT_PATH) go/create_index $(notdir $@) "$@.index"

create_indices: indices/IDMap,HNSW128,Flat indices/IVF2048_HNSW128,Flat

benchmark: go/faissextcode.test
	go/faissextcode.test -test.run="^$$" -test.bench=. -test.benchtime=30s -test.timeout=12h | tee results

run_msmarco_queries: indices/IDMap,HNSW128,Flat
	FAISS_EXTENSION_BINARY_PATH='build/reldebug/extension/faiss/faiss.duckdb_extension' go/create_trec
	

install_local: install_release_local
install_release_local: release
	echo "INSTALL \"$(RELEASE_EXT_PATH)\"" | build/release/duckdb 
install_debug_local: debug
	echo "INSTALL \"$(DEBUG_EXT_PATH)\"" | build/release/duckdb 
