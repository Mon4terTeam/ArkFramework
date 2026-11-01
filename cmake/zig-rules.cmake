# Loaded through CMAKE_USER_MAKE_RULES_OVERRIDE by zig-toolchain.cmake.
# CMAKE_AR must be a single executable, so archive with `zig ar` / `zig ranlib` directly.
foreach(lang C CXX)
  set(CMAKE_${lang}_ARCHIVE_CREATE "\"${ZIG_EXECUTABLE}\" ar qc <TARGET> <OBJECTS>")
  set(CMAKE_${lang}_ARCHIVE_APPEND "\"${ZIG_EXECUTABLE}\" ar q <TARGET> <OBJECTS>")
  set(CMAKE_${lang}_ARCHIVE_FINISH "\"${ZIG_EXECUTABLE}\" ranlib <TARGET>")
endforeach()
