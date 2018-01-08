
#include "template.h"
#include <vector>


extern "C" {
#include "..\quakedef.h"
#include "..\tracing.h"

qboolean SV_RecursiveHullCheck(hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace);
void SV_MoveBounds(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, vec3_t boxmins, vec3_t boxmaxs);
void SV_ClipToLinks(int thread, areanode_t *node, moveclip_t *clip);
extern areanode_t	sv_areanodes[AREA_NODES];
extern msurface_t *gSkyChain;
}



std::vector<vector3> tris;

#ifdef USE_OPENCL
	bool clInit(int itemSize, int itemNum, void* srcdata, const trinfo_t& trinfo);
	bool GetWorldModelTris(trinfo_t& trinfo, void* rand_normals)
	{
		for (int s = 0; s < cl.worldmodel->numsurfaces; s++) {
			const msurface_t& ms = (cl.worldmodel->surfaces[s]);
			glpoly_t* gp = ms.polys;
			vector3 ver[3];
			for (int i = 0; i < gp->numverts; i++) {
				float	*v = gp->verts[i];
				if (i < 2) { //store first two vertices
					ver[i][0] = v[0]; ver[i][1] = v[1]; ver[i][2] = v[2];
					memcpy(&ver[i + 1], &ver[i], sizeof(ver[i]));
				}
				else {
					memcpy(&ver[1], &ver[2], sizeof(ver[1]));
					ver[2][0] = v[0]; ver[2][1] = v[1]; ver[2][2] = v[2];

					// push one tiangle
					tris.push_back(ver[0]);
					tris.push_back(ver[2]);
					tris.push_back(ver[1]);
				}
			}
		}

		trinfo.tri_num = tris.size() / 3;
		bool bret = clInit( sizeof(vector3), tris.size(), tris.data(), trinfo );
		if (!bret) OUTPUT_LOG("OpenCL init failed!\n");
		return bret;
	}
#endif

trace_t ClipMoveToWorldModel(int thread, const vector3& start, const vector3& end)
{
	trace_t		trace;

	// fill in a default trace
	memset(&trace, 0, sizeof(trace_t));
	trace.fraction = 1;
	trace.allsolid = true;
	VectorCopy(V3T(end), trace.endpos);


	int tr = 0;
	float t, u, v;
	int num = tris.size();

#if 0// use OpenCL
	
	// fill ray data
	bool clFillRay(const void* srcdata, size_t bytenum);
	float raydata[6] = { start.x, start.y, start.z, end.x, end.y, end.z };
	//clFillRay(raydata, sizeof(raydata));
	
	// launching kernel
	bool clRunKernel();
	//clRunKernel();

	// reading back
	bool clReadTrace(void* dest);
	float tracedata[4];
	//clReadTrace(tracedata);

	trace.fraction = tracedata[0];
	

#else // use CPU
	for (int i = 0; i < num; i += 3) {
		bool bInter = SegmentIntersectTriangle(start, end, tris[i], tris[i + 1], tris[i + 2], &t, &u, &v);
		if (bInter && t < trace.fraction) {
			trace.fraction = t;
			tr = i;
		}
	}
#endif


	
	if ( trace.fraction < 1.0f ) {
		trace.allsolid = false;
		VectorCopy(V3T( start + (end-start)*trace.fraction ), trace.endpos);
		vector3 trinormal = (tris[tr+1] - tris[tr]).Cross(tris[tr+2] - tris[tr]);
		trinormal.Normalize();
		VectorCopy(V3T(trinormal), trace.plane.normal);
		trace.plane.dist = tris[tr].Dot(trinormal);
		trace.content = 0;
	}

	return trace;
}


extern "C" { int rhcount; }

