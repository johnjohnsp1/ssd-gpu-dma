cmake_minimum_required (VERSION 3.1)
project (libnvm-samples)

make_sample (identify-userspace identify-userspace "userspace.c")

make_sample (identify-module identify "module.c")

make_sample (identify-smartio identify "smartio.c")
set_sisci (identify-smartio)

make_sisci_choice(identify identify-smartio identify-module)
