# C Fiber

An implementation of green threads in c, with c++ templates, for x86_64 processors.

Two synchronization primitives are provided
* Semaphore: Allow a number of concurrent fibers
* Barrier: All waiting fibers are released at once

`czsf_wait()` on a sync primitive **must** be called from a fiber.

To use the header-only version include the following snippet in a source file:
```c++
#define CZSF_IMPLEMENTATION
#include "czsf.h"
```

Based on the presentation [Parallelizing the Naughty Dog Engine Using Fibers](https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine).

## Building the example

Linux:

```sh
g++ example.cpp -pthread
```

Windows:

```sh
g++ example.cpp
```
