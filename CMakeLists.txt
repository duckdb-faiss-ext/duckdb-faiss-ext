cmake_minimum_required(VERSION 2.8.12)

# Set extension name here
set(TARGET_NAME faiss)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
project(${TARGET_NAME})
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

# add omp as a target
set(OMP_STATIC_LIB ${CMAKE_CURRENT_SOURCE_DIR}/libomp/omp/build/runtime/src/libomp.a)

add_custom_command(
        OUTPUT ${OMP_STATIC_LIB}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND sh ompconfigure)

add_custom_target(BuildOMP DEPENDS ${OMP_STATIC_LIB})

# create extension targets
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})

set(PARAMETERS "-warnings")
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

# build omp before the extension
add_dependencies(${EXTENSION_NAME} BuildOMP)

# link libraries
target_link_libraries(${TARGET_NAME}_extension ${OMP_STATIC_LIB})
target_link_libraries(${TARGET_NAME}_loadable_extension ${OMP_STATIC_LIB})

# We do... (really need other peoples build script) - says Arjen
find_package(MKL)
if(MKL_FOUND)
  target_link_libraries(${TARGET_NAME}_extension PRIVATE ${MKL_LIBRARIES})
  target_link_libraries(${TARGET_NAME}_loadable_extension PRIVATE ${MKL_LIBRARIES})
  # target_link_libraries(faiss_avx2 PRIVATE ${MKL_LIBRARIES})
else()
  find_package(BLAS REQUIRED)
  target_link_libraries(${TARGET_NAME}_extension ${BLAS_LIBRARIES})
  target_link_libraries(${TARGET_NAME}_loadable_extension ${BLAS_LIBRARIES})
  # target_link_libraries(faiss_avx2 PRIVATE ${BLAS_LIBRARIES})
  
  find_package(LAPACK REQUIRED)
  target_link_libraries(${TARGET_NAME}_extension ${LAPACK_LIBRARIES})
  target_link_libraries(${TARGET_NAME}_loadable_extension ${LAPACK_LIBRARIES})
  # target_link_libraries(faiss_avx2 PRIVATE ${LAPACK_LIBRARIES})
endif()

if(APPLE)
  target_link_libraries(${EXTENSION_NAME} "-framework Accelerate")
endif()

install(
        TARGETS ${EXTENSION_NAME}
        EXPORT "${DUCKDB_EXPORT_SET}"
        LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
        ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
