// d3d_rmain.c

#include "quakedef.h"
#include "d3d_help.h"
//=============================================
extern LPDIRECTDRAWSURFACE7 g_pddsBackBuffer;
extern LPDIRECT3DDEVICE7    g_pd3dDevice;
extern D3DVIEWPORT7 g_vp;
BOOL bIsTorches; // my add

#define GLdouble double //syq 2000.11.14

entity_t	r_worldentity;

qboolean	r_cache_thrash;		// compatability

vec3_t		modelorg, r_entorigin;
entity_t	*currententity;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];

int			c_brush_polys, c_alias_polys;

qboolean	envmap;				// true during envmap command capture 

int			currenttexture = -1;		// to avoid unnecessary texture sets

int			cnttextures[2] = {-1, -1};     // cached

int			particletexture;	// little dot for particles
int			playertextures;		// up to 16 color translated skins

int			mirrortexturenum;	// quake texturenum, not gltexturenum
qboolean	mirror;
mplane_t	*mirror_plane;

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float	r_world_matrix[16];
float	r_base_world_matrix[16];

//
// screen size info
//
refdef_t	r_refdef;

mleaf_t		*r_viewleaf, *r_oldviewleaf;

texture_t	*r_notexture_mip;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


void R_MarkLeaves (void);

cvar_t	r_norefresh = {"r_norefresh","0"};
cvar_t	r_drawentities = {"r_drawentities","1"};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1"};
cvar_t	r_speeds = {"r_speeds","0"};
cvar_t	r_fullbright = {"r_fullbright","0"};
cvar_t	r_lightmap = {"r_lightmap","0"};
cvar_t	r_shadows = {"r_shadows","0"};
cvar_t	r_mirroralpha = {"r_mirroralpha","1"};
cvar_t	r_wateralpha = {"r_wateralpha","1"};
cvar_t	r_dynamic = {"r_dynamic","1"};
cvar_t	r_novis = {"r_novis","0"};

//syq 2000.1.12
cvar_t  r_showfps = {"r_showfps","0"};
cvar_t	r_lightmapmode = {"r_lightmapmode","0"};

cvar_t	gl_finish = {"gl_finish","0"};
cvar_t	gl_clear = {"gl_clear","0"};
cvar_t	gl_cull = {"gl_cull","1"};
cvar_t	gl_texsort = {"gl_texsort","1"};
cvar_t	gl_smoothmodels = {"gl_smoothmodels","1"};
cvar_t	gl_affinemodels = {"gl_affinemodels","0"};
cvar_t	gl_polyblend = {"gl_polyblend","1"};
cvar_t	gl_flashblend = {"gl_flashblend","1"};
cvar_t	gl_playermip = {"gl_playermip","0"};
cvar_t	gl_nocolors = {"gl_nocolors","0"};
cvar_t	gl_keeptjunctions = {"gl_keeptjunctions","0"};
cvar_t	gl_reporttjunctions = {"gl_reporttjunctions","0"};
cvar_t	gl_doubleeyes = {"gl_doubleeys", "1"};

extern	cvar_t	gl_ztrick;

/*
=================
R_CullBo

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int		i;

	for (i=0 ; i<4 ; i++)
		if (BoxOnPlaneSide (mins, maxs, &frustum[i]) == 2)
			return true;
	return false;
}


void R_RotateForEntity (entity_t *e)
{
	D3DMATRIX mat;
	IDirect3DDevice7_GetTransform(g_pd3dDevice,D3DTRANSFORMSTATE_WORLD,&mat);

	D3DUtil_TranslateMatrix(&mat,e->origin[0],  e->origin[1],  e->origin[2]);
	D3DUtil_RotateMatrix(&mat,AngleToRadian(-e->angles[1]),  0, 0, 1);
	D3DUtil_RotateMatrix(&mat,AngleToRadian(e->angles[0]),  0, 1, 0);
	D3DUtil_RotateMatrix(&mat,AngleToRadian(e->angles[2]),  1, 0, 0);

	IDirect3DDevice7_SetTransform(g_pd3dDevice,D3DTRANSFORMSTATE_WORLD,&mat);
	
/*
    glTranslatef (e->origin[0],  e->origin[1],  e->origin[2]);
    glRotatef (e->angles[1],  0, 0, 1);
    glRotatef (-e->angles[0],  0, 1, 0);
    glRotatef (e->angles[2],  1, 0, 0);
	*/
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame (entity_t *currententity)
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int				i, numframes, frame;
	float			*pintervals, fullinterval, targettime, time;

	psprite = (msprite_t*)currententity->model->cache.data;
	frame = currententity->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_Printf ("R_DrawSprite: no such frame %d\n", frame);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		time = cl.time + currententity->syncbase;

	// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
	// are positive, so we don't have to worry about division by 0
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		for (i=0 ; i<(numframes-1) ; i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}


