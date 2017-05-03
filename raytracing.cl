
// 1/32 epsilon to keep floating point happy
#define	DIST_EPSILON	(0.03125)


typedef float vec3_t[3];
#define DotProduct(x,y) (x[0]*y[0]+x[1]*y[1]+x[2]*y[2])
#define VectorSubtract(a,b,c) {c[0]=a[0]-b[0];c[1]=a[1]-b[1];c[2]=a[2]-b[2];}
#define VectorAdd(a,b,c) {c[0]=a[0]+b[0];c[1]=a[1]+b[1];c[2]=a[2]+b[2];}
#define VectorCopy(a,b) {b[0]=a[0];b[1]=a[1];b[2]=a[2];}
#define VectorScale(in, s, out) { out[0] = in[0]*s; out[1] = in[1]*s; out[2] = in[2]*s; }


float randomf(unsigned int* seed) // uniform between 0-1
{
	*seed = ((*seed) * 16807 ) % 2147483647;
	return (float)(*seed) * 4.6566129e-10; // 4.6566129e-10 = 1/(2^31-1) = 1/2147483647
}

float Expose(float light, float exposure)
{
	return (1.0f - exp(-light * exposure));
}


bool rayintertri( float3 org, float3 dir, float3 v0, float3 v1, float3 v2, float* t, float* u, float* v, bool bBackFace )
{
		// find vectors for two edges sharing v0
		float3 edge1 = v1 - v0;
		float3 edge2 = v2 - v0;

		// begin calculating determinant - also used to calculate U parameter
		float3 pvec = cross( dir, edge2 );

		// calculate distance from v0 to ray origin
		float3 tvec = org - v0;

		// if determinant is near zero, ray lies in plane of triangle
		float det = dot( edge1, pvec );

		if (bBackFace && det < 0) { // also test back side
			det = -det;
			tvec = -tvec;
		}

		if (det < 0.0001f) return false;

		// calculate U parameter and test bounds
		*u = dot( tvec, pvec);
		if (*u < 0.0f || *u > det) return false;

		// prepare to test V parameter
		float3 qvec = cross( tvec, edge1 );

		// calculate V parameter and test bounds
		*v = dot( dir, qvec );
		if (*v < 0.0f || *u + *v > det) return false;

		// calculate t, scale parameters, ray intersects triangle
		*t = dot( edge2, qvec );
		float fInvDet = 1.0f / det;
		*t *= fInvDet;
		*u *= fInvDet;
		*v *= fInvDet;

		return true;
}



typedef struct
{
	int			planenum;
	short		children[2];	// negative numbers are contents
} dclipnode_t;

typedef struct mplane_s
{
	vec3_t	normal;
	float	dist;
	uchar	type;			// for texture axis selection and fast side tests
	uchar	signbits;		// signx + signy<<1 + signz<<1
	uchar	pad[2];
} mplane_t;



typedef struct {
	float fraction,u,v;

	int tri_num;
	__global float4* pver;

	float3 v0, v1, v2;

	mplane_t	plane;			// surface normal at impact
} trace_tt;


typedef struct
{
	bool	allsolid;	// if true, plane is not valid
	bool	startsolid;	// if true, the initial point was in a solid area
	bool	inopen, inwater;
	float	fraction;		// time completed, 1.0 = didn't hit anything
	vec3_t	endpos;			// final position
	mplane_t	plane;			// surface normal at impact
	int content; // surface content
} trace_t;



typedef struct
{
	float width, height;
	float screenaspect;
	float nearclip;

	vec3_t vieworg;
	vec3_t worldx, worldy, worldz;

	float tri_num;
	float rand_normal_num; // random normals number
	float ambient; // ambient lighting
	float exposure; // exposure factor
	float aorange; // testing range for ao lighting

	float clipnodes_num; // hull clipnodes number
	float planes_num; // hull planes number
} trinfo_t;

typedef struct {
	int num;
	float p1f, p2f;
	vec3_t p1, p2;

	float		t1, t2;
	float		frac;
	vec3_t		mid;
	int			side;
	float		midf;

	__global dclipnode_t	*node;
	__global mplane_t	*plane;


	int addr; // address of return
} param_t;


