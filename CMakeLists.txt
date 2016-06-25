cmake_minimum_required(VERSION 2.8.11)
project(JRNL)

# extensions needed by this program
add_definitions("-D_DEFAULT_SOURCE")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --std=c89 -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -pedantic -pedantic-errors -Wunreachable-code -Wformat=2 -fno-builtin -Werror -g")

add_executable(jrnld j.c jrnl.c jrnld.c)