/*
=================
R_DrawSpriteModel

=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	vec3_t	point;
	mspriteframe_t	*frame;
	float		*up, *right;
	vec3_t		v_forward, v_right, v_up;
	msprite_t		*psprite;

	D3DLVERTEX lver[4];

	// don't even bother culling, because it's just a single
	// polygon without a surface cache
	frame = R_GetSpriteFrame (e);
	psprite = (msprite_t*)currententity->model->cache.data;

	if (psprite->type == SPR_ORIENTED)
	{	// bullet marks on walls
		AngleVectors (currententity->angles, v_forward, v_right, v_up);
		up = v_up;
		right = v_right;
	}
	else
	{	// normal sprite
		up = vup;
		right = vright;
	}

	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->left, right, point);
	lver[0].x=point[0]; lver[0].y=point[1]; lver[0].z=point[2];
	lver[0].color=D3DRGB(1.0f,1.0f,1.0f);
	lver[0].tu=0; lver[0].tv=1;

	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->left, right, point);
	lver[1].x=point[0]; lver[1].y=point[1]; lver[1].z=point[2];
	lver[1].color=D3DRGB(1.0f,1.0f,1.0f);
	lver[1].tu=0; lver[1].tv=0;

	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->right, right, point);
	lver[3].x=point[0]; lver[3].y=point[1]; lver[3].z=point[2];
	lver[3].color=D3DRGB(1.0f,1.0f,1.0f);
	lver[3].tu=1; lver[3].tv=0;

	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->right, right, point);
	lver[2].x=point[0]; lver[2].y=point[1]; lver[2].z=point[2];
	lver[2].color=D3DRGB(1.0f,1.0f,1.0f);
	lver[2].tu=1; lver[2].tv=1;

	D3D_DisableMultitexture();
	D3D_Bind(frame->gl_texturenum);

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_SRCBLEND,   D3DBLEND_SRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_DESTBLEND,   D3DBLEND_ONE  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   TRUE );
//	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHATESTENABLE,   TRUE );

	IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,D3DPT_TRIANGLESTRIP,D3DFVF_LVERTEX,lver,4,NULL);

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   FALSE );
//	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHATESTENABLE,   FALSE );
/*
	glColor3f (1,1,1);

	GL_DisableMultitexture();

    GL_Bind(frame->gl_texturenum);

	glEnable (GL_ALPHA_TEST);
	glBegin (GL_QUADS);

	glTexCoord2f (0, 1);
	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->left, right, point);
	glVertex3fv (point);

	glTexCoord2f (0, 0);
	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->left, right, point);
	glVertex3fv (point);

	glTexCoord2f (1, 0);
	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->right, right, point);
	glVertex3fv (point);

	glTexCoord2f (1, 1);
	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->right, right, point);
	glVertex3fv (point);
	
	glEnd ();

	glDisable (GL_ALPHA_TEST);
	*/
}

/*
=============================================================

  ALIAS MODELS

=============================================================
*/


#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

vec3_t	shadevector;
float	shadelight, ambientlight;

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
;

float	*shadedots = r_avertexnormal_dots[0];

int	lastposenum;

/*
=============
GL_DrawAliasFrame
=============
*/
void D3D_DrawAliasFrame (aliashdr_t *paliashdr, int posenum)
{
	float	s, t;
	float 	l;
	int		i, j;
	int		index;
	trivertx_t	*v, *verts;
	int		list;
	int		*order;
	vec3_t	point;
	float	*normal;
	int		count;

	D3DLVERTEX* plver;
	int nVerIndex;

lastposenum = posenum;

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	while (1)
	{
		D3DPRIMITIVETYPE enumPrim;
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done
		if (count < 0)
		{
			count = -count;
//			glBegin (GL_TRIANGLE_FAN);
			enumPrim=D3DPT_TRIANGLEFAN;
		}
		else
			enumPrim=D3DPT_TRIANGLESTRIP;
//			glBegin (GL_TRIANGLE_STRIP);

		//plver=malloc(sizeof(D3DLVERTEX)*count);
		plver=Z_Malloc (sizeof(D3DLVERTEX)*count);
		if (NULL==plver) break;
		nVerIndex=0;

		do
		{
			// texture coordinates come from the draw list
			//glTexCoord2f (((float *)order)[0], ((float *)order)[1]);
			plver[nVerIndex].tu=((float *)order)[0];
			plver[nVerIndex].tv=((float *)order)[1];
			
			order += 2;

			// normals and vertexes come from the frame list
			l = shadedots[verts->lightnormalindex] * shadelight;
			if (bIsTorches) l=1; // my add
			//glColor3f (l, l, l);
			//glVertex3f (verts->v[0], verts->v[1], verts->v[2]);
			plver[nVerIndex].x=verts->v[0];
			plver[nVerIndex].y=verts->v[1];
			plver[nVerIndex].z=verts->v[2];
			plver[nVerIndex].color=D3DRGB(l,l,l);
			verts++;
			nVerIndex++;
		} while (--count);
//		glEnd ();
		IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,enumPrim,D3DFVF_LVERTEX,plver,nVerIndex,NULL);
		//free(plver);
		Z_Free(plver);
	}
}


