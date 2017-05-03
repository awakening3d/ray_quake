#ifndef TRACING_H
#define TRACING_H


typedef struct areanode_s
{
	int		axis;		// -1 = leaf node
	float	dist;
	struct areanode_s	*children[2];
	link_t	trigger_edicts;
	link_t	solid_edicts;
} areanode_t;

#define	AREA_DEPTH	4
#define	AREA_NODES	32



typedef struct
{
	vec3_t		boxmins, boxmaxs;// enclose the test object along entire move
	float		*mins, *maxs;	// size of the moving object
	vec3_t		mins2, maxs2;	// size when clipping against mosnters
	float		*start, *end;
	trace_t		trace;
	int			type;
	edict_t		*passedict;
} moveclip_t;

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


#define V3T( v )	( *(vec3_t*)(const float*)v )

#define OUTPUT_LOG Con_Printf

#endif

