# Build options and instrumentation toggles.

option(ENABLE_TRACY "Enable Tracy profiler client integration" OFF)

# Optional: Address Sanitizer + Undefined Behavior Sanitizer for memory/safety
# checks. Runtime suppressions via LSAN_OPTIONS=suppressions=./.asan_ignorefile
option(ENABLE_SANITIZERS "Enable ASan + UBSan for memory and safety checks" OFF)
if(ENABLE_SANITIZERS)
  message(STATUS "Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined \
-fno-omit-frame-pointer")
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
  message(
    STATUS "ASan suppressions: set LSAN_OPTIONS=suppressions=./.asan_ignorefile"
  )
endif()

# Optional: code coverage instrumentation for unit/logical tests (gcovr style).
option(ENABLE_COVERAGE "Enable coverage flags for GCC/Clang (gcovr)" OFF)
if(ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Enabling code coverage instrumentation (gcovr)")
    add_compile_options(-O0 -g --coverage)
    add_link_options(--coverage)
  else()
    message(WARNING "Coverage requested but compiler is not GCC/Clang")
  endif()
endif()

# Optional: LLVM code coverage instrumentation (llvm-cov style, clang required).
option(ENABLE_LLVM_COV "Enable LLVM coverage instrumentation (llvm-cov)" OFF)
if(ENABLE_LLVM_COV)
  message(STATUS "Enabling LLVM code coverage instrumentation")
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_CXX_COMPILER clang++)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping -O0 -g)
  add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif()
