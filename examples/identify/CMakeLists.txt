cmake_minimum_required (VERSION 3.1)
project (libnvm-samples)

make_sample (userspace_identify userspace_identify "userspace.c")

make_sample (module_identify module_identify "module.c")

make_sample (smartio_identify smartio_identify "smartio.c")
set_sisci (smartio_identify)