typedef struct
{
	__global const trinfo_t* pinfo;
	__global const dclipnode_t* clipnodes;
	__global const mplane_t* planes;

	int d;
	param_t p[512];

} world_t;


void push(world_t* pworld, const param_t* param)
{
	param_t* p = pworld->p;
	p[pworld->d].num = param->num;
	p[pworld->d].p1f = param->p1f;
	p[pworld->d].p2f = param->p2f;
	VectorCopy( param->p1, p[pworld->d].p1 );
	VectorCopy( param->p2, p[pworld->d].p2 );
	p[pworld->d].t1 = param->t1;
	p[pworld->d].t2 = param->t2;
	p[pworld->d].frac = param->frac;
	VectorCopy( param->mid, p[pworld->d].mid );
	p[pworld->d].side = param->side;
	p[pworld->d].midf = param->midf;
	p[pworld->d].node = param->node;
	p[pworld->d].plane = param->plane;
	p[pworld->d].addr = param->addr;

	pworld->d++;
}

void pop(world_t* pworld, param_t* param)
{
	pworld->d--;

	const param_t* p = pworld->p;
	param->num = p[pworld->d].num;
	param->p1f = p[pworld->d].p1f;
	param->p2f = p[pworld->d].p2f;
	VectorCopy( p[pworld->d].p1, param->p1 );
	VectorCopy( p[pworld->d].p2, param->p2 );
	param->t1 = p[pworld->d].t1;
	param->t2 = p[pworld->d].t2;
	param->frac = p[pworld->d].frac;
	VectorCopy( p[pworld->d].mid, param->mid );
	param->side = p[pworld->d].side;
	param->midf = p[pworld->d].midf;
	param->node = p[pworld->d].node;
	param->plane = p[pworld->d].plane;
	param->addr = p[pworld->d].addr;
}



void trace_world(trace_tt* ptrace, float3 org, float3 dir)
{
	bool bBackFace = false;

	__global float4* pver = ptrace->pver;

	float t, u, v;
	for (int i=0; i<ptrace->tri_num; i++, pver += 3 )
	{
		float3 v0 = pver[0].xyz;
		float3 v1 = pver[1].xyz;
		float3 v2 = pver[2].xyz;
		bool binter = rayintertri( org, dir, v0, v1, v2, &t, &u, &v, bBackFace );
		if (!binter) continue;
		if (t<0 || t>= ptrace->fraction) continue;
		ptrace->fraction = t;
		ptrace->u = u;
		ptrace->v = v;
		ptrace->v0 = v0;
		ptrace->v1 = v1;
		ptrace->v2 = v2;
	}
}


#define	CONTENTS_EMPTY		-1
#define	CONTENTS_SOLID		-2
#define	CONTENTS_WATER		-3
#define	CONTENTS_SLIME		-4
#define	CONTENTS_LAVA		-5
#define	CONTENTS_SKY		-6
#define	CONTENTS_ORIGIN		-7		// removed at csg time
#define	CONTENTS_CLIP		-8		// changed to contents_solid


int HullPointContents ( const world_t* pworld, int num, vec3_t p)
{
	float		d;
	__global dclipnode_t	*node;
	__global mplane_t	*plane;

	while (num >= 0) {
//		if (num < hull->firstclipnode || num > hull->lastclipnode)
//			Sys_Error ("SV_HullPointContents: bad node number");
	
		node = pworld->clipnodes + num;
		plane = pworld->planes + node->planenum;
		
		if (plane->type < 3)
			d = p[plane->type] - plane->dist; // axis-aligned plane
		else
			d = DotProduct (plane->normal, p) - plane->dist; // distance to plane
		if (d < 0)
			num = node->children[1]; // in back of plane
		else
			num = node->children[0]; // in front of plane
	}
	
	return num;
}

