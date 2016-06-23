cmake_minimum_required(VERSION 2.8.11)
project(JRNL)
add_definitions(--std=c11 -Wall -Wextra -Wpedantic -Wno-padded -Werror -D_GNU_SOURCE)
add_executable(jrnld j.c jrnl.c)
