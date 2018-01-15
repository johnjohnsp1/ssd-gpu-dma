cmake_minimum_required (VERSION 3.1)
project (libnvm)

make_sample (dis-rw dis-rw "rw.c;segment.c;util.c")
set_sisci (dis-rw)
