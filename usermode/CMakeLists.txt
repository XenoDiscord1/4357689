cmake_minimum_required(VERSION 3.20)
project(RecoilComp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows subsystem — нет консольного окна
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")

add_executable(RecoilComp WIN32
    main.cpp
    recoil.cpp
    logger.cpp
    profile.cpp
    overlay.cpp
)

target_compile_definitions(RecoilComp PRIVATE
    _UNICODE
    UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(RecoilComp PRIVATE
    /O2          # Оптимизация скорости
    /W3
    /WX-
    /Zi          # Debug info (убрать для release)
    /EHsc
    /MT          # Static CRT — нет зависимости от VC редистрибутива
)

target_link_libraries(RecoilComp PRIVATE
    user32
    shell32
    kernel32
    advapi32
)
