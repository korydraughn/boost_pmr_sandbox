#! /bin/bash

#clang++ -std=c++17 -Og -g -o pmr_test pmr.cpp -I/opt/irods-externals/boost1.67.0-0/include -L/opt/irods-externals/boost1.67.0-0/lib -lboost_container \
#    -Wl,-rpath=/opt/irods-externals/boost1.67.0-0/lib
#
##clang++ -std=c++17 -stdlib=libc++ -nostdinc++ -O2 -o alloc_test alloc_test.cpp \
#clang++ -std=c++17 -stdlib=libc++ -nostdinc++ -Og -g -o alloc_test alloc_test.cpp \
#    -I/opt/irods-externals/boost1.67.0-0/include \
#    -I/opt/irods-externals/clang6.0-0/include/c++/v1 \
#    -L/opt/irods-externals/boost1.67.0-0/lib \
#    -L/opt/irods-externals/clang6.0-0/lib \
#    -lboost_container \
#    -Wl,-rpath=/opt/irods-externals/boost1.67.0-0/lib \
#    -Wl,-rpath=/opt/irods-externals/clang6.0-0/lib

#clang++ -std=c++17 -stdlib=libc++ -nostdinc++ -O2 -o fbr_test fixed_buffer_resource_test.cpp \
clang++ -std=c++17 -stdlib=libc++ -nostdinc++ -fsanitize=undefined -O0 -g -o fbr_test fixed_buffer_resource_test.cpp \
    -I/opt/irods-externals/boost1.67.0-0/include \
    -I/opt/irods-externals/clang6.0-0/include/c++/v1 \
    -I/opt/irods-externals/fmt6.1.2-1/include \
    -L/opt/irods-externals/boost1.67.0-0/lib \
    -L/opt/irods-externals/clang6.0-0/lib \
    -L/opt/irods-externals/fmt6.1.2-1/lib \
    -lboost_container \
    -lfmt \
    -Wl,-rpath=/opt/irods-externals/boost1.67.0-0/lib \
    -Wl,-rpath=/opt/irods-externals/clang6.0-0/lib \
    -Wl,-rpath=/opt/irods-externals/fmt6.1.2-1/lib

