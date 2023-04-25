cmake_minimum_required(VERSION 2.8.12)

# Set extension name here
set(TARGET_NAME faiss)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
project(${TARGET_NAME})
include_directories(src/include /Users/hannes/source/faiss)

set(EXTENSION_SOURCES src/faiss_extension.cpp)
add_library(${EXTENSION_NAME} STATIC ${EXTENSION_SOURCES})

target_link_libraries(
  ${EXTENSION_NAME} /opt/homebrew/opt/libomp/lib/libomp.a
  /Users/hannes/source/faiss/build/faiss/libfaiss.a "-framework Accelerate")

set(PARAMETERS "-warnings")
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