/*
=============
D3D_DrawAliasShadow
=============
*/
extern	vec3_t			lightspot;

void D3D_DrawAliasShadow (aliashdr_t *paliashdr, int posenum)
{
	float	s, t, l;
	int		i, j;
	int		index;
	trivertx_t	*v, *verts;
	int		list;
	int		*order;
	vec3_t	point;
	float	*normal;
	float	height, lheight;
	int		count;

	D3DLVERTEX* plver;
	int nVerIndex;

	lheight = currententity->origin[2] - lightspot[2];

	height = 0;
	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	height = -lheight + 1.0;

	while (1)
	{
		D3DPRIMITIVETYPE enumPrim;

		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done
		if (count < 0)
		{
			count = -count;
			//glBegin (GL_TRIANGLE_FAN);
			enumPrim=D3DPT_TRIANGLEFAN;
		}
		else
			//glBegin (GL_TRIANGLE_STRIP);
			enumPrim=D3DPT_TRIANGLESTRIP;

//		plver=malloc(sizeof(D3DLVERTEX)*count);
		plver=Z_Malloc(sizeof(D3DLVERTEX)*count);
		if (NULL==plver) break;
		nVerIndex=0;

		do
		{
			// texture coordinates come from the draw list
			// (skipped for shadows) glTexCoord2fv ((float *)order);
			order += 2;

			// normals and vertexes come from the frame list
			point[0] = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
			point[1] = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
			point[2] = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

			point[0] -= shadevector[0]*(point[2]+lheight);
			point[1] -= shadevector[1]*(point[2]+lheight);
			point[2] = height;
//			height -= 0.001;
//			glVertex3fv (point);
			plver[nVerIndex].x=point[0]; 
			plver[nVerIndex].y=point[1];
			plver[nVerIndex].z=point[2];
			plver[nVerIndex].color=D3DRGBA(0.0f,0.0f,0.0f,0.5f);
			nVerIndex++;

			verts++;
		} while (--count);

//		glEnd ();
		IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,enumPrim,D3DFVF_LVERTEX,plver,nVerIndex,NULL);
		
		//free(plver);
		Z_Free(plver);

	}	
	
}



/*
=================
R_SetupAliasFrame

=================
*/
void R_SetupAliasFrame (int frame, aliashdr_t *paliashdr)
{
	int				pose, numposes;
	float			interval;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d\n", frame);
		frame = 0;
	}

	pose = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		interval = paliashdr->frames[frame].interval;
		pose += (int)(cl.time / interval) % numposes;
	}

	D3D_DrawAliasFrame (paliashdr, pose);
}



