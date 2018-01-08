#include "template.h"

//#define USE_OPENCL // whether use opencl to rendering, you should define it in VC's Preprocessor definitions
//( you need copy raytracing.cl to game folder for opencl rendering )

extern "C" {
	#include "..\quakedef.h"
	#include "..\tracing.h"
	extern dlight_t		cl_dlights[MAX_DLIGHTS];
	void Draw_StretchRaw(int x, int y, int w, int h, int cols, int rows, byte *data, qboolean rgba);
}

extern qboolean		gBlockOnly[MAX_TRACE_THREAD_NUM];

trace_t		CL_PMTrace_thread(int thread, const vector3& start, const vector3& end);


vec3_t contentcolor[32];

cvar_t tl_test = { "tl_test", "99" }; // for teset
cvar_t tl_interlaced = { "tl_interlaced", "0" }; // line interlace
cvar_t tl_ambient = { "tl_ambient", "16" }; // ambient lighting
cvar_t tl_exposure = { "tl_exposure", "0.4" }; // exposure factor
cvar_t tl_aorange = { "tl_aorange", "600" }; // testing range for ao lighting
cvar_t tl_dlightsnum = { "tl_dlightsnum", "64" }; // max dynamic lights number
cvar_t tl_dlights_shadow_num = { "tl_dlights_shadow_num", "8" }; // Up to how many lights can have shadow?


//#define SINGLE_THREAD_RENDERING	// whether use single thread rendering

static HANDLE thread_handles[MAX_TRACE_THREAD_NUM];



typedef struct { int x; int y; } iv2d;


#define dmRand() (((float)rand()) / RAND_MAX)



static const int W = 128;
static const int H = 128;
float gNearClip = H / 2;


static const int gNormalNum = 16;
static vector3 vRadomNormal[gNormalNum];

static void GenerateRadomNormals()
{
	//static bool bInit = false;
	//if (bInit) return;
	//bInit = true;
	const float vrange = 0.5f;
	int i = 0;
	while (true) {
		float fx = dmRand() * 2 * vrange - vrange;
		float fy = dmRand() * 2 * vrange - vrange;
		if (fx*fx + fy*fy >= 1) continue;

		float fz = sqrt(1.0f - fx*fx - fy*fy);
		vRadomNormal[i++] = vector3(fx, fy, fz);
		if (i >= gNormalNum) break;
	}

}

float Expose(float light, float exposure)
{
	return (1.0f - exp(-light * exposure));
}


vec3_t v_forward, v_right, v_up;
float screenaspect;

