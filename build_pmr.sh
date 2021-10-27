#! /bin/bash

clang++ -std=c++17 -Og -g -o pmr_test pmr.cpp -I/opt/irods-externals/boost1.67.0-0/include -L/opt/irods-externals/boost1.67.0-0/lib -lboost_container \
    -Wl,-rpath=/opt/irods-externals/boost1.67.0-0/lib

#clang++ -std=c++17 -stdlib=libc++ -nostdinc++ -O2 -o alloc_test alloc_test.cpp \
clang++ -std=c++17 -stdlib=libc++ -nostdinc++ -Og -g -o alloc_test alloc_test.cpp \
    -I/opt/irods-externals/boost1.67.0-0/include \
    -I/opt/irods-externals/clang6.0-0/include/c++/v1 \
    -L/opt/irods-externals/boost1.67.0-0/lib \
    -L/opt/irods-externals/clang6.0-0/lib \
    -lboost_container \
    -Wl,-rpath=/opt/irods-externals/boost1.67.0-0/lib \
    -Wl,-rpath=/opt/irods-externals/clang6.0-0/lib