/*
=================
R_DrawAliasModel

=================
*/
int R_LightPoint (vec3_t p);
void R_DrawAliasModel (entity_t *e)
{
	int			i, j;
	int			lnum;
	vec3_t		dist;
	float		add;
	model_t		*clmodel;
	vec3_t		mins, maxs;
	aliashdr_t	*paliashdr;
	trivertx_t	*verts, *v;
	int			index;
	float		s, t, an;
	int			anim;

	clmodel = currententity->model;

	VectorAdd (currententity->origin, clmodel->mins, mins);
	VectorAdd (currententity->origin, clmodel->maxs, maxs);

	if (R_CullBox (mins, maxs))
		return;


	VectorCopy (currententity->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	//
	// get lighting information
	//

	ambientlight = shadelight = R_LightPoint (currententity->origin);

	// allways give the gun some light
	if (e == &cl.viewent && ambientlight < 24)
		ambientlight = shadelight = 24;

	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		if (cl_dlights[lnum].die >= cl.time)
		{
			VectorSubtract (currententity->origin,
							cl_dlights[lnum].origin,
							dist);
			add = cl_dlights[lnum].radius - Length(dist);

			if (add > 0) {
				ambientlight += add;
				//ZOID models should be affected by dlights as well
				shadelight += add;
			}
		}
	}

	// clamp lighting so it doesn't overbright as much
	if (ambientlight > 128)
		ambientlight = 128;
	if (ambientlight + shadelight > 192)
		shadelight = 192 - ambientlight;

	// ZOID: never allow players to go totally black
	i = currententity - cl_entities;
	if (i >= 1 && i<=cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
		if (ambientlight < 8)
			ambientlight = shadelight = 8;

	// HACK HACK HACK -- no fullbright colors, so make torches full light
	if (!strcmp (clmodel->name, "progs/flame2.mdl")
		|| !strcmp (clmodel->name, "progs/flame.mdl") ) 
	{
		ambientlight = shadelight = 256;
		bIsTorches=TRUE; // my add
	}else {
		bIsTorches=FALSE; // my add
	}

	shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
	shadelight = shadelight / 200.0;
	
	an = e->angles[1]/180*M_PI;
	shadevector[0] = cos(-an);
	shadevector[1] = sin(-an);
	shadevector[2] = 1;
	VectorNormalize (shadevector);

	//
	// locate the proper data
	//
	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);

	c_alias_polys += paliashdr->numtris;

	//
	// draw all the triangles
	//

	D3D_DisableMultitexture();

    //glPushMatrix ();
	D3DUtil_SaveMatrix(g_pd3dDevice);
	R_RotateForEntity (e);

	if (!strcmp (clmodel->name, "progs/eyes.mdl") && gl_doubleeyes.value) {
		//glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2] - (22 + 8));
		// double size of eyes, since they are really hard to see in gl
		//glScalef (paliashdr->scale[0]*2, paliashdr->scale[1]*2, paliashdr->scale[2]*2);
		D3DMATRIX mat;
		IDirect3DDevice7_GetTransform(g_pd3dDevice,D3DTRANSFORMSTATE_WORLD,&mat);
		D3DUtil_TranslateMatrix(&mat,paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2] - (22 + 8));
		D3DUtil_ScaleMatrix(&mat,paliashdr->scale[0]*2, paliashdr->scale[1]*2, paliashdr->scale[2]*2);
		IDirect3DDevice7_SetTransform(g_pd3dDevice,D3DTRANSFORMSTATE_WORLD,&mat);
	} else {
		//glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
		//glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);
		D3DMATRIX mat;
		IDirect3DDevice7_GetTransform(g_pd3dDevice,D3DTRANSFORMSTATE_WORLD,&mat);
		D3DUtil_TranslateMatrix(&mat,paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
		D3DUtil_ScaleMatrix(&mat,paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);
		IDirect3DDevice7_SetTransform(g_pd3dDevice,D3DTRANSFORMSTATE_WORLD,&mat);
	}

	anim = (int)(cl.time*10) & 3;
	D3D_Bind(paliashdr->gl_texturenum[currententity->skinnum][anim]);

	// we can't dynamically colormap textures, so they are cached
	// seperately for the players.  Heads are just uncolored.
	if (currententity->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = currententity - cl_entities;
		if (i >= 1 && i<=cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
		    D3D_Bind(playertextures - 1 + i);
	}

	//if (gl_smoothmodels.value)
	//	glShadeModel (GL_SMOOTH);
	//glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	IDirect3DDevice7_SetTextureStageState( g_pd3dDevice, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );


	//if (gl_affinemodels.value)
	//	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	
	R_SetupAliasFrame (currententity->frame, paliashdr);

	//glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	IDirect3DDevice7_SetTextureStageState( g_pd3dDevice, 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );


	//glShadeModel (GL_FLAT);
	//if (gl_affinemodels.value)
	//	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	//glPopMatrix ();
	D3DUtil_RestoreMatrix(g_pd3dDevice);

	if (r_shadows.value)
	{
		//glPushMatrix ();
		D3DUtil_SaveMatrix(g_pd3dDevice);
		R_RotateForEntity (e);
		//glDisable (GL_TEXTURE_2D);
		IDirect3DDevice7_SetTextureStageState( g_pd3dDevice, 0, D3DTSS_COLOROP,   D3DTOP_DISABLE );
		//glEnable (GL_BLEND);
//		IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_SRCBLEND,   D3DBLEND_SRCALPHA  );
//		IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_DESTBLEND,   D3DBLEND_INVSRCALPHA  );
		IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   TRUE );

		//glColor4f (0,0,0,0.5);
		D3D_DrawAliasShadow (paliashdr, lastposenum);
		//glEnable (GL_TEXTURE_2D);
		IDirect3DDevice7_SetTextureStageState( g_pd3dDevice, 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );
		//glDisable (GL_BLEND);
		IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   FALSE );
		//glColor4f (1,1,1,1);
		//glPopMatrix ();
		D3DUtil_RestoreMatrix(g_pd3dDevice);
	}

}

