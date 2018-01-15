cmake_minimum_required (VERSION 3.1)
project (libnvm)

make_sample (read-example readdisk "read.c;transfer.c")
set_multithread (read-example)
