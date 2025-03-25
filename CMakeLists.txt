if(UNIX AND NOT APPLE)
  cmake_minimum_required(VERSION 3.29)
else()
  cmake_minimum_required(VERSION 3.16)
endif()

set(TARGET_NAME faiss)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

file(
  GLOB
  EXTENSION_SOURCES
  src/*.cpp)
file(
  GLOB
  EXTENSION_GPU_SOURCES
  src/gpu/*.cpp)

set(EXTENSION_SOURCES ${EXTENSION_SOURCES})
set(EXTENSION_GPU_SOURCES ${EXTENSION_GPU_SOURCES})

include_directories(src/include)
include_directories(faiss/)

set(FIND_LIBRARY_USE_LIB64_PATHS TRUE)

# configure
set(FAISS_ENABLE_PYTHON OFF)
set(FAISS_ENABLE_GPU OFF)
if(UNIX AND NOT APPLE AND DUCKDB_FAISS_EXT_ENABLE_GPU_CUDA)
  set(FAISS_ENABLE_GPU ON)
  add_compile_definitions(DDBF_ENABLE_GPU)
  set(EXTENSION_SOURCES ${EXTENSION_SOURCES} ${EXTENSION_GPU_SOURCES})
endif()
set(BUILD_TESTING OFF)

# create extension targets
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} "" ${EXTENSION_SOURCES})

# link libraries
add_subdirectory(faiss)

if(UNIX AND NOT APPLE AND DUCKDB_FAISS_EXT_ENABLE_GPU_CUDA)
  # If we are linking with faiss+GPU, we need access to cuda as well for the
  # faiss headers. + we need to link faiss as whole-archive, since faiss depends
  # on faiss_gpu and faiss_gpu on faiss.
  find_package(CUDAToolkit REQUIRED)
  target_link_libraries(${EXTENSION_NAME} CUDA::cublas_static)
  target_link_libraries(${LOADABLE_EXTENSION_NAME} CUDA::cublas_static)
  target_link_libraries(${EXTENSION_NAME} "$<LINK_LIBRARY:WHOLE_ARCHIVE,faiss>")
  target_link_libraries(${LOADABLE_EXTENSION_NAME} "$<LINK_LIBRARY:WHOLE_ARCHIVE,faiss>")
else()
  target_link_libraries(${EXTENSION_NAME} faiss)
  target_link_libraries(${LOADABLE_EXTENSION_NAME} faiss)
endif()

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

install(
  TARGETS ${EXTENSION_NAME} faiss
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
