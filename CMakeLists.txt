cmake_minimum_required(VERSION 3.24)
project(euchre VERSION 1.0 LANGUAGES C CXX)

# Unity build: each executable is a single translation unit, a platform
# layer (src/main_*.cpp) that includes the portable core (src/euchre.cpp).
# The core uses no C++ runtime library, exceptions or RTTI. SDL3 and
# stb_truetype are fetched over HTTPS, pinned by SHA256; SDL3 is linked
# statically.

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Dependencies -----------------------------------------------------------
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA       OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK     OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC       OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI       OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR       OFF CACHE BOOL "" FORCE)
set(SDL_POWER        OFF CACHE BOOL "" FORCE)
set(SDL_TRAY         OFF CACHE BOOL "" FORCE)
set(SDL_GPU          OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
  URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SYSTEM
)
# stb_truetype, the header only
FetchContent_Declare(stb
  URL      https://github.com/nothings/stb/archive/6b5799bae8a1fe3792691ef4b72ae26434c3e799.tar.gz
  URL_HASH SHA256=87e9ecd2d2e0cb8fa35bf5a93ea6ca3999595ea4f0a4738ed1aac0a34cbd4da9
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SOURCE_SUBDIR _none
)
FetchContent_MakeAvailable(SDL3 stb)

# --- Flags for our code only ------------------------------------------------
add_library(euchre_flags INTERFACE)
target_compile_options(euchre_flags INTERFACE
  -fno-exceptions -fno-rtti -fno-threadsafe-statics
  -fno-asynchronous-unwind-tables
  -Wall -Wextra -Wconversion -Wno-sign-conversion
  -Wno-unused-parameter -Wno-unused-function
  -Wno-deprecated-enum-enum-conversion -Wno-deprecated-enum-float-conversion
)
check_cxx_compiler_flag(-Wdeprecated-anon-enum-enum-conversion EUCHRE_HAS_ANONENUM)
if(EUCHRE_HAS_ANONENUM)  # clang's name for the same warning
  target_compile_options(euchre_flags INTERFACE -Wno-deprecated-anon-enum-enum-conversion)
endif()
target_compile_definitions(euchre_flags INTERFACE APP_VERSION="${PROJECT_VERSION}")
target_include_directories(euchre_flags SYSTEM INTERFACE ${stb_SOURCE_DIR})

# Debug builds trap on undefined behavior, which includes failed asserts.
set(EUCHRE_TRAP "")
check_cxx_compiler_flag(-fsanitize-trap EUCHRE_HAS_TRAP)
if(EUCHRE_HAS_TRAP)
  set(EUCHRE_TRAP -fsanitize=undefined -fsanitize-trap)
else()
  check_cxx_compiler_flag(-fsanitize-undefined-trap-on-error EUCHRE_HAS_TRAP2)
  if(EUCHRE_HAS_TRAP2)
    set(EUCHRE_TRAP -fsanitize=undefined -fsanitize-undefined-trap-on-error)
  endif()
endif()
target_compile_options(euchre_flags INTERFACE "$<$<CONFIG:Debug>:-g3;${EUCHRE_TRAP}>")
target_link_options(euchre_flags INTERFACE "$<$<CONFIG:Debug>:${EUCHRE_TRAP}>")

check_linker_flag(CXX -nostdlib++ EUCHRE_HAS_NOSTDLIBXX)
if(EUCHRE_HAS_NOSTDLIBXX)
  target_link_options(euchre_flags INTERFACE -nostdlib++)
endif()

# Sources that #embed or #include others, for rebuilds
file(GLOB euchre_core CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
set(euchre_embeds ${PROJECT_SOURCE_DIR}/assets/Inter-Bold-subset.ttf)

# --- The game ---------------------------------------------------------------
add_executable(euchre WIN32 MACOSX_BUNDLE src/main_sdl.cpp)
target_link_libraries(euchre PRIVATE euchre_flags SDL3::SDL3-static)
set_property(SOURCE src/main_sdl.cpp APPEND PROPERTY OBJECT_DEPENDS ${euchre_embeds})

if(WIN32)
  enable_language(RC)
  set(EUCHRE_ICO ${PROJECT_SOURCE_DIR}/assets/euchre.ico)
  string(REPLACE "." "," EUCHRE_VERSION_RC "${PROJECT_VERSION}.0.0")
  configure_file(cmake/euchre.rc.in ${CMAKE_BINARY_DIR}/euchre.rc @ONLY)
  if(EXISTS ${EUCHRE_ICO})
    target_sources(euchre PRIVATE ${CMAKE_BINARY_DIR}/euchre.rc)
  endif()
  target_link_options(euchre PRIVATE -static)
endif()

if(APPLE)
  set_target_properties(euchre PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME          "Euchre"
    MACOSX_BUNDLE_GUI_IDENTIFIER       "com.nullprogram.euchre"
    MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

# --- Tests ------------------------------------------------------------------
if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_executable(euchre_test src/main_test.cpp)
  target_link_libraries(euchre_test PRIVATE euchre_flags)
  set_property(SOURCE src/main_test.cpp APPEND PROPERTY OBJECT_DEPENDS ${euchre_embeds})
  add_test(NAME unit COMMAND euchre_test)
endif()
