An implementation of green threads in c++.

Two synchronization primitives are provided
* Semaphore: Allow a number of concurrent fibers
* Barrier: All waiting fibers are released at once

`wait()` for a sync primitive must be called from a fiber.

Based on the presentation [Parallelizing the Naughty Dog Engine Using Fibers](https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine).