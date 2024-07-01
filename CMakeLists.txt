cmake_minimum_required(VERSION 3.9)

set(TARGET_NAME faiss)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME} LANGUAGES CXX)
include_directories(src/include)

file(
  GLOB
  EXTENSION_SOURCES
  src/*.cpp)

# faiss things
# The reason we need this is because we cannot use faiss' buid script,
# as it does not export to the right export-set.
set(FAISS_SOURCES
  faiss/faiss/AutoTune.cpp
  faiss/faiss/Clustering.cpp
  faiss/faiss/IVFlib.cpp
  faiss/faiss/Index.cpp
  faiss/faiss/Index2Layer.cpp
  faiss/faiss/IndexAdditiveQuantizer.cpp
  faiss/faiss/IndexBinary.cpp
  faiss/faiss/IndexBinaryFlat.cpp
  faiss/faiss/IndexBinaryFromFloat.cpp
  faiss/faiss/IndexBinaryHNSW.cpp
  faiss/faiss/IndexBinaryHash.cpp
  faiss/faiss/IndexBinaryIVF.cpp
  faiss/faiss/IndexFlat.cpp
  faiss/faiss/IndexFlatCodes.cpp
  faiss/faiss/IndexHNSW.cpp
  faiss/faiss/IndexIDMap.cpp
  faiss/faiss/IndexIVF.cpp
  faiss/faiss/IndexIVFAdditiveQuantizer.cpp
  faiss/faiss/IndexIVFFlat.cpp
  faiss/faiss/IndexIVFPQ.cpp
  faiss/faiss/IndexIVFFastScan.cpp
  faiss/faiss/IndexIVFAdditiveQuantizerFastScan.cpp
  faiss/faiss/IndexIVFPQFastScan.cpp
  faiss/faiss/IndexIVFPQR.cpp
  faiss/faiss/IndexIVFSpectralHash.cpp
  faiss/faiss/IndexLSH.cpp
  faiss/faiss/IndexNNDescent.cpp
  faiss/faiss/IndexLattice.cpp
  faiss/faiss/IndexNSG.cpp
  faiss/faiss/IndexPQ.cpp
  faiss/faiss/IndexFastScan.cpp
  faiss/faiss/IndexAdditiveQuantizerFastScan.cpp
  faiss/faiss/IndexIVFIndependentQuantizer.cpp
  faiss/faiss/IndexPQFastScan.cpp
  faiss/faiss/IndexPreTransform.cpp
  faiss/faiss/IndexRefine.cpp
  faiss/faiss/IndexReplicas.cpp
  faiss/faiss/IndexRowwiseMinMax.cpp
  faiss/faiss/IndexScalarQuantizer.cpp
  faiss/faiss/IndexShards.cpp
  faiss/faiss/IndexShardsIVF.cpp
  faiss/faiss/MatrixStats.cpp
  faiss/faiss/MetaIndexes.cpp
  faiss/faiss/VectorTransform.cpp
  faiss/faiss/clone_index.cpp
  faiss/faiss/index_factory.cpp
  faiss/faiss/impl/AuxIndexStructures.cpp
  faiss/faiss/impl/CodePacker.cpp
  faiss/faiss/impl/IDSelector.cpp
  faiss/faiss/impl/FaissException.cpp
  faiss/faiss/impl/HNSW.cpp
  faiss/faiss/impl/NSG.cpp
  faiss/faiss/impl/PolysemousTraining.cpp
  faiss/faiss/impl/ProductQuantizer.cpp
  faiss/faiss/impl/AdditiveQuantizer.cpp
  faiss/faiss/impl/ResidualQuantizer.cpp
  faiss/faiss/impl/LocalSearchQuantizer.cpp
  faiss/faiss/impl/ProductAdditiveQuantizer.cpp
  faiss/faiss/impl/ScalarQuantizer.cpp
  faiss/faiss/impl/index_read.cpp
  faiss/faiss/impl/index_write.cpp
  faiss/faiss/impl/io.cpp
  faiss/faiss/impl/kmeans1d.cpp
  faiss/faiss/impl/lattice_Zn.cpp
  faiss/faiss/impl/pq4_fast_scan.cpp
  faiss/faiss/impl/pq4_fast_scan_search_1.cpp
  faiss/faiss/impl/pq4_fast_scan_search_qbs.cpp
  faiss/faiss/impl/residual_quantizer_encode_steps.cpp
  faiss/faiss/impl/io.cpp
  faiss/faiss/impl/lattice_Zn.cpp
  faiss/faiss/impl/NNDescent.cpp
  faiss/faiss/invlists/BlockInvertedLists.cpp
  faiss/faiss/invlists/DirectMap.cpp
  faiss/faiss/invlists/InvertedLists.cpp
  faiss/faiss/invlists/InvertedListsIOHook.cpp
  faiss/faiss/utils/Heap.cpp
  faiss/faiss/utils/WorkerThread.cpp
  faiss/faiss/utils/distances.cpp
  faiss/faiss/utils/distances_simd.cpp
  faiss/faiss/utils/extra_distances.cpp
  faiss/faiss/utils/hamming.cpp
  faiss/faiss/utils/partitioning.cpp
  faiss/faiss/utils/quantize_lut.cpp
  faiss/faiss/utils/random.cpp
  faiss/faiss/utils/sorting.cpp
  faiss/faiss/utils/utils.cpp
  faiss/faiss/utils/distances_fused/avx512.cpp
  faiss/faiss/utils/distances_fused/distances_fused.cpp
  faiss/faiss/utils/distances_fused/simdlib_based.cpp
)

add_definitions(-DFINTEGER=int)
if(WIN32)
  add_definitions(-DFAISS_MAIN_LIB)
endif()
if(NOT WIN32)
  list(APPEND FAISS_SOURCES faiss/faiss/invlists/OnDiskInvertedLists.cpp)
  list(APPEND FAISS_HEADERS faiss/faiss/invlists/OnDiskInvertedLists.h)
endif()


set(EXTENSION_SOURCES ${EXTENSION_SOURCES} ${FAISS_SOURCES})

include_directories(faiss/)

# create extension targets
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

# link libraries
find_package(OpenMP REQUIRED)
target_link_libraries(${EXTENSION_NAME} OpenMP::OpenMP_CXX)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenMP::OpenMP_CXX)

find_package(MKL QUIET)
if(MKL_FOUND)
  target_link_libraries(${EXTENSION_NAME} ${MKL_LIBRARIES})
  target_link_libraries(${LOADABLE_EXTENSION_NAME} ${MKL_LIBRARIES})
else()
  find_package(BLAS REQUIRED)
  target_link_libraries(${EXTENSION_NAME} ${BLAS_LIBRARIES})
  target_link_libraries(${LOADABLE_EXTENSION_NAME} ${BLAS_LIBRARIES})

  find_package(LAPACK REQUIRED)
  target_link_libraries(${EXTENSION_NAME} ${LAPACK_LIBRARIES})
  target_link_libraries(${LOADABLE_EXTENSION_NAME} ${LAPACK_LIBRARIES})
endif()

if(APPLE)
  target_link_libraries(${EXTENSION_NAME} "-framework Accelerate")
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