unsigned pixelcolor(int thread, int x, int y)
{
	int r = 0, g = 0, b = 0, a = 255;
	
	vector3 vieworg(r_refdef.vieworg);

	vector3 worldx(v_right[0], v_up[0], v_forward[0]);
	vector3 worldy(v_right[1], v_up[1], v_forward[1]);
	vector3 worldz(v_right[2], v_up[2], v_forward[2]);

#define RAYLEN 1500

	vector3 rdv(x, screenaspect*y, gNearClip); // view space
	rdv.Normalize();
	vector3 raydir(rdv.Dot(worldx), rdv.Dot(worldy), rdv.Dot(worldz)); // world space
	vector3 end = vieworg + raydir * RAYLEN;

	trace_t t = CL_PMTrace_thread(thread, vieworg, end);
	if (1 == t.fraction) { // don't hit anything
		r = 255, g = 150, b = 100;
		goto output;
	}
		
	if (CONTENTS_WATER == t.content || CONTENTS_SLIME == t.content || CONTENTS_LAVA == t.content || CONTENTS_SKY == t.content) {
		r = 255 * contentcolor[-t.content][0] + 128;
		g = 255 * contentcolor[-t.content][1] + 128;
		b = 255 * contentcolor[-t.content][2] + 128;
		goto output;
	}


	//r=g=b = t.fraction * 128;
	vector3 pnormal(t.plane.normal);
	quaternion rot;
	rot.rotationFromTo(vector3(0, 0, 1), pnormal);

	vector3 start = t.endpos;
	vector3 pn;
	float sum = 0;
	float skylight[3] = { 0 };

	// ao lighting
	if (tl_aorange.value > 0) {
		GenerateRadomNormals();
		for (int r = 0; r < gNormalNum; r++) {
			pn = rot * vRadomNormal[r];
			end = start + pn / pn.Length() * tl_aorange.value;
			trace_t tt = CL_PMTrace_thread(thread, start, end);

			if (CONTENTS_WATER == tt.content || CONTENTS_SLIME == tt.content || CONTENTS_LAVA == tt.content || CONTENTS_SKY == tt.content) {
				skylight[0] += 0.91f * contentcolor[-tt.content][0];
				skylight[1] += 0.91f * contentcolor[-tt.content][1];
				skylight[2] += 0.91f * contentcolor[-tt.content][2];
			}
			sum += tt.fraction;
		}

		float exposure = tl_exposure.value / gNormalNum;
		r = g = b = Expose(sum, exposure) * 255 + tl_ambient.value;
		skylight[0] = Expose(skylight[0], exposure);
		skylight[1] = Expose(skylight[1], exposure);
		skylight[2] = Expose(skylight[2], exposure);
		r += skylight[0] * 255;
		g += skylight[1] * 255;
		b += skylight[2] * 255;
	}

	// dynamic lights
	int dlightsnum = min(tl_dlightsnum.value, MAX_DLIGHTS);
	int shadowcount = 0;
	for (int i = 0; i < dlightsnum; i++) {
		const dlight_t& dl = cl_dlights[i];
		if (dl.die < cl.time || !dl.radius) continue;
		if (dl.intensity <= 0) continue;

		vector3 lightpos(dl.origin);

		if (lightpos.Dot(pnormal) - t.plane.dist <= 0) continue; // light in back of plane

		vector3 tolight = lightpos - start;
		float dist = tolight.Length();

		if (dist > dl.radius * 1.5f ) continue;

		tolight = tolight / dist; // normzize
		float diffuse = pnormal.Dot(tolight);
		float atten = 0.1f / (1.0f + 0.5f * dist + 0.005f * dist*dist);
		diffuse *= atten * dl.intensity;
		if (diffuse > 0.03f) {
			if (shadowcount++ < tl_dlights_shadow_num.value) {
				gBlockOnly[thread] = TRUE; // faster tracing, no need distance
				trace_t tt = CL_PMTrace_thread(thread, start, lightpos);
				gBlockOnly[thread] = FALSE;
				if (tt.fraction < 1.0f) { // hit something
					diffuse = (1 - diffuse)*0.033f;
				}
			}
		}
		r += diffuse * 255 * dl.color[0];
		g += diffuse * 255 * dl.color[1];
		b += diffuse * 255 * dl.color[2];
	}


output:

	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;

	return a << 24 | b << 16 | g << 8 | r;
}


class CTile
{
public:
	static const int pixelnum = 64 * 64;
private:
	iv2d pixels[pixelnum];
	int count;
public:
	CTile() {
		clear();
	}
	void clear() { count = 0; }
	inline const iv2d& getPixel(int i) { return pixels[i]; }
	bool pushpixel(const iv2d& iv) // return true if tile is full
	{
		pixels[count++] = iv;
		if (count >= pixelnum) return true;
		return false;
	}

};

static CTile	tiles[MAX_TRACE_THREAD_NUM];
static int	tilenum = 0;


static unsigned pic[W*H];
static const int halfW = W / 2;
static const int halfH = H / 2;


unsigned int tile_thread_proc(void* param)
{
	int t = (int)param;

	for (int p = 0; p < CTile::pixelnum; p++) {
		int w = tiles[t].getPixel(p).x;
		int h = tiles[t].getPixel(p).y;
		int i = h*W + w;
		pic[i] = pixelcolor(t, w - halfW, H - h - halfH);
	}

	return 0;
}

bool GetWorldModelTris(trinfo_t& trinfo, void* rand_normals);
bool clFillInfo(const void* srcdata, size_t bytenum);
void clFillLevelData();
bool clRunKernel();
bool clReadPic(void* dest); // reading back

