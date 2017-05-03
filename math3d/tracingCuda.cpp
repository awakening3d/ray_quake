
// For the CUDA runtime routines (prefixed with "cuda_")
#include <cuda_runtime.h>
#include <cuda.h>

// helper functions and utilities to work with CUDA
#include <helper_functions.h>

#include <nvrtc_helper.h>



bool loadCudaFile(const char* filename)
{
	char *ptx, *kernel_file;
	size_t ptxSize;
	kernel_file = sdkFindFilePath(filename, 0);
	compileFileToPTX(kernel_file, 0, NULL, &ptx, &ptxSize);
	CUmodule module = loadPTX(ptx, 0, 0);

	CUfunction kernel_addr;
	checkCudaErrors(cuModuleGetFunction(&kernel_addr, module, "vectorAdd"));

	return true;
}