/*
bool RecursiveHullCheck ( const world_t* pworld, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	__global dclipnode_t	*node;
	__global mplane_t	*plane;

	param_t p = { num, p1f, p2f }; VectorCopy( p1, p.p1 ); VectorCopy( p2, p.p2 );


	// check for empty
	if (p.num < 0) {
		if (p.num != CONTENTS_SOLID) {
			trace->allsolid = false;
			if (p.num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
		} else
			trace->startsolid = true;

		trace->content = p.num;

		return true;		// empty
	}

//	if (p.num >= pinfo->clipnodes_num)
//		Sys_Error ("SV_RecursiveHullCheck: bad node number");


// find the point distances
	node = pworld->clipnodes + p.num;
	plane = pworld->planes + node->planenum;

	if (plane->type < 3) {
		p.t1 = p.p1[plane->type] - plane->dist;
		p.t2 = p.p2[plane->type] - plane->dist;
	} else {
		p.t1 = DotProduct (plane->normal, p.p1) - plane->dist;
		p.t2 = DotProduct (plane->normal, p.p2) - plane->dist;
	}
	

	if (p.t1 >= 0 && p.t2 >= 0)
		return RecursiveHullCheck (pworld, node->children[0], p.p1f, p.p2f, p.p1, p.p2, trace);
	if (p.t1 < 0 && p.t2 < 0)
		return RecursiveHullCheck (pworld, node->children[1], p.p1f, p.p2f, p.p1, p.p2, trace);

// put the crosspoint DIST_EPSILON pixels on the near side
	if (p.t1 < 0)
		p.frac = (p.t1 + DIST_EPSILON)/(p.t1-p.t2);
	else
		p.frac = (p.t1 - DIST_EPSILON)/(p.t1-p.t2);
	if (p.frac < 0)
		p.frac = 0;
	if (p.frac > 1)
		p.frac = 1;
		
	p.midf = p.p1f + (p.p2f - p.p1f)*p.frac;
	for (int i=0; i<3; i++)
		p.mid[i] = p.p1[i] + p.frac*(p.p2[i] - p.p1[i]);

	p.side = (p.t1 < 0);

	// move up to the node
	if (!RecursiveHullCheck (pworld, node->children[p.side], p.p1f, p.midf, p.p1, p.mid, trace) )
		return false;

	if (HullPointContents (pworld, node->children[p.side^1], p.mid) != CONTENTS_SOLID)
		return RecursiveHullCheck (pworld, node->children[p.side^1], p.midf, p.p2f, p.mid, p.p2, trace); // go past the node
	
	if (trace->allsolid)
		return false;		// never got out of the solid area
		
	//==================
	// the other side of the node is solid, this is the impact point
	//==================
	if (!p.side) {
		VectorCopy (plane->normal, trace->plane.normal);
		trace->plane.dist = plane->dist;
	} else {
		VectorScale( plane->normal, -1, trace->plane.normal );
		trace->plane.dist = -plane->dist;
	}
	
	trace->fraction = p.midf;
	VectorCopy (p.mid, trace->endpos);

	return false;
}
*/


