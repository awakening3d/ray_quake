#ifdef USE_OPENCL


#include "CL/cl.h"
#include <stdio.h>
#include "windows.h"

#pragma comment( lib, "OpenCL.lib" )

extern "C" {
	#include "..\quakedef.h"
	#include "..\tracing.h"
}

const char* loadTxtFile(const char* path, size_t& size)
{
	const int maxsize = 1024 * 64;
	static char buff[maxsize];

	if (NULL == path) goto loadfailed;

	FILE* fp = fopen(path, "rb");
	if (!fp) goto loadfailed;

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	if (size > maxsize) {
		fclose(fp);
		goto loadfailed;
	}
	fseek(fp, 0, SEEK_SET);
	fread(buff, size, 1, fp);
	fclose(fp);
	return buff;

loadfailed:
	size = 0;
	return buff;
}



static cl_int error = 0; // error code
static cl_platform_id platform = NULL;
static cl_context context = NULL;
static cl_command_queue queue = NULL;
static cl_device_id device = NULL;

static cl_program program = NULL;
static cl_kernel clker = NULL;

static cl_mem pic = NULL;
static cl_mem tris = NULL;
static cl_mem info = NULL;
static cl_mem clipnodes = NULL;
static cl_mem planes = NULL;


void clClear()
{
	clReleaseKernel(clker);
	clReleaseProgram(program);
	clReleaseMemObject(pic);
	clReleaseMemObject(tris);
	clReleaseMemObject(info);
	clReleaseMemObject(clipnodes);
	clReleaseMemObject(planes);
	clReleaseCommandQueue(queue);
	clReleaseContext(context);
}


static const int trace_fnum = 4; // float number of trace data

static const size_t local_ws = 256; // number of work-items per work-group
static size_t global_ws = 0; // number of total work-items

static int W, H; // width & height of rendering pic


void showCLLog()
{
	// show log
	char* build_log;
	size_t log_size;
	// first call to know proper size
	clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
	build_log = new char[log_size + 1];
	// get the log
	clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, build_log, NULL);
	build_log[log_size] = '\0';
	OUTPUT_LOG("%s\n", build_log);
	delete[] build_log;
}

static const trinfo_t* ptrinfo = NULL;

bool clInit(int itemSize, int itemNum, void* srcdata, const trinfo_t& trinfo)
{
	ptrinfo = &trinfo;
	W = trinfo.width;
	H = trinfo.height;
	size_t pixelnum = W*H;

	global_ws = (pixelnum + local_ws - 1) / local_ws  * local_ws;

	error = clGetPlatformIDs(1, &platform, NULL);
	error = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
	context = clCreateContext(0, 1, &device, NULL, NULL, &error);
	queue = clCreateCommandQueue(context, device, 0, &error);

	const char* path = ".\\raytracing.cl";
	size_t src_size = 0;
	const char* source = loadTxtFile(path, src_size);
	program = clCreateProgramWithSource(context, 1, &source, &src_size, &error);
	if (CL_SUCCESS != error) {
		OUTPUT_LOG("---error:%d---CL CreateProgram failed: %s\n", error, path);
		goto clfailed;
	}

	// build the program
	error = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
	if (CL_SUCCESS != error) {
		OUTPUT_LOG("---error:%d---CL build program failed(%s):\n", error, path);
		showCLLog();
		goto clfailed;
	}

	// extract the kernel
	clker = clCreateKernel(program, "tracing_gpu", &error);
	if (CL_SUCCESS != error) {
		OUTPUT_LOG("---error:%d---CL CreateKernel failed!\n", error);
		goto clfailed;
	}

	// create buffers
	pic = clCreateBuffer(context, CL_MEM_WRITE_ONLY, pixelnum*sizeof(unsigned), NULL, &error);
	tris = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, itemSize * itemNum, srcdata, &error);
	info = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(trinfo), (void*)&trinfo, &error);
	
	// set parameters
	error = clSetKernelArg(clker, 0, sizeof(cl_mem), &pic);
	error |= clSetKernelArg(clker, 1, sizeof(cl_mem), &tris);
	error |= clSetKernelArg(clker, 2, sizeof(cl_mem), &info);
	error |= clSetKernelArg(clker, 3, sizeof(size_t), &pixelnum);
	if (CL_SUCCESS != error) {
		OUTPUT_LOG("---error:%d---CL set parameters failed!\n", error);
		goto clfailed;
	}


	bool bret = false;
	
	bret = true;

clfailed:

	return bret;
}


static bool levelfilled = false;

void cb_OnLevelLoaded()
{
	levelfilled = false;
}

void clFillLevelData()
{
	if (levelfilled) return;
	levelfilled = true;

	if (clipnodes) clReleaseMemObject(clipnodes);
	if (planes) clReleaseMemObject(planes);
	
	clipnodes = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
		sizeof(dclipnode_t)*ptrinfo->clipnodes_num, cl.worldmodel->hulls->clipnodes, &error);
	planes = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
		sizeof(mplane_t)*ptrinfo->planes_num, cl.worldmodel->hulls->planes, &error);

	error |= clSetKernelArg(clker, 4, sizeof(cl_mem), &clipnodes);
	error |= clSetKernelArg(clker, 5, sizeof(cl_mem), &planes);
}

bool clFillInfo(const void* srcdata, size_t bytenum)
{
	// fill info
	cl_int error = clEnqueueWriteBuffer(queue, info, CL_TRUE, 0, bytenum, srcdata, 0, NULL, NULL);
	return CL_SUCCESS == error;
}


// reading back
bool clReadPic(void* dest)
{
	cl_int error = clEnqueueReadBuffer(queue, pic, CL_TRUE, 0, W*H*sizeof(unsigned), dest, 0, NULL, NULL);
	return CL_SUCCESS == error;
}


// launching kernel
bool clRunKernel()
{
	cl_int error = clEnqueueNDRangeKernel(queue, clker, 1, NULL, &global_ws, &local_ws, 0, NULL, NULL);
	return CL_SUCCESS == error;
}

#endif