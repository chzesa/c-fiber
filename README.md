# CPP Fiber

An implementation of green threads in c++.

Two synchronization primitives are provided
* Semaphore: Allow a number of concurrent fibers
* Barrier: All waiting fibers are released at once

`wait()` on a sync primitive **must** be called from a fiber.

Based on the presentation [Parallelizing the Naughty Dog Engine Using Fibers](https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine).

## Building

Linux:

```sh
g++ src/fiber.cpp example.cpp -pthread
```

Windows:

```sh
g++ src/fiber.cpp example.cpp
```
