cmake_minimum_required(VERSION 3.9)

# Set extension name here
set(TARGET_NAME faiss)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)
project(${TARGET_NAME} LANGUAGES CXX)
include_directories(src/include)

# we don't need other peoples build script (says Hannes)
file(
  GLOB
  EXTENSION_SOURCES
  src/*.cpp
  "faiss/faiss/*.cpp"
  "faiss/faiss/impl/*.cpp"
  "faiss/faiss/invlists/*.cpp"
  "faiss/faiss/utils/*.cpp"
  "faiss/faiss/utils/distances_fused/*.cpp")
add_definitions(-DFINTEGER=int)
set(EXTENSION_SOURCES ${EXTENSION_SOURCES} ${FAISS_SOURCES})

include_directories(faiss/ libomp/omp/build/runtime/src)

# create extension targets
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

# link libraries
find_package(OpenMP REQUIRED)
target_link_libraries(${EXTENSION_NAME} OpenMP::OpenMP_CXX)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenMP::OpenMP_CXX)

find_package(MKL QUIET)
if(MKL_FOUND)
  target_link_libraries(${EXTENSION_NAME} PRIVATE ${MKL_LIBRARIES})
  target_link_libraries(${LOADABLE_EXTENSION_NAME} PRIVATE ${MKL_LIBRARIES})
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