extern "C" {


	void PathTracingFrame()
	{

		//if (cl.time < 8) return;
		AngleVectors(r_refdef.viewangles, v_forward, v_right, v_up);
		screenaspect = (float)r_refdef.vrect.height / r_refdef.vrect.width;
		gNearClip = (float)screenaspect*H*0.5f / tanf(r_refdef.fov_y*0.5f / 180 * PI);

		static trinfo_t trinfo = {
			W, H,
			screenaspect,
			gNearClip,
		};

		static bool clInited = false;

		static bool bregvar = true;
		if (bregvar) {
			bregvar = false;
			Cvar_RegisterVariable(&tl_test);
			Cvar_RegisterVariable(&tl_interlaced);
			Cvar_RegisterVariable(&tl_ambient);
			Cvar_RegisterVariable(&tl_exposure);
			Cvar_RegisterVariable(&tl_aorange);
			Cvar_RegisterVariable(&tl_dlightsnum);
			Cvar_RegisterVariable(&tl_dlights_shadow_num);

			for (int i = 0; i < 32; i++) VectorSet(contentcolor[i], 1, 1, 1);
			VectorSet(contentcolor[-CONTENTS_WATER], 0, 1, 0);
			VectorSet(contentcolor[-CONTENTS_SLIME], 0.6, 0.6, 0);
			VectorSet(contentcolor[-CONTENTS_LAVA], 1, 0.4, 0);
			VectorSet(contentcolor[-CONTENTS_SKY], 0.5, 0, 0.7);
	

			GenerateRadomNormals();
			trinfo.rand_normal_num = gNormalNum;
#ifdef USE_OPENCL
			clInited = GetWorldModelTris(trinfo, vRadomNormal);
#endif

			//r_lightmap.value = 1;
		}



#ifdef USE_OPENCL
	if (clInited) {
		vector3 worldx(v_right[0], v_up[0], v_forward[0]);
		vector3 worldy(v_right[1], v_up[1], v_forward[1]);
		vector3 worldz(v_right[2], v_up[2], v_forward[2]);

		memcpy(trinfo.vieworg, r_refdef.vieworg, sizeof(trinfo.vieworg));
		memcpy(trinfo.worldx, V3T(worldx), sizeof(trinfo.worldx));
		memcpy(trinfo.worldy, V3T(worldy), sizeof(trinfo.worldy));
		memcpy(trinfo.worldz, V3T(worldz), sizeof(trinfo.worldz));

		trinfo.ambient = tl_ambient.value;
		trinfo.exposure = tl_exposure.value;
		trinfo.aorange = tl_aorange.value;
		trinfo.clipnodes_num = cl.worldmodel->hulls->lastclipnode + 1;
		trinfo.planes_num = cl.worldmodel->numplanes;

		clFillInfo(&trinfo, sizeof(trinfo));
		clFillLevelData();
		clRunKernel();
		clReadPic(pic);
		Draw_StretchRaw(r_refdef.vrect.x, r_refdef.vrect.y, r_refdef.vrect.width, r_refdef.vrect.height, W, H, (byte*)pic, true);
	}
	return;
#endif

		static iv2d pc[4] = {
			{ 0, 0 }, { 1, 1 }, { 0, 1 }, { 1, 0 }
		};
		static int pci = 0;

		const iv2d& pp = pc[pci++];
		if (pci >= 4) pci = 0;

		static int odd = 0;
		odd = !odd;


		iv2d iv;

		// first clear tiles
		for (int t = 0; t < MAX_TRACE_THREAD_NUM; t++) tiles[t].clear();
		tilenum = 0;

		// push pixels to tiles
		for (int h = 0; h < H; h++)
			for (int w = 0; w < W; w++) {
				//if (w % 2 == pp.x && h % 2 == pp.y) {
				if (!tl_interlaced.value || h % 2 == odd) {
					iv.x = w; iv.y = h;
					if (tiles[tilenum].pushpixel(iv))
						tilenum++;
				}
			}

		// render tiles
		for (int t = 0; t < tilenum; t++) {
#ifdef SINGLE_THREAD_RENDERING
			tile_thread_proc((void*)t);
#else
			DWORD threadId = 0;
			thread_handles[t] = (HANDLE)CreateThread(NULL, 0,
				(LPTHREAD_START_ROUTINE)tile_thread_proc, (void*)t, 0, &threadId);
#endif
		}

#ifndef SINGLE_THREAD_RENDERING
		WaitForMultipleObjects(tilenum, thread_handles, true, INFINITE);
#endif

		Draw_StretchRaw(r_refdef.vrect.x, r_refdef.vrect.y, r_refdef.vrect.width, r_refdef.vrect.height, W, H, (byte*)pic, true);
	}

}

