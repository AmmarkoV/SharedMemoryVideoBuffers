cmake_minimum_required(VERSION 3.10)

project(SharedMemoryVideoBuffers)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_C_FLAGS "-Wall -O2 -pthread -g")
set(CMAKE_SHARED_LINKER_FLAGS "-shared -fPIC -g")

set(X11LIBS -lX11)
set(OBJ_DIR ${CMAKE_BINARY_DIR}/obj)

# Source files
set(SERVER_SRC src/c/server.c src/c/sharedMemoryVideoBuffers.c)
set(CLIENT_SRC src/c/client.c src/c/sharedMemoryVideoBuffers.c)
set(CONSUMER_SRC src/c/consumer.c src/c/sharedMemoryVideoBuffers.c)
set(PUBLISHER_SRC src/c/publisher.c src/c/sharedMemoryVideoBuffers.c)
set(VIEWER_SRC src/c/viewer.c src/c/sharedMemoryVideoBuffers.c)
set(V4L2SINK_SRC src/c/v4l2sink.c src/c/pixel_convert.c src/c/sharedMemoryVideoBuffers.c)
set(SCREENCAP_SRC src/c/screencapture.c src/c/sharedMemoryVideoBuffers.c)
set(LIBRARY_SRC src/c/sharedMemoryVideoBuffers.c)

# Targets
add_executable(server ${SERVER_SRC})
add_executable(client ${CLIENT_SRC})
add_executable(consumer ${CONSUMER_SRC})
add_executable(publisher ${PUBLISHER_SRC})
add_executable(viewer ${VIEWER_SRC})
add_executable(v4l2sink ${V4L2SINK_SRC})
add_library(SharedMemoryVideoBuffers SHARED ${LIBRARY_SRC})

# Include directories
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(consumer PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(publisher PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(viewer PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(v4l2sink PRIVATE ${CMAKE_SOURCE_DIR}/src/c)
target_include_directories(SharedMemoryVideoBuffers PRIVATE ${CMAKE_SOURCE_DIR}/src/c)

# Link libraries. Passed through target_link_libraries (not CMAKE_C_FLAGS) so they come
# after the objects: linkers using --as-needed drop libraries listed before them, and
# before glibc 2.34 shm_open/shm_unlink live in librt, not libc
set(SHMVB_LIBS -pthread rt m)
target_link_libraries(server PRIVATE ${SHMVB_LIBS})
target_link_libraries(client PRIVATE ${SHMVB_LIBS})
target_link_libraries(consumer PRIVATE ${SHMVB_LIBS})
target_link_libraries(publisher PRIVATE ${SHMVB_LIBS})
target_link_libraries(viewer PRIVATE ${X11LIBS} ${SHMVB_LIBS})
target_link_libraries(v4l2sink PRIVATE ${SHMVB_LIBS})
target_link_libraries(SharedMemoryVideoBuffers PRIVATE ${SHMVB_LIBS})

# Output directories
set_target_properties(server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(publisher PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(viewer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(SharedMemoryVideoBuffers PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
 


# Optional: screencapture needs libX11 + libXext (MIT-SHM). Skipped, not fatal, if missing
find_path(XSHM_INCLUDE_DIR X11/extensions/XShm.h)
find_library(X11_LIB X11)
find_library(XEXT_LIB Xext)
if(XSHM_INCLUDE_DIR AND X11_LIB AND XEXT_LIB)
    add_executable(screencapture ${SCREENCAP_SRC})
    target_include_directories(screencapture PRIVATE ${CMAKE_SOURCE_DIR}/src/c ${XSHM_INCLUDE_DIR})
    target_link_libraries(screencapture PRIVATE ${X11_LIB} ${XEXT_LIB} ${SHMVB_LIBS})
    set_target_properties(screencapture PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
else()
    message(STATUS "screencapture skipped: libX11/libXext development files not found")
endif()
