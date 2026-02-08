#pragma once

#include <thread>

// use this to set the number of threads for the compress or decompress object using the number of cores in the CPU
#define APE_SET_THREADS_USING_CORES(APE_OBJECT) APE_OBJECT->SetNumberOfThreads(APE_MAX(2, std::thread::hardware_concurrency() / 4))