//==================================================================================

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawBrushModel (entity_t *e);
void R_DrawEntitiesOnList (void)
{
	int		i;

	if (!r_drawentities.value)
		return;

	// draw sprites seperately, because of alpha blending
	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		switch (currententity->model->type)
		{
		case mod_alias:
			R_DrawAliasModel (currententity);
			break;

		case mod_brush:
			R_DrawBrushModel (currententity);
			break;

		default:
			break;
		}
	}

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		switch (currententity->model->type)
		{
		case mod_sprite:
			R_DrawSpriteModel (currententity);
			break;
		}
	}
}

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel (void)
{
	float		ambient[4], diffuse[4];
	int			j;
	int			lnum;
	vec3_t		dist;
	float		add;
	dlight_t	*dl;
	int			ambientlight, shadelight;

	if (!r_drawviewmodel.value)
		return;

	if (chase_active.value)
		return;

	if (envmap)
		return;

	if (!r_drawentities.value)
		return;

	if (cl.items & IT_INVISIBILITY)
		return;

	if (cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;
	if (!currententity->model)
		return;

	j = R_LightPoint (currententity->origin);

	if (j < 24)
		j = 24;		// allways give some light on gun
	ambientlight = j;
	shadelight = j;

// add dynamic lights		
	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		dl = &cl_dlights[lnum];
		if (!dl->radius)
			continue;
		if (!dl->radius)
			continue;
		if (dl->die < cl.time)
			continue;

		VectorSubtract (currententity->origin, dl->origin, dist);
		add = dl->radius - Length(dist);
		if (add > 0)
			ambientlight += add;
	}

	ambient[0] = ambient[1] = ambient[2] = ambient[3] = (float)ambientlight / 128;
	diffuse[0] = diffuse[1] = diffuse[2] = diffuse[3] = (float)shadelight / 128;

	// hack the depth range to prevent view model from poking into walls
//	glDepthRange (gldepthmin, gldepthmin + 0.3*(gldepthmax-gldepthmin));
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        FALSE );
	R_DrawAliasModel (currententity);
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        TRUE );
//	glDepthRange (gldepthmin, gldepthmax);
	
}


/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
	D3DTLVERTEX v[4];
	D3DMATRIX mat;
	int x,y,w,h;
	LPDIRECTDRAWSURFACE7 pSurf;

	if (!gl_polyblend.value)
		return;
	if (!v_blend[3])
		return;

	D3D_DisableMultitexture();
/*
	glDisable (GL_ALPHA_TEST);
	glEnable (GL_BLEND);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_TEXTURE_2D);

    glLoadIdentity ();

    glRotatef (-90,  1, 0, 0);	    // put Z going up
    glRotatef (90,  0, 0, 1);	    // put Z going up

	glColor4fv (v_blend);

	glBegin (GL_QUADS);

	glVertex3f (10, 100, 100);
	glVertex3f (10, -100, 100);
	glVertex3f (10, -100, -100);
	glVertex3f (10, 100, -100);
	glEnd ();

	glDisable (GL_BLEND);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_ALPHA_TEST);
	*/

/*	D3DUtil_SetIdentityMatrix(&mat);
	D3DUtil_RotateMatrix(&mat,-g_PI_DIV_2,1,0,0);
	D3DUtil_RotateMatrix(&mat,g_PI_DIV_2,0,0,1);
	IDirect3DDevice7_SetTransform( g_pd3dDevice, D3DTRANSFORMSTATE_WORLD, &mat );
*/
	x=g_vp.dwX;
	y=g_vp.dwY;
	w=g_vp.dwWidth;
	h=g_vp.dwHeight;       

	v[0].sx=x; v[0].sy=y+h; v[0].sz=0.5f; v[0].rhw=0.5f; v[0].color=D3DRGBA(v_blend[0],v_blend[1],v_blend[2],v_blend[3]); 
	v[1].sx=x; v[1].sy=y; v[1].sz=0.5f; v[1].rhw=0.5f; v[1].color=D3DRGBA(v_blend[0],v_blend[1],v_blend[2],v_blend[3]); 
	v[2].sx=x+w; v[2].sy=y+h; v[2].sz=0.5f; v[2].rhw=0.5f; v[2].color=D3DRGBA(v_blend[0],v_blend[1],v_blend[2],v_blend[3]); 
	v[3].sx=x+w; v[3].sy=y; v[3].sz=0.5f; v[3].rhw=0.5f; v[3].color=D3DRGBA(v_blend[0],v_blend[1],v_blend[2],v_blend[3]); 

//	IDirect3DDevice7_SetTextureStageState( g_pd3dDevice, 0, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	IDirect3DDevice7_GetTexture( g_pd3dDevice, 0, &pSurf );
	IDirect3DDevice7_SetTexture( g_pd3dDevice, 0, NULL );


	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   TRUE );

    IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        FALSE );
	IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX ,v,4,NULL);
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        TRUE );

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   FALSE );
//	IDirect3DDevice7_SetTextureStageState( g_pd3dDevice, 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );
	IDirect3DDevice7_SetTexture( g_pd3dDevice, 0, pSurf );
}


int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}

