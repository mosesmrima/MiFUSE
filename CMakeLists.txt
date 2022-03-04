cmake_minimum_required(VERSION 3.21)
project(mifuse_main)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(mifuse_main
        main.c
        inotify.c
        Makefile
        mifuseops.h
        log.h
        log.c
        params.h
        README.md)
