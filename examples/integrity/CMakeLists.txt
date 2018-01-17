cmake_minimum_required (VERSION 3.1)
project (libnvm-samples)

make_sample (integrity integrity "integrity.c;transfer.c;util.c")
set_multithread (integrity)
set_sisci (integrity)