void RotatePointAroundVector( vec3_t dst, const vec3_t dir, const vec3_t point, float degrees );
void R_SetFrustum (void)
{
	int		i;

	if (r_refdef.fov_x == 90) 
	{
		// front side is visible

		VectorAdd (vpn, vright, frustum[0].normal);
		VectorSubtract (vpn, vright, frustum[1].normal);

		VectorAdd (vpn, vup, frustum[2].normal);
		VectorSubtract (vpn, vup, frustum[3].normal);
	}
	else
	{
		// rotate VPN right by FOV_X/2 degrees
		RotatePointAroundVector( frustum[0].normal, vup, vpn, -(90-r_refdef.fov_x / 2 ) );
		// rotate VPN left by FOV_X/2 degrees
		RotatePointAroundVector( frustum[1].normal, vup, vpn, 90-r_refdef.fov_x / 2 );
		// rotate VPN up by FOV_X/2 degrees
		RotatePointAroundVector( frustum[2].normal, vright, vpn, 90-r_refdef.fov_y / 2 );
		// rotate VPN down by FOV_X/2 degrees
		RotatePointAroundVector( frustum[3].normal, vright, vpn, -( 90 - r_refdef.fov_y / 2 ) );
	}

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}



/*
===============
R_SetupFrame
===============
*/
void R_AnimateLight (void);
void V_CalcBlend (void);
void R_SetupFrame (void)
{
	int				edgecount;
	vrect_t			vrect;
	float			w, h;

// don't allow cheats in multiplayer
	if (cl.maxclients > 1)
		Cvar_Set ("r_fullbright", "0");

	R_AnimateLight ();

	r_framecount++;

// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);

	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	c_brush_polys = 0;
	c_alias_polys = 0;

}


void MYgluPerspective( GLdouble fovy, GLdouble aspect,
		     GLdouble zNear, GLdouble zFar )
{
   GLdouble xmin, xmax, ymin, ymax;

   ymax = zNear * tan( fovy * M_PI / 360.0 );
   ymin = -ymax;

   xmin = ymin * aspect;
   xmax = ymax * aspect;

//   glFrustum( xmin, xmax, ymin, ymax, zNear, zFar );
}


/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	float	screenaspect;
	float	yfov;
	int		i;
	extern	int glwidth, glheight;
	int		x, x2, y2, y, w, h;

	D3DVECTOR vFrom,vAt,vUp;
	D3DMATRIX matProj,matView,matWorld;

	vFrom.x=vFrom.y=vFrom.z=0;
//	vFrom.x=r_refdef.vieworg[0]; vFrom.y=r_refdef.vieworg[1]; vFrom.z=r_refdef.vieworg[2];
	vAt.x=vFrom.x; vAt.y=vFrom.y; vAt.z=vFrom.z+1;
	vUp.x=0; vUp.y=1; vUp.z=0;
	D3DUtil_SetViewMatrix( &matView, &vFrom, &vAt, &vUp );
	D3DUtil_SetIdentityMatrix(&matWorld);

	D3DUtil_ScaleMatrix(&matWorld,1,1,-1);

	D3DUtil_RotateMatrix(&matWorld,-g_PI_DIV_2+g_PI,1,0,0);
	D3DUtil_RotateMatrix(&matWorld,g_PI_DIV_2+g_PI,0,0,1);
	D3DUtil_RotateMatrix(&matWorld,-r_refdef.viewangles[2]/180*g_PI,1,0,0);
	D3DUtil_RotateMatrix(&matWorld,r_refdef.viewangles[0]/180*g_PI,0,1,0);
	D3DUtil_RotateMatrix(&matWorld,r_refdef.viewangles[1]/180*g_PI,0,0,1);
	D3DUtil_TranslateMatrix(&matWorld, -r_refdef.vieworg[0], -r_refdef.vieworg[1],  -r_refdef.vieworg[2]);

	IDirect3DDevice7_SetTransform( g_pd3dDevice, D3DTRANSFORMSTATE_VIEW, &matView );
	IDirect3DDevice7_SetTransform( g_pd3dDevice, D3DTRANSFORMSTATE_WORLD, &matWorld );

	IDirect3DDevice7_GetTransform( g_pd3dDevice, D3DTRANSFORMSTATE_WORLD, (LPD3DMATRIX)r_world_matrix );

	//
	// set up viewpoint
	//
//	glMatrixMode(GL_PROJECTION);
//  glLoadIdentity ();
	x = r_refdef.vrect.x * glwidth/vid.width;
	x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwidth/vid.width;
	//y = (vid.height-r_refdef.vrect.y) * glheight/vid.height;
	y=(r_refdef.vrect.y + r_refdef.vrect.height)* glwidth/vid.width;;
	//y2 = (vid.height - (r_refdef.vrect.y + r_refdef.vrect.height)) * glheight/vid.height;
	y2=r_refdef.vrect.y* glwidth/vid.width;;

	// fudge around because of frac screen scale
	if (x > 0)
		x--;
	if (x2 < glwidth)
		x2++;
	if (y2 < 0)
		y2--;
	if (y < glheight)
		y++;

	w = x2 - x;
	h = y - y2;

	if (envmap)
	{
		x = y2 = 0;
		w = h = 256;
	}

	//glViewport (glx + x, gly + y2, w, h);
	if (glx+x!=g_vp.dwX || 
		gly+y2!=g_vp.dwY ||
		w!=g_vp.dwWidth ||
		h!=g_vp.dwHeight) 
	{
		g_vp.dwX=glx+x;
		g_vp.dwY=gly+y2;
		g_vp.dwWidth=w;
		g_vp.dwHeight=h;       
		IDirect3DDevice7_SetViewport( g_pd3dDevice, &g_vp );
	}
	
    screenaspect = (float)r_refdef.vrect.width/r_refdef.vrect.height;
