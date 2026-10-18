# Third party code

- `stb_vorbis.c`: the Ogg Vorbis decoder of Sean Barrett, version 1.22.
  It comes from <https://github.com/nothings/stb> at commit
  `1ee679ca2ef753a528db5ba6801e1067b40481b8`. The SHA-256 of the file is
  `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`.
  The license is MIT or public domain, as the end of the file tells.
  Do not change the file. Put each change in the code that uses it.
- `fdlibm/`: the math library of Sun Microsystems, from
  <https://www.netlib.org/fdlibm/> (2026-10-18). The license is in each
  file: a person may use, copy, change, and give the code, if the notice
  stays. Alpharune uses it because it gives the same bits on each
  platform. The C files are as netlib has them. `fdlibm.h` has three
  changes, each with a comment that starts with "Alpharune": the public
  names get the prefix `fd_`, clang on ARM and x86 counts as little endian,
  and the words of a double are read through a type that may alias it.