trace_t ClipMoveToHull(int thread, hull_t *hull, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	trace_t		trace;
	vec3_t		offset = { 0,0,0 };
	vec3_t		start_l, end_l;
	

	// fill in a default trace
	memset(&trace, 0, sizeof(trace_t));
	trace.fraction = 1;
	trace.allsolid = true;
	VectorCopy(end, trace.endpos);

	VectorSubtract(start, offset, start_l);
	VectorSubtract(end, offset, end_l);

	rhcount = 0;
	// trace a line through the apropriate clipping hull
	SV_RecursiveHullCheck(hull, hull->firstclipnode, 0, 1, start_l, end_l, &trace);

	// fix trace up by the offset
	if (trace.fraction != 1) VectorAdd(trace.endpos, offset, trace.endpos);

	// did we clip the move?
//	if (trace.fraction < 1 || trace.startsolid)
//		trace.ent = ent;

	return trace;
}


extern "C" {

	bool check_sky_content(vec3_t vstart, vec3_t vend, vec3_t vstop)
	{
		if (!gSkyChain) return false;
		const vector3& start = *(vector3*)vstart;
		const vector3& end = *(vector3*)vend;
		const vector3& stop = *(vector3*)vstop;
		for (msurface_t *fa = gSkyChain; fa; fa = fa->texturechain) {

			for (glpoly_t* p = fa->polys; p; p = p->next) {
				vector3 trivers[6];
				int trivernum = 0;

				if (3 == p->numverts) {
					trivers[0] = *(vector3*)&(p->verts[0]);
					trivers[1] = *(vector3*)&(p->verts[2]);
					trivers[2] = *(vector3*)&(p->verts[1]);
					trivernum = 3;
				}
				else if (4 == p->numverts) {
					trivers[0] = *(vector3*)&(p->verts[0]);
					trivers[1] = *(vector3*)&(p->verts[2]);
					trivers[2] = *(vector3*)&(p->verts[1]);
					trivers[3] = *(vector3*)&(p->verts[2]);
					trivers[4] = *(vector3*)&(p->verts[0]);
					trivers[5] = *(vector3*)&(p->verts[3]);
					trivernum = 6;
				}

				for (int i = 0; i < trivernum; i += 3) {
					float t, u, v;
					bool bInter = SegmentIntersectTriangle(start, end, trivers[i], trivers[i + 1], trivers[i + 2], &t, &u, &v);
					if (bInter) {
						vector3 vInter = start + (end - start)*t;
						if ((vInter - stop).SqrLength() < 0.01f) return true;
					}
				}

			}

		}
		return false;
	}

}
/*
==================
SV_Move
==================
*/
trace_t tr_Move(int thread, const vector3& start, const vector3& end, int type, edict_t *passedict)
{
	moveclip_t	clip;
	int			i;

	memset(&clip, 0, sizeof(moveclip_t));

	// clip to world
	clip.trace = ClipMoveToHull(thread, cl.worldmodel->hulls, V3T(start), vec3_origin, vec3_origin, V3T(end) );
	//clip.trace = ClipMoveToWorldModel(thread, start, end);

	/*
	//--- for sky content test
	if (CONTENTS_SKY == clip.trace.content) {
		if (!check_sky_content(V3T(start), V3T(end), clip.trace.endpos))
			clip.trace.content = CONTENTS_EMPTY;
	}
	*/


	clip.start = V3T(start);
	clip.end = V3T(end);
	clip.mins = vec3_origin;
	clip.maxs = vec3_origin;
	clip.type = type;
	clip.passedict = passedict;

	if (type == MOVE_MISSILE)
	{
		for (i = 0; i<3; i++)
		{
			clip.mins2[i] = -15;
			clip.maxs2[i] = 15;
		}
	}
	else
	{
		VectorCopy(vec3_origin, clip.mins2);
		VectorCopy(vec3_origin, clip.maxs2);
	}

	// create the bounding box of the entire move
	SV_MoveBounds( V3T(start), clip.mins2, clip.maxs2, V3T(end), clip.boxmins, clip.boxmaxs);

	// clip to entities
	SV_ClipToLinks(thread, sv_areanodes, &clip);

	return clip.trace;
}



qboolean	gBlockOnly[MAX_TRACE_THREAD_NUM];
trace_t		CL_PMTrace_thread(int thread, const vector3& start, const vector3& end)
{
	edict_t	*ent=NULL;
	trace_t trace = tr_Move(thread, start, end, MOVE_NOMONSTERS, ent);
	return trace;
}