bool __RecursiveHullCheck ( const world_t* pworld, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	param_t p = { num, p1f, p2f }; VectorCopy( p1, p.p1 ); VectorCopy( p2, p.p2 );

	bool result;

begincall:

	// check for empty
	if (p.num < 0) {
		if (p.num != CONTENTS_SOLID) {
			trace->allsolid = false;
			if (p.num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
		} else
			trace->startsolid = true;

		trace->content = p.num;

		//return true;
		result = true; goto endcall;
	}

//	if (p.num >= pinfo->clipnodes_num)
//		Sys_Error ("SV_RecursiveHullCheck: bad node number");


// find the point distances
	p.node = pworld->clipnodes + p.num;
	p.plane = pworld->planes + p.node->planenum;

	if (p.plane->type < 3) {
		p.t1 = p.p1[p.plane->type] - p.plane->dist;
		p.t2 = p.p2[p.plane->type] - p.plane->dist;
	} else {
		p.t1 = DotProduct (p.plane->normal, p.p1) - p.plane->dist;
		p.t2 = DotProduct (p.plane->normal, p.p2) - p.plane->dist;
	}
	

	if (p.t1 >= 0 && p.t2 >= 0) {
		//return RecursiveHullCheck (pworld, p.node->children[0], p.p1f, p.p2f, p.p1, p.p2, trace);
		p.addr = 1;
		push( pworld, &p );
		p.num = p.node->children[0];
		goto begincall;
returncall1:
		goto endcall;	
	}
	if (p.t1 < 0 && p.t2 < 0) {
		//return RecursiveHullCheck (pworld, p.node->children[1], p.p1f, p.p2f, p.p1, p.p2, trace);
		p.addr = 2;
		push( pworld, &p );
		p.num = p.node->children[1];
		goto begincall;
returncall2:
		goto endcall;	
	}

// put the crosspoint DIST_EPSILON pixels on the near side
	if (p.t1 < 0)
		p.frac = (p.t1 + DIST_EPSILON)/(p.t1-p.t2);
	else
		p.frac = (p.t1 - DIST_EPSILON)/(p.t1-p.t2);
	if (p.frac < 0)
		p.frac = 0;
	if (p.frac > 1)
		p.frac = 1;
		
	p.midf = p.p1f + (p.p2f - p.p1f)*p.frac;
	for (int i=0; i<3; i++)
		p.mid[i] = p.p1[i] + p.frac*(p.p2[i] - p.p1[i]);

	p.side = (p.t1 < 0);

	// move up to the node
//	if (!RecursiveHullCheck (pworld, p.node->children[p.side], p.p1f, p.midf, p.p1, p.mid, trace) )
//		return false;
	p.addr = 3;
	push( pworld, &p );
	p.num = p.node->children[p.side];
	p.p2f = p.midf;
	VectorCopy( p.mid, p.p2 );
	goto begincall;
returncall3:
	if (!result) {
		result = false; goto endcall;
	}
		


	if (HullPointContents (pworld, p.node->children[p.side^1], p.mid) != CONTENTS_SOLID) {
		//return RecursiveHullCheck (pworld, p.node->children[p.side^1], p.midf, p.p2f, p.mid, p.p2, trace); // go past the node
		p.addr = 4;
		push( pworld, &p );
		p.num = p.node->children[p.side^1];
		p.p1f = p.midf;
		VectorCopy( p.mid, p.p1 );
		goto begincall;
returncall4:
		goto endcall;
	}
	
	if (trace->allsolid) {
		//return false;		// never got out of the solid area
		result = false; goto endcall;
	}
		
	//==================
	// the other side of the node is solid, this is the impact point
	//==================
	if (!p.side) {
		VectorCopy (p.plane->normal, trace->plane.normal);
		trace->plane.dist = p.plane->dist;
	} else {
		VectorScale( p.plane->normal, -1, trace->plane.normal );
		trace->plane.dist = -p.plane->dist;
	}
	
	trace->fraction = p.midf;
	VectorCopy (p.mid, trace->endpos);

	//return false;
	result = false; goto endcall;


endcall: // exit function
	if (0==pworld->d) return result;
	pop( pworld, &p );
	switch (p.addr) {
		case 1: goto returncall1;
		case 2: goto returncall2;
		case 3: goto returncall3;
		case 4: goto returncall4;
	}

}




bool RecursiveHullCheck ( const world_t* pworld, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	param_t p = { num, p1f, p2f }; VectorCopy( p1, p.p1 ); VectorCopy( p2, p.p2 );

	bool result;


while (true) {
	// check for empty
	if (p.num < 0) {
		if (p.num != CONTENTS_SOLID) {
			trace->allsolid = false;
			if (p.num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
		} else
			trace->startsolid = true;

		trace->content = p.num;

		//return true;
		result = true;
		
		bool b = false;
		while (0 != pworld->d) { // endcall
			pop( pworld, &p );
			if ( 3 == p.addr ) {
				if (!result) {
					result = false; continue;
				}

				if (HullPointContents (pworld, p.node->children[p.side^1], p.mid) != CONTENTS_SOLID) {
					//return RecursiveHullCheck (pworld, p.node->children[p.side^1], p.midf, p.p2f, p.mid, p.p2, trace); // go past the node
					p.addr = 4;
					push( pworld, &p );
					p.num = p.node->children[p.side^1];
					p.p1f = p.midf;
					VectorCopy( p.mid, p.p1 );
					b = true;
					break;
				}
				
				if (trace->allsolid) {
					//return false;		// never got out of the solid area
					result = false; continue;
				}
					
				//==================
				// the other side of the node is solid, this is the impact point
				//==================
				if (!p.side) {
					VectorCopy (p.plane->normal, trace->plane.normal);
					trace->plane.dist = p.plane->dist;
				} else {
					VectorScale( p.plane->normal, -1, trace->plane.normal );
					trace->plane.dist = -p.plane->dist;
				}
				
				trace->fraction = p.midf;
				VectorCopy (p.mid, trace->endpos);

				//return false;
				result = false; continue;

			}

		} // of while (0 != pworld->d)
		if (b) continue; else break;


	} // of if (p.num < 0)


//	if (p.num >= pinfo->clipnodes_num)
//		Sys_Error ("SV_RecursiveHullCheck: bad node number");


// find the point distances
	p.node = pworld->clipnodes + p.num;
	p.plane = pworld->planes + p.node->planenum;

	if (p.plane->type < 3) {
		p.t1 = p.p1[p.plane->type] - p.plane->dist;
		p.t2 = p.p2[p.plane->type] - p.plane->dist;
	} else {
		p.t1 = DotProduct (p.plane->normal, p.p1) - p.plane->dist;
		p.t2 = DotProduct (p.plane->normal, p.p2) - p.plane->dist;
	}
	

	if (p.t1 >= 0 && p.t2 >= 0) {
		//return RecursiveHullCheck (pworld, p.node->children[0], p.p1f, p.p2f, p.p1, p.p2, trace);
		p.addr = 1;
		push( pworld, &p );
		p.num = p.node->children[0];
		continue;
	}
	if (p.t1 < 0 && p.t2 < 0) {
		//return RecursiveHullCheck (pworld, p.node->children[1], p.p1f, p.p2f, p.p1, p.p2, trace);
		p.addr = 2;
		push( pworld, &p );
		p.num = p.node->children[1];
		continue;
	}

// put the crosspoint DIST_EPSILON pixels on the near side
	if (p.t1 < 0)
		p.frac = (p.t1 + DIST_EPSILON)/(p.t1-p.t2);
	else
		p.frac = (p.t1 - DIST_EPSILON)/(p.t1-p.t2);
	if (p.frac < 0)
		p.frac = 0;
	if (p.frac > 1)
		p.frac = 1;
		
	p.midf = p.p1f + (p.p2f - p.p1f)*p.frac;
	for (int i=0; i<3; i++)
		p.mid[i] = p.p1[i] + p.frac*(p.p2[i] - p.p1[i]);

	p.side = (p.t1 < 0);

	// move up to the node
//	if (!RecursiveHullCheck (pworld, p.node->children[p.side], p.p1f, p.midf, p.p1, p.mid, trace) )
//		return false;
	p.addr = 3;
	push( pworld, &p );
	p.num = p.node->children[p.side];
	p.p2f = p.midf;
	VectorCopy( p.mid, p.p2 );
	continue;

} // of while (true)

	return result;

}


__kernel void tracing_gpu( __global unsigned* pic, __global const float4* tri, __global const trinfo_t* pinfo, const int num,
	__global const dclipnode_t* clipnodes, __global const mplane_t* planes)
{
	const int idx = get_global_id(0);
	if (idx>=num) return;


	int W = pinfo->width;
	int H = pinfo->height;
	int w = idx % W;
	int h = idx / W;

	int halfW = W / 2;
	int halfH = H / 2;

	int x = w - halfW;
	int y = H - h - halfH;

__global const float* info = (__global const float*)pinfo;
	float screenaspect = pinfo->screenaspect;
	float gNearClip = pinfo->nearclip;
	float3 org = (float3)(pinfo->vieworg[0],pinfo->vieworg[1],pinfo->vieworg[2]); // view org
	float3 worldx = (float3)(pinfo->worldx[0],pinfo->worldx[1],pinfo->worldx[2]);
	float3 worldy = (float3)(pinfo->worldy[0],pinfo->worldy[1],pinfo->worldy[2]);
	float3 worldz = (float3)(pinfo->worldz[0],pinfo->worldz[1],pinfo->worldz[2]);

	int tri_num = pinfo->tri_num;
	int rand_normal_num = pinfo->rand_normal_num;
	float ambient = pinfo->ambient; // ambient lighting
	float exposuref = pinfo->exposure; // exposure factor
	float aorange = pinfo->aorange; // testing range for ao lighting
	float clipnodes_num = pinfo->clipnodes_num; // hull clipnodes number
	float planes_num = pinfo->planes_num; // hull planes number



#define RAYLEN 1500
#define randomNUsed 16
	int r = 0, g = 0, b = 0, a = 255;
	float3 rdv = (float3)(x, screenaspect*y, gNearClip); // view space
	rdv = normalize( rdv );
	float3 dir = (float3)( dot(rdv,worldx), dot(rdv,worldy), dot(rdv,worldz) ) * RAYLEN; // raydir in world space

	unsigned pixel = 0;
	uchar* prgb = (uchar*)&pixel;
	
	//trace_tt trace = { 1, 0, 0, tri_num, tri };
	//trace_world( &trace, org, dir);
	trace_t trace = { true, false, false, false, 1};
	world_t world = { pinfo, clipnodes, planes, 0 };

	vec3_t v_end = { org.x + dir.x, org.y + dir.y, org.z + dir.z };
	// trace a line through the apropriate clipping hull
	RecursiveHullCheck( &world, 0, 0, 1, pinfo->vieworg, v_end, &trace );


	// ao lighting
	if (aorange > 0) {
		unsigned int seed = (unsigned int)(idx);

		// ray hit the world
		float3 interpoint = org + dir*trace.fraction; // intersect point
		vec3_t v_interpoint = { interpoint.x, interpoint.y, interpoint.z };

		//float3 edge1 = trace.v1 - trace.v0;
		//float3 edge2 = trace.v2 - trace.v0;
		//float3 planen = cross( edge1, edge2 ); // normal of hit plane

		float3 planen = (float3)(trace.plane.normal[0], trace.plane.normal[1], trace.plane.normal[2]);

		float3 v0 = planen * trace.plane.dist;
		float3 v1 = v0 + (float3)(randomf(&seed),randomf(&seed),randomf(&seed))*99;
		float frac = trace.plane.dist / dot( v1, planen );
		v1*=frac; // a point on plane
		float3 edge1 = v1 - v0;


		float3 edge2 = cross( planen, edge1 );

		// local coord of intersect point ( edge1, edge2, planen )
		edge1 = normalize( edge1 );
		edge2 = normalize( edge2 );
		planen = normalize( planen );

		// matrix to world space
		worldx = (float3)(edge1.x, edge2.x, planen.x);
		worldy = (float3)(edge1.y, edge2.y, planen.y);
		worldz = (float3)(edge1.z, edge2.z, planen.z);


		float sum = 0;
		float3 skylight = { 0,0,0 };

		for (int r = 0; r < randomNUsed; r++) {

			rdv.x = randomf(&seed) - 0.5f;
			rdv.y = randomf(&seed) - 0.5f;
			rdv.z = sqrt(1.0f - rdv.x*rdv.x - rdv.y*rdv.y);

			float3 aodir = (float3)( dot(rdv,worldx), dot(rdv,worldy), dot(rdv,worldz) ) * aorange; // ao raydir in world space
			
			//trace_tt tt = { 1, 0, 0, tri_num, tri };
			//trace_world( &tt, interpoint, aodir );
			trace_t tt = { true, false, false, false, 1};
			world.d = 0;
			vec3_t v_aoend = { interpoint.x + aodir.x, interpoint.y + aodir.y, interpoint.z + aodir.z };
			// trace a line through the apropriate clipping hull
			RecursiveHullCheck( &world, 0, 0, 1, v_interpoint, v_aoend, &tt );

			/*
			if (CONTENTS_WATER == tt.content || CONTENTS_SLIME == tt.content || CONTENTS_LAVA == tt.content || CONTENTS_SKY == tt.content) {
				skylight.x += 0.91f * contentcolor[-tt.content][0];
				skylight.y += 0.91f * contentcolor[-tt.content][1];
				skylight.z += 0.91f * contentcolor[-tt.content][2];
			}
			*/
			sum += tt.fraction;
		}

		float exposure = exposuref / randomNUsed;
		r = g = b = Expose(sum, exposure) * 255 + ambient;
//		skylight.x = Expose(skylight.x, exposure);
//		skylight.y = Expose(skylight.y, exposure);
//		skylight.z = Expose(skylight.z, exposure);
//		r += skylight.x * 255;
//		g += skylight.y * 255;
//		b += skylight.z * 255;
	}
	
	prgb[0] = r;
	prgb[1] = g;//t * 255;
	prgb[2] = b;//t * 255;
	prgb[3] = 255;

	pic[idx] = pixel;
}