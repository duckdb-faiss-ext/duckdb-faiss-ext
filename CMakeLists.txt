cmake_minimum_required(VERSION 2.8.12)

# Set extension name here
set(TARGET_NAME faiss)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
project(${TARGET_NAME})
include_directories(src/include)


FILE(GLOB EXTENSION_SOURCES src/*.cpp "faiss/faiss/*.cpp"  "faiss/faiss/impl/*.cpp" "faiss/faiss/invlists/*.cpp" "faiss/faiss/utils/*.cpp" "faiss/faiss/utils/distances_fused/*.cpp")
add_library(${EXTENSION_NAME} STATIC ${EXTENSION_SOURCES})
add_definitions(-DFINTEGER=int)
include_directories(faiss/)

# FIXME this hardcodes the omp path
include_directories(/opt/homebrew/opt/libomp/include)

target_link_libraries(
  ${EXTENSION_NAME} /opt/homebrew/opt/libomp/lib/libomp.a
  "-framework Accelerate")

set(EXTENSION_SOURCES ${EXTENSION_SOURCES} ${FAISS_SOURCES})

set(PARAMETERS "-warnings")
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