//	yfov = 2*atan((float)r_refdef.vrect.height/r_refdef.vrect.width)*180/M_PI;
    //MYgluPerspective (r_refdef.fov_y,  screenaspect,  4,  4096);
	D3DUtil_SetProjectionMatrix(&matProj,r_refdef.fov_y/180*g_PI,  1.0f/screenaspect, 4,  4096);
	IDirect3DDevice7_SetTransform( g_pd3dDevice, D3DTRANSFORMSTATE_PROJECTION, &matProj );

	if (mirror)
	{
//		if (mirror_plane->normal[2])
//			glScalef (1, -1, 1);
//		else
//			glScalef (-1, 1, 1);
//		glCullFace(GL_BACK);
		IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_CULLMODE,       D3DCULL_CW );
	}
	else
//		glCullFace(GL_FRONT);
		IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_CULLMODE,       D3DCULL_CCW );

/*	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity ();

    glRotatef (-90,  1, 0, 0);	    // put Z going up
    glRotatef (90,  0, 0, 1);	    // put Z going up
    glRotatef (-r_refdef.viewangles[2],  1, 0, 0);
    glRotatef (-r_refdef.viewangles[0],  0, 1, 0);
    glRotatef (-r_refdef.viewangles[1],  0, 0, 1);
    glTranslatef (-r_refdef.vieworg[0],  -r_refdef.vieworg[1],  -r_refdef.vieworg[2]);

	glGetFloatv (GL_MODELVIEW_MATRIX, r_world_matrix);

	//
	// set drawing parms
	//
*/	if (gl_cull.value)
;//		glEnable(GL_CULL_FACE);
	else
//		glDisable(GL_CULL_FACE);
		IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_CULLMODE,       D3DCULL_NONE );

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   FALSE );
//	glDisable(GL_BLEND);
//	glDisable(GL_ALPHA_TEST);
//	glEnable(GL_DEPTH_TEST);
}




void	R_SetGL2D(void)
{
	// set 2D virtual screen size
	//glViewport(0, 0, vid.width, vid.height);
	if (0 != g_vp.dwX ||
		0 != g_vp.dwY ||
		vid.width != g_vp.dwWidth ||
		vid.height != g_vp.dwHeight)
	{
		g_vp.dwX = 0;
		g_vp.dwY = 0;
		g_vp.dwWidth = vid.width;
		g_vp.dwHeight = vid.height;
		IDirect3DDevice7_SetViewport(g_pd3dDevice, &g_vp);
	}


//	glMatrixMode(GL_PROJECTION);
//	glLoadIdentity();

//	glOrtho(0, vid.width, vid.height, 0, -99999, 99999);
//	glMatrixMode(GL_MODELVIEW);
//	glLoadIdentity();

//	glDisable(GL_DEPTH_TEST);
//	glDisable(GL_CULL_FACE);
//	glDisable(GL_BLEND);
//	glEnable(GL_ALPHA_TEST);
//	glColor4f(1, 1, 1, 1);
}

/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
void R_DrawWorld (void);
void R_RenderDlights (void);
void R_DrawParticles (void);
void R_RenderScene (void)
{
	R_SetupFrame ();

	R_SetFrustum ();

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	R_DrawWorld ();		// adds static entities to the list


	// lighting world
	if (true) {

		void PathTracingFrame();

		void	R_SetGL2D();
		R_SetGL2D();
		PathTracingFrame();
		R_SetupGL();
	}

	S_ExtraUpdate ();	// don't let sound get messed up if going slow

	R_DrawEntitiesOnList ();

	D3D_DisableMultitexture();

//	R_RenderDlights ();

	R_DrawParticles ();

#ifdef GLTEST
	Test_Draw ();
#endif

}


