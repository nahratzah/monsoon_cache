cmake_minimum_required(VERSION 3.3)
project(monsoon_cache)

set(MONSOON_CACHE_VERSION_MAJOR 0 CACHE STRING "major version" FORCE)
set(MONSOON_CACHE_VERSION_MINOR 0 CACHE STRING "minor version" FORCE)
set(MONSOON_CACHE_VERSION ${MONSOON_CACHE_VERSION_MAJOR}.${MONSOON_CACHE_VERSION_MINOR} CACHE STRING "version" FORCE)

enable_testing()
find_package(instrumentation)

include (CheckCXXCompilerFlag)
check_cxx_compiler_flag ("-std=c++17" STD_CXX17)
check_cxx_compiler_flag ("-std=c++1z" STD_CXX1Z)
if(STD_CXX17)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
elseif(STD_CXX1Z)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1z")
else()
  message(WARNING "Cannot figure out how to enable C++17 on compiler.")
endif()

set(headers
    include/monsoon/cache/access_expire_decorator.h
    include/monsoon/cache/allocator.h
    include/monsoon/cache/bucket.h
    include/monsoon/cache/builder.h
    include/monsoon/cache/cache.h
    include/monsoon/cache/cache_impl.h
    include/monsoon/cache/cache_query.h
    include/monsoon/cache/create_handler.h
    include/monsoon/cache/doc_.h
    include/monsoon/cache/element.h
    include/monsoon/cache/expire_queue.h
    include/monsoon/cache/impl.h
    include/monsoon/cache/key_decorator.h
    include/monsoon/cache/max_age_decorator.h
    include/monsoon/cache/max_size_decorator.h
    include/monsoon/cache/mem_use.h
    include/monsoon/cache/stats.h
    include/monsoon/cache/store_delete_lock.h
    include/monsoon/cache/thread_safe_decorator.h
    include/monsoon/cache/weaken_decorator.h
    )

add_library(monsoon_cache INTERFACE)
if(instrumentation_DIR)
  target_link_libraries(monsoon_cache INTERFACE instrumentation)
endif()

find_package (Threads REQUIRED)
if(CMAKE_USE_PTHREADS_INIT)
  target_link_libraries(monsoon_cache INTERFACE ${CMAKE_THREAD_LIBS_INIT})
endif()

target_include_directories(monsoon_cache INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    )

install(FILES ${headers} DESTINATION "include/monsoon/cache")
install(TARGETS monsoon_cache EXPORT monsoon_cache DESTINATION "lib")
install(EXPORT monsoon_cache DESTINATION "lib/cmake/monsoon_cache")

configure_file(monsoon_cache-config-version.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/monsoon_cache-config-version.cmake @ONLY)
install(FILES monsoon_cache-config.cmake ${CMAKE_CURRENT_BINARY_DIR}/monsoon_cache-config-version.cmake DESTINATION "lib/cmake/monsoon_cache")

add_subdirectory(test)

find_package(Doxygen REQUIRED mscgen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
  doxygen_add_docs(monsoon_cache-doc include)
endif()
