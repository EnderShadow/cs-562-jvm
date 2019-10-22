cmake_minimum_required(VERSION 3.15)
project(JVM C)

set(CMAKE_C_STANDARD 11)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(JVM main.c jvmSettings.h dataTypes.h stringutils.h utils.h heap.c heap.h classfile.c classfile.h object.c object.h gc.c gc.h indirection.c indirection_impl.h indirection.h garbage_collection.h jvmSettings.c flags.h mm.c mm.h jthread.c jthread.h bytecode_interpreter.c bytecode_interpreter.h opcodes.h classloader.c classloader.h)
target_link_libraries(JVM Threads::Threads)
target_link_libraries(JVM m)