/*
=============
R_Clear
=============
*/
void R_Clear (void)
{
	if (r_mirroralpha.value != 1.0)
	{
//		if (gl_clear.value)
//			glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//		else
//			glClear (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 0.5;
//		glDepthFunc (GL_LEQUAL);
//		IDirect3DDevice7_SetRenderState( g_pd3dDevice,D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL );
	}
	else if (gl_ztrick.value)
	{
		static int trickframe;

//		if (gl_clear.value)
//			glClear (GL_COLOR_BUFFER_BIT);

		trickframe++;
		if (trickframe & 1)
		{
			gldepthmin = 0;
			gldepthmax = 0.49999;
//			glDepthFunc (GL_LEQUAL);
//			IDirect3DDevice7_SetRenderState( g_pd3dDevice,D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL );
		}
		else
		{
			gldepthmin = 1;
			gldepthmax = 0.5;
//			glDepthFunc (GL_GEQUAL);
//			IDirect3DDevice7_SetRenderState( g_pd3dDevice,D3DRENDERSTATE_ZFUNC, D3DCMP_GREATEREQUAL );
		}
	}
	else
	{
//		if (gl_clear.value)
//			glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//		else
//			glClear (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 1;
//		glDepthFunc (GL_LEQUAL);
//		IDirect3DDevice7_SetRenderState( g_pd3dDevice,D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL );
	}

//	glDepthRange (gldepthmin, gldepthmax);
	
	IDirect3DDevice7_Clear(g_pd3dDevice,0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0x00,1.0f,0);
}

/*
=============
R_Mirror
=============
*/
void R_Mirror (void)
{
	float		d;
	msurface_t	*s;
	entity_t	*ent;

	if (!mirror)
		return;
/*
	memcpy (r_base_world_matrix, r_world_matrix, sizeof(r_base_world_matrix));

	d = DotProduct (r_refdef.vieworg, mirror_plane->normal) - mirror_plane->dist;
	VectorMA (r_refdef.vieworg, -2*d, mirror_plane->normal, r_refdef.vieworg);

		cl_visedicts[cl_numvisedicts] = ent;
		cl_numvisedicts++;
	}

	gldepthmin = 0.5;
	gldepthmax = 1;
	glDepthRange (gldepthmin, gldepthmax);
	glDepthFunc (GL_LEQUAL);

	R_RenderScene ();
	R_DrawWaterSurfaces ();

	gldepthmin = 0;
	d = DotProduct (vpn, mirror_plane->normal);
	VectorMA (vpn, -2*d, mirror_plane->normal, vpn);

	r_refdef.viewangles[0] = -asin (vpn[2])/M_PI*180;
	r_refdef.viewangles[1] = atan2 (vpn[1], vpn[0])/M_PI*180;
	r_refdef.viewangles[2] = -r_refdef.viewangles[2];

	ent = &cl_entities[cl.viewentity];
	if (cl_numvisedicts < MAX_VISEDICTS)
	{
	gldepthmax = 0.5;
	glDepthRange (gldepthmin, gldepthmax);
	glDepthFunc (GL_LEQUAL);

	// blend on top
	glEnable (GL_BLEND);
	glMatrixMode(GL_PROJECTION);
	if (mirror_plane->normal[2])
		glScalef (1,-1,1);
	else
		glScalef (-1,1,1);
	glCullFace(GL_FRONT);
	glMatrixMode(GL_MODELVIEW);

	glLoadMatrixf (r_base_world_matrix);

	glColor4f (1,1,1,r_mirroralpha.value);
	s = cl.worldmodel->textures[mirrortexturenum]->texturechain;
	for ( ; s ; s=s->texturechain)
		R_RenderBrushPoly (s);
	cl.worldmodel->textures[mirrortexturenum]->texturechain = NULL;
	glDisable (GL_BLEND);
	glColor4f (1,1,1,1);
	*/
}

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_DrawWaterSurfaces (void);
void R_RenderView (void)
{
	double	time1, time2;
//	GLfloat colors[4] = {(GLfloat) 0.0, (GLfloat) 0.0, (GLfloat) 1, (GLfloat) 0.20};

	if (r_norefresh.value)
		return;

	if (!r_worldentity.model || !cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	if (r_speeds.value)
	{
//		glFinish ();
		time1 = Sys_FloatTime ();
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	mirror = false;

//	if (gl_finish.value)
//		glFinish ();

	R_Clear ();

	// render normal view

/***** Experimental silly looking fog ******
****** Use r_fullbright if you enable ******
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogfv(GL_FOG_COLOR, colors);
	glFogf(GL_FOG_END, 512.0);
	glEnable(GL_FOG);
********************************************/

	R_RenderScene ();
	R_DrawViewModel ();
	R_DrawWaterSurfaces ();

//  More fog right here :)
//	glDisable(GL_FOG);
//  End of all fog code...

	// render mirror view
	R_Mirror ();

	//R_PolyBlend ();

	if (r_speeds.value)
	{
//		glFinish ();
		time2 = Sys_FloatTime ();
		Con_Printf ("%3i ms  %4i wpoly %4i epoly\n", (int)((time2-time1)*1000), c_brush_polys, c_alias_polys); 
	}
}
