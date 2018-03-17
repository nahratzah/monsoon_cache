include(CTest)
find_package(UnitTest++)

if (UnitTest++_FOUND)
  include_directories(${UTPP_INCLUDE_DIRS})

  add_executable (test_cache cache.cc)
  target_link_libraries (test_cache UnitTest++ monsoon_cache)
  add_test (cache test_cache)
endif ()
