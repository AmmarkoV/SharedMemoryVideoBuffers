cmake_minimum_required(VERSION 3.10)

project(SharedMemoryVideoBuffers)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_C_FLAGS "-Wall -pthread -lrt -lm -g")
set(CMAKE_SHARED_LINKER_FLAGS "-shared -fPIC -g")

set(X11LIBS -lX11)
set(OBJ_DIR ${CMAKE_BINARY_DIR}/obj)

# Source files
set(SERVER_SRC src/c/server.c src/c/sharedMemoryVideoBuffers.c)
set(CLIENT_SRC src/c/client.c src/c/sharedMemoryVideoBuffers.c)
set(CONSUMER_SRC src/c/consumer.c src/c/sharedMemoryVideoBuffers.c)
set(PUBLISHER_SRC src/c/publisher.c src/c/sharedMemoryVideoBuffers.c)
set(VIEWER_SRC src/c/viewer.c src/c/sharedMemoryVideoBuffers.c)
set(LIBRARY_SRC src/c/sharedMemoryVideoBuffers.c)

# Targets
add_executable(server ${SERVER_SRC})
add_executable(client ${CLIENT_SRC})
add_executable(consumer ${CONSUMER_SRC})
add_executable(publisher ${PUBLISHER_SRC})
add_executable(viewer ${VIEWER_SRC})
add_library(SharedMemoryVideoBuffers SHARED ${LIBRARY_SRC})

# Include directories
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(consumer PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(publisher PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(viewer PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(SharedMemoryVideoBuffers PRIVATE ${CMAKE_SOURCE_DIR}/src/c)

# Link libraries
target_link_libraries(viewer PRIVATE ${X11LIBS})

# Output directories
set_target_properties(server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(publisher PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(viewer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(SharedMemoryVideoBuffers PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
 

