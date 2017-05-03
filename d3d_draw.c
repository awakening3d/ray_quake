// vid buffer

#include "quakedef.h"
#include "d3dtextr.h"

#include "resource.h"

extern unsigned char d_15to8table[65536];

cvar_t		gl_nobind = {"gl_nobind", "0"};
cvar_t		gl_max_size = {"gl_max_size", "1024"};
cvar_t		gl_picmip = {"gl_picmip", "0"};

byte		*draw_chars;				// 8*8 graphic characters
qpic_t		*draw_disc;
qpic_t		*draw_backtile;

int			translate_texture;
int			char_texture;

typedef struct
{
	int		texnum;
	float	sl, tl, sh, th;
} glpic_t;

byte		conback_buffer[sizeof(qpic_t) + sizeof(glpic_t)];
qpic_t		*conback = (qpic_t *)&conback_buffer;

//--------- my add --------------
#ifndef GL_RGBA
#define GL_RGBA 254
#endif


//int		gl_lightmap_format = 4;
int		gl_lightmap_format = GL_RGBA;
int		gl_solid_format = 3;
int		gl_alpha_format = 4;

int		d3d_filter_min = D3DTFN_LINEAR;
int		d3d_filter_max = D3DTFG_LINEAR;


int		texels;

typedef struct
{
	int		texnum;
	char	identifier[64];
	int		width, height;
	qboolean	mipmap;
	//LPDIRECTDRAWSURFACE7 lpSurface7;
} gltexture_t;

#define	MAX_GLTEXTURES	1024
gltexture_t	gltextures[MAX_GLTEXTURES];
int			numgltextures=0;

//=============================================
extern LPDIRECTDRAWSURFACE7 g_pddsBackBuffer;
extern LPDIRECT3DDEVICE7    g_pd3dDevice;
extern D3DVIEWPORT7 g_vp;

static DWORD oldtarget=0;

void D3D_Bind (int texnum)
{
	int i;

	if (gl_nobind.value)
		texnum = char_texture;
	if (currenttexture == texnum)
		return;
	currenttexture = texnum;

	for (i=0;i<numgltextures;i++) {
		if (texnum == gltextures[i].texnum) {
			LPDIRECTDRAWSURFACE7 pSurf;
			pSurf=D3DTextr_GetSurface(gltextures[i].identifier);
			IDirect3DDevice7_SetTexture(g_pd3dDevice,oldtarget,pSurf);
			return;
		}
	}

	IDirect3DDevice7_SetTexture(g_pd3dDevice,oldtarget,NULL);


/*
#ifdef _WIN32
	bindTexFunc (GL_TEXTURE_2D, texnum);
#else
	glBindTexture(GL_TEXTURE_2D, texnum);
#endif
*/	
}


/*
=============================================================================

  scrap allocation

  Allocate all the little status bar obejcts into a single texture
  to crutch up stupid hardware / drivers

=============================================================================
*/

#define	MAX_SCRAPS		2
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT*4];
qboolean	scrap_dirty;
int			scrap_texnum;

// returns a texture number and the position inside it
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		bestx;
	int		texnum;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i=0 ; i<BLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (scrap_allocated[texnum][i+j] >= best)
					break;
				if (scrap_allocated[texnum][i+j] > best2)
					best2 = scrap_allocated[texnum][i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	Sys_Error ("Scrap_AllocBlock: full");
}

int	scrap_uploads;
void D3D_ReLoad8(char *identifier,byte *data, int width, int height,  qboolean mipmap, qboolean alpha);
void Scrap_Upload (void)
{
	int		texnum;
	char name[100]="";

	scrap_uploads++;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++) {
		//D3D_Bind(scrap_texnum + texnum);
		//D3D_Upload8 (scrap_texels[texnum], BLOCK_WIDTH, BLOCK_HEIGHT, false, true);
		sprintf(name,"scrap%d",texnum);
		//D3D_LoadTexture(name, BLOCK_WIDTH, BLOCK_HEIGHT, scrap_texels[texnum],false, true);
		D3D_ReLoad8(name,scrap_texels[texnum], BLOCK_WIDTH, BLOCK_HEIGHT,  false, true);
	}

	scrap_dirty = false;
}

//=============================================================================
/* Support Routines */

typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic
} cachepic_t;

#define	MAX_CACHED_PICS		128
cachepic_t	menu_cachepics[MAX_CACHED_PICS];
int			menu_numcachepics;

byte		menuplyr_pixels[4096];

int		pic_texels;
int		pic_count;

int D3D_LoadPicTexture (qpic_t *pic);

qpic_t *Draw_PicFromWad (char *name)
{
	qpic_t	*p;
	glpic_t	*gl;

	p = (qpic_t*)W_GetLumpName (name);
	gl = (glpic_t *)p->data;

	// load little ones into the scrap
	if (p->width < 64 && p->height < 64)
	{
		int		x, y;
		int		i, j, k;
		int		texnum;

		texnum = Scrap_AllocBlock (p->width, p->height, &x, &y);
		scrap_dirty = true;
		k = 0;
		for (i=0 ; i<p->height ; i++)
			for (j=0 ; j<p->width ; j++, k++)
				scrap_texels[texnum][(y+i)*BLOCK_WIDTH + x + j] = p->data[k];
		texnum += scrap_texnum;
		gl->texnum = texnum;
		gl->sl = (x+0.01)/(float)BLOCK_WIDTH;
		gl->sh = (x+p->width-0.01)/(float)BLOCK_WIDTH;
		gl->tl = (y+0.01)/(float)BLOCK_WIDTH;
		gl->th = (y+p->height-0.01)/(float)BLOCK_WIDTH;

		pic_count++;
		pic_texels += p->width*p->height;
	}
	else
	{
		gl->texnum = D3D_LoadPicTexture (p);
		gl->sl = 0;
		gl->sh = 1;
		gl->tl = 0;
		gl->th = 1;
	}
	return p;
}


/*
================
Draw_CachePic
================
*/
qpic_t	*Draw_CachePic (char *path)
{
	cachepic_t	*pic;
	int			i;
	qpic_t		*dat;
	glpic_t		*gl;

	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
		if (!strcmp (path, pic->name))
			return &pic->pic;

	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");
	menu_numcachepics++;
	strcpy (pic->name, path);

//
// load the pic from disk
//
	dat = (qpic_t *)COM_LoadTempFile (path);	
	if (!dat)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	SwapPic (dat);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width*dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl = (glpic_t *)pic->pic.data;
	gl->texnum = D3D_LoadPicTexture (dat);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;

	return &pic->pic;
}


void Draw_CharToConback (int num, byte *dest)
{
	int		row, col;
	byte	*source;
	int		drawline;
	int		x;

	row = num>>4;
	col = num&15;
	source = draw_chars + (row<<10) + (col<<3);

	drawline = 8;

	while (drawline--)
	{
		for (x=0 ; x<8 ; x++)
			if (source[x] != 255)
				dest[x] = 0x60 + source[x];
		source += 128;
		dest += 320;
	}

}

typedef struct
{
	char *name;
	int	minimize, maximize;
} glmode_t;

/*
glmode_t modes[] = {
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
	
};
*/
glmode_t modes[] = {
	{"GL_NEAREST", 0, 0},
	{"GL_LINEAR", 0, 0},
	{"GL_NEAREST_MIPMAP_NEAREST", 0, 0},
	{"GL_LINEAR_MIPMAP_NEAREST", 0, 0},
	{"GL_NEAREST_MIPMAP_LINEAR", 0, 0},
	{"GL_LINEAR_MIPMAP_LINEAR", 0, 0}
	
};
/*
===============
Draw_TextureMode_f
===============
*/
void Draw_TextureMode_f (void)
{
	int		i;
	gltexture_t	*glt;

	if (Cmd_Argc() == 1)
	{
		for (i=0 ; i< 6 ; i++)
			if (d3d_filter_min == modes[i].minimize)
			{
				Con_Printf ("%s\n", modes[i].name);
				return;
			}
		Con_Printf ("current filter is unknown???\n");
		return;
	}

	for (i=0 ; i< 6 ; i++)
	{
		if (!Q_strcasecmp (modes[i].name, Cmd_Argv(1) ) )
			break;
	}
	if (i == 6)
	{
		Con_Printf ("bad filter name\n");
		return;
	}

	d3d_filter_min = modes[i].minimize;
	d3d_filter_max = modes[i].maximize;

	// change all the existing mipmap texture objects
	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
	{
		if (glt->mipmap)
		{
/*			D3D_Bind (glt->texnum);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
*/		}
	}
}

/*
===============
Draw_Init
===============
*/
void Draw_Init (void)
{
	int		i;
	qpic_t	*cb;
	byte	*dest, *src;
	int		x, y;
	char	ver[40];
	glpic_t	*gl;
	int		start;
	byte	*ncdata;
	int		f, fstep;
	char name[100];
	extern HINSTANCE	global_hInstance;
	LPDIRECTDRAWSURFACE7 pSurf;

	Cvar_RegisterVariable (&gl_nobind);
	Cvar_RegisterVariable (&gl_max_size);
	Cvar_RegisterVariable (&gl_picmip);

	// 3dfx can only handle 256 wide textures
	if (!Q_strncasecmp ((char *)d3d_renderer, "3dfx",4) ||
		strstr((char *)d3d_renderer, "Glide"))
		Cvar_Set ("gl_max_size", "256");

	Cmd_AddCommand ("gl_texturemode", &Draw_TextureMode_f);

	// load the console background and the charset
	// by hand, because we need to write the version
	// string into the background before turning
	// it into a texture
	draw_chars = (byte*)W_GetLumpName ("conchars");
	for (i=0 ; i<256*64 ; i++) {
		if (draw_chars[i] == 0)
			draw_chars[i] = 255;	// proper transparent color
	}

	// now turn them into textures
	char_texture = D3D_LoadTexture ("charset", 128, 128, draw_chars, false, true);

	start = Hunk_LowMark();

	cb = (qpic_t *)COM_LoadTempFile ("gfx/conback.lmp");	
	if (!cb)
		Sys_Error ("Couldn't load gfx/conback.lmp");
	SwapPic (cb);

	// hack the version number directly into the pic
#if defined(__linux__)
	sprintf (ver, "(Linux %2.2f, gl %4.2f) %4.2f", (float)LINUX_VERSION, (float)GLQUAKE_VERSION, (float)VERSION);
#else
	sprintf (ver, "(gl %4.2f) %4.2f", (float)GLQUAKE_VERSION, (float)VERSION);
#endif
	dest = cb->data + 320*186 + 320 - 11 - 8*strlen(ver);
	y = strlen(ver);
	for (x=0 ; x<y ; x++)
		Draw_CharToConback (ver[x], dest+(x<<3));

#if 0
	conback->width = vid.conwidth;
	conback->height = vid.conheight;

 	// scale console to vid size
 	dest = ncdata = Hunk_AllocName(vid.conwidth * vid.conheight, "conback");
 
 	for (y=0 ; y<vid.conheight ; y++, dest += vid.conwidth)
 	{
 		src = cb->data + cb->width * (y*cb->height/vid.conheight);
 		if (vid.conwidth == cb->width)
 			memcpy (dest, src, vid.conwidth);
 		else
 		{
 			f = 0;
 			fstep = cb->width*0x10000/vid.conwidth;
 			for (x=0 ; x<vid.conwidth ; x+=4)
 			{
 				dest[x] = src[f>>16];
 				f += fstep;
 				dest[x+1] = src[f>>16];
 				f += fstep;
 				dest[x+2] = src[f>>16];
 				f += fstep;
 				dest[x+3] = src[f>>16];
 				f += fstep;
 			}
 		}
 	}
#else
	conback->width = cb->width;
	conback->height = cb->height;
	ncdata = cb->data;
#endif
/*
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
*/
	gl = (glpic_t *)conback->data;
	gl->texnum = D3D_LoadTexture ("conback", conback->width, conback->height, ncdata, false, false);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;
	conback->width = vid.width;
	conback->height = vid.height;

	//----- syq 2001.1.9 --- <  //改变控制台背景图片
	pSurf=D3DTextr_GetSurface("conback"); //取控制台背景纹理表面
	if (pSurf) {
		HDC hdc,hmemdc=NULL;
		HBITMAP hConBmp,hOldBmp;
		BITMAP stBmp;
		DDSURFACEDESC2 desc;
		if (DD_OK==IDirectDrawSurface7_GetDC(pSurf,&hdc)) {
			hmemdc=CreateCompatibleDC(hdc); //建立一个临时dc
			if (hmemdc) {
				hConBmp=LoadBitmap(global_hInstance,MAKEINTRESOURCE(IDB_D3DQUAKE)); //从资源中装入bmp
				if (hConBmp) {
					GetObject(hConBmp,sizeof(BITMAP),&stBmp); //取bmp高宽

					//取表面高宽
					memset(&desc,0,sizeof(desc));
					desc.dwSize=sizeof(desc);
					desc.ddpfPixelFormat.dwSize=sizeof( DDPIXELFORMAT);
					IDirectDrawSurface7_GetSurfaceDesc(pSurf,&desc);

					//拷贝图片数据到表面
					hOldBmp=(HBITMAP)SelectObject(hmemdc,hConBmp);
					BitBlt(hdc,desc.dwWidth-stBmp.bmWidth-80,desc.dwHeight-stBmp.bmHeight-5,stBmp.bmWidth,stBmp.bmHeight,hmemdc,0,0,SRCCOPY);
					SelectObject(hmemdc,hOldBmp);
					DeleteObject(hConBmp);
				}
				DeleteDC(hmemdc);
			}
			IDirectDrawSurface7_ReleaseDC(pSurf,hdc);
		}
	}

	//----- syq 2001.1.9 --- >

	// free loaded console
	Hunk_FreeToLowMark(start);

	// save a texture slot for translated picture
	//translate_texture = texture_extension_number++;

	// save slots for scraps
	scrap_texnum = texture_extension_number;
	//texture_extension_number += MAX_SCRAPS;
	for (i=0 ; i<MAX_SCRAPS ; i++) {
		sprintf(name,"scrap%d",i);
		scrap_texels[i][0]=scrap_texels[i][1]=scrap_texels[i][2]=scrap_texels[i][3]=255;
		D3D_LoadTexture(name, BLOCK_WIDTH, BLOCK_HEIGHT, scrap_texels[i],false, true);
	}

	//
	// get the other pics we need
	//
	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
}



/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Character (int x, int y, int num)
{
	byte			*dest;
	byte			*source;
	unsigned short	*pusdest;
	int				drawline;	
	int				row, col;
	float			frow, fcol, size;
	HDC				hdc=NULL;
	D3DTLVERTEX v[4];

	if (num == 32)
		return;		// space

	num &= 255;
	
	if (y <= -8)
		return;			// totally off screen

	row = num>>4;
	col = num&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	D3D_Bind (char_texture);

	frow+=0.005;
	fcol+=0.006;

	v[0].sx=x; v[0].sy=y+8; v[0].sz=0.5f; v[0].rhw=0.5f; v[0].color=0xffffffff; v[0].tu=fcol; v[0].tv=frow+size;
	v[1].sx=x; v[1].sy=y; v[1].sz=0.5f; v[1].rhw=0.5f; v[1].color=0xffffffff; v[1].tu=fcol; v[1].tv=frow;
	v[2].sx=x+8; v[2].sy=y+8; v[2].sz=0.5f; v[2].rhw=0.5f; v[2].color=0xffffffff; v[2].tu=fcol+size; v[2].tv=frow+size;
	v[3].sx=x+8; v[3].sy=y; v[3].sz=0.5f; v[3].rhw=0.5f; v[3].color=0xffffffff; v[3].tu=fcol+size; v[3].tv=frow;

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_SRCBLEND,   D3DBLEND_SRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_DESTBLEND,   D3DBLEND_INVSRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   TRUE );

    IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        FALSE );
	IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX ,v,4,NULL);
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        TRUE );

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   FALSE );

/*
	glBegin (GL_QUADS);
	glTexCoord2f (fcol, frow);
	glVertex2f (x, y);
	glTexCoord2f (fcol + size, frow);
	glVertex2f (x+8, y);
	glTexCoord2f (fcol + size, frow + size);
	glVertex2f (x+8, y+8);
	glTexCoord2f (fcol, frow + size);
	glVertex2f (x, y+8);
	glEnd ();
*/

/*	IDirectDrawSurface7_GetDC(g_pddsBackBuffer,&hdc);
	if (hdc) {
		SetBkMode(hdc,TRANSPARENT);
		SetTextColor(hdc,RGB(50,200,50));
		TextOut(hdc,x,y,&num,1);
		IDirectDrawSurface7_ReleaseDC(g_pddsBackBuffer,hdc);
	}
	*/
}

/*
================
Draw_String
================
*/
void Draw_String (int x, int y, char *str)
{
	while (*str)
	{
		Draw_Character (x, y, *str);
		str++;
		x += 8;
	}

/*	HDC				hdc=NULL;
	IDirectDrawSurface7_GetDC(g_pddsBackBuffer,&hdc);
	if (hdc) {
		SetBkMode(hdc,TRANSPARENT);
		SetTextColor(hdc,RGB(50,200,50));
		TextOut(hdc,x,y,str,strlen(str));
		IDirectDrawSurface7_ReleaseDC(g_pddsBackBuffer,hdc);
	}
	*/
}

/*
================
Draw_DebugChar

Draws a single character directly to the upper right corner of the screen.
This is for debugging lockups by drawing different chars in different parts
of the code.
================
*/
void Draw_DebugChar (char num)
{
}

/*
=============
Draw_AlphaPic
=============
*/
void Draw_AlphaPic (int x, int y, qpic_t *pic, float alpha)
{
	byte			*dest, *source;
	unsigned short	*pusdest;
//	int				v, u;
	glpic_t			*gl;

	D3DTLVERTEX v[4];

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;

	v[0].sx=x; v[0].sy=y+pic->height; v[0].sz=0.6f; v[0].rhw=0.5f; v[0].color=D3DRGBA(1.0f,1.0f,1.0f,alpha); v[0].tu=gl->sl; v[0].tv=gl->th;
	v[1].sx=x; v[1].sy=y; v[1].sz=0.6f; v[1].rhw=0.5f; v[1].color=D3DRGBA(1.0f,1.0f,1.0f,alpha); v[1].tu=gl->sl; v[1].tv=gl->tl;
	v[2].sx=x+pic->width; v[2].sy=y+pic->height; v[2].sz=0.6f; v[2].rhw=0.5f; v[2].color=D3DRGBA(1.0f,1.0f,1.0f,alpha); v[2].tu=gl->sh; v[2].tv=gl->th;
	v[3].sx=x+pic->width; v[3].sy=y; v[3].sz=0.6f; v[3].rhw=0.5f; v[3].color=D3DRGBA(1.0f,1.0f,1.0f,alpha); v[3].tu=gl->sh; v[3].tv=gl->tl;

	D3D_Bind (gl->texnum);

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_SRCBLEND,   D3DBLEND_SRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_DESTBLEND,   D3DBLEND_INVSRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   TRUE );

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        FALSE );
	IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,v,4,NULL);
    IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        TRUE );

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   FALSE );
/*	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;
	glDisable(GL_ALPHA_TEST);
	glEnable (GL_BLEND);
//	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//	glCullFace(GL_FRONT);
	glColor4f (1,1,1,alpha);
	GL_Bind (gl->texnum);
	glBegin (GL_QUADS);
	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (x, y);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (x, y+pic->height);
	glEnd ();
	glColor4f (1,1,1,1);
	glEnable(GL_ALPHA_TEST);
	glDisable (GL_BLEND);
	*/
}


/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic)
{
	byte			*dest, *source;
	unsigned short	*pusdest;
//	int				v, u;
	glpic_t			*gl;

	D3DTLVERTEX v[4];

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;


	v[0].sx=x; v[0].sy=y+pic->height; v[0].sz=0.5f; v[0].rhw=0.5f; v[0].color=0xffffffff; v[0].tu=gl->sl; v[0].tv=gl->th;
	v[1].sx=x; v[1].sy=y; v[1].sz=0.5f; v[1].rhw=0.5f; v[1].color=0xffffffff; v[1].tu=gl->sl; v[1].tv=gl->tl;
	v[2].sx=x+pic->width; v[2].sy=y+pic->height; v[2].sz=0.5f; v[2].rhw=0.5f; v[2].color=0xffffffff; v[2].tu=gl->sh; v[2].tv=gl->th;
	v[3].sx=x+pic->width; v[3].sy=y; v[3].sz=0.5f; v[3].rhw=0.5f; v[3].color=0xffffffff; v[3].tu=gl->sh; v[3].tv=gl->tl;

	D3D_Bind (gl->texnum);

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        FALSE );
	IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX ,v,4,NULL);
    IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        TRUE );	
	/*
	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;
	glColor4f (1,1,1,1);
	GL_Bind (gl->texnum);
	glBegin (GL_QUADS);
	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (x, y);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (x, y+pic->height);
	glEnd ();
	*/
}


/*
=============
Draw_TransPic
=============
*/
void Draw_TransPic (int x, int y, qpic_t *pic)
{
	byte	*dest, *source, tbyte;
	unsigned short	*pusdest;
	int				v, u;

	if (x < 0 || (unsigned)(x + pic->width) > vid.width || y < 0 ||
		 (unsigned)(y + pic->height) > vid.height)
	{
		return; //syq 2000.10.31
		Sys_Error ("Draw_TransPic: bad coordinates");
	}
		
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_SRCBLEND,   D3DBLEND_SRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_DESTBLEND,   D3DBLEND_INVSRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   TRUE );
	Draw_Pic (x, y, pic);
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   FALSE );
}


/*
=============
Draw_TransPicTranslate

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (int x, int y, qpic_t *pic, byte *translation)
{
	int				v, u, c;
	unsigned		trans[64*64], *dest;
	byte			*src;
	int				p;
	D3DTLVERTEX ver[4];

	c = pic->width * pic->height;

	dest = trans;
	for (v=0 ; v<64 ; v++, dest += 64)
	{
		src = &menuplyr_pixels[ ((v*pic->height)>>6) *pic->width];
		for (u=0 ; u<64 ; u++)
		{
			p = src[(u*pic->width)>>6];
			if (p == 255)
				dest[u] = p;
			else
				dest[u] =  d_8to24table[translation[p]];
		}
	}

	translate_texture=D3D_LoadTexture32( "translate_texture",64,64,trans,FALSE,TRUE);
	D3D_Bind (translate_texture);

	ver[0].sx=x; ver[0].sy=y+pic->height; ver[0].sz=0.5f; ver[0].rhw=0.5f; ver[0].color=0xffffffff; ver[0].tu=0; ver[0].tv=1;
	ver[1].sx=x; ver[1].sy=y; ver[1].sz=0.5f; ver[1].rhw=0.5f; ver[1].color=0xffffffff; ver[1].tu=0; ver[1].tv=0;
	ver[2].sx=x+pic->width; ver[2].sy=y+pic->height; ver[2].sz=0.5f; ver[2].rhw=0.5f; ver[2].color=0xffffffff; ver[2].tu=1; ver[2].tv=1;
	ver[3].sx=x+pic->width; ver[3].sy=y; ver[3].sz=0.5f; ver[3].rhw=0.5f; ver[3].color=0xffffffff; ver[3].tu=1; ver[3].tv=0;

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_SRCBLEND,   D3DBLEND_SRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_DESTBLEND,   D3DBLEND_INVSRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   TRUE );
    
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        FALSE );
	IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX ,ver,4,NULL);
    IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        TRUE );

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   FALSE );


/*
	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glColor3f (1,1,1);
	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);
	glVertex2f (x, y);
	glTexCoord2f (1, 0);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (1, 1);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (0, 1);
	glVertex2f (x, y+pic->height);
	glEnd ();
	*/
}



//#define RAWSIZE	512
void Draw_StretchRaw(int x, int y, int w, int h, int cols, int rows, byte *data, qboolean rgba)
{
	//static unsigned	image32[RAWSIZE * RAWSIZE];

	float		t;
	D3DTLVERTEX ver[4];

	qboolean blendmode = true;

	if (blendmode && !r_lightmap.value) {
		IDirect3DDevice7_SetRenderState(g_pd3dDevice, D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
		IDirect3DDevice7_SetRenderState(g_pd3dDevice, D3DRENDERSTATE_DESTBLEND, D3DBLEND_SRCCOLOR);
		IDirect3DDevice7_SetRenderState(g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
	}


	if (rgba) {
		int src_texture = D3D_LoadTexture32("StretchRaw_texture", cols, rows, data, FALSE, TRUE);
		D3D_Bind(src_texture);

		t = 1;
	}


	ver[0].sx = x; ver[0].sy = y + h; ver[0].sz = 0.5f; ver[0].rhw = 0.5f; ver[0].color = 0xffffffff; ver[0].tu = 0; ver[0].tv = 1;
	ver[1].sx = x; ver[1].sy = y; ver[1].sz = 0.5f; ver[1].rhw = 0.5f; ver[1].color = 0xffffffff; ver[1].tu = 0; ver[1].tv = 0;
	ver[2].sx = x + w; ver[2].sy = y + h; ver[2].sz = 0.5f; ver[2].rhw = 0.5f; ver[2].color = 0xffffffff; ver[2].tu = 1; ver[2].tv = 1;
	ver[3].sx = x + w; ver[3].sy = y; ver[3].sz = 0.5f; ver[3].rhw = 0.5f; ver[3].color = 0xffffffff; ver[3].tu = 1; ver[3].tv = 0;

	IDirect3DDevice3_SetTextureStageState(g_pd3dDevice, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	IDirect3DDevice3_SetTextureStageState(g_pd3dDevice, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

	IDirect3DDevice7_SetRenderState(g_pd3dDevice, D3DRENDERSTATE_ZENABLE, FALSE);
	IDirect3DDevice7_DrawPrimitive(g_pd3dDevice, D3DPT_TRIANGLESTRIP, D3DFVF_TLVERTEX, ver, 4, NULL);
	IDirect3DDevice7_SetRenderState(g_pd3dDevice, D3DRENDERSTATE_ZENABLE, TRUE);

	IDirect3DDevice3_SetTextureStageState(g_pd3dDevice, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	IDirect3DDevice3_SetTextureStageState(g_pd3dDevice, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);


	IDirect3DDevice7_SetRenderState(g_pd3dDevice, D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
	IDirect3DDevice7_SetRenderState(g_pd3dDevice, D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
	IDirect3DDevice7_SetRenderState(g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);

}



/*
================
Draw_ConsoleBackground

================
*/
void Draw_ConsoleBackground (int lines)
{
	int y = (vid.height * 3) >> 2;

	if (lines > y)
		Draw_Pic(0, lines - vid.height, conback);
	else
		Draw_AlphaPic (0, lines - vid.height, conback, (float)(1.2 * lines)/y);
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h)
{
	/*
	glColor3f (1,1,1);
	GL_Bind (*(int *)draw_backtile->data);
	glBegin (GL_QUADS);
	glTexCoord2f (x/64.0, y/64.0);
	glVertex2f (x, y);
	glTexCoord2f ( (x+w)/64.0, y/64.0);
	glVertex2f (x+w, y);
	glTexCoord2f ( (x+w)/64.0, (y+h)/64.0);
	glVertex2f (x+w, y+h);
	glTexCoord2f ( x/64.0, (y+h)/64.0 );
	glVertex2f (x, y+h);
	glEnd ();
	*/
	D3DTLVERTEX v[4];

	v[0].sx=x; v[0].sy=y+h; v[0].sz=0.5f; v[0].rhw=0.5f; v[0].color=0xffffffff; v[0].tu=x/64.0f; v[0].tv=(y+h)/64.0f;
	v[1].sx=x; v[1].sy=y; v[1].sz=0.5f; v[1].rhw=0.5f; v[1].color=0xffffffff; v[1].tu=x/64.0f; v[1].tv=y/64.0f;
	v[2].sx=x+w; v[2].sy=y+h; v[2].sz=0.5f; v[2].rhw=0.5f; v[2].color=0xffffffff; v[2].tu=(x+w)/64.0f; v[2].tv=(y+h)/64.0f;
	v[3].sx=x+w; v[3].sy=y; v[3].sz=0.5f; v[3].rhw=0.5f; v[3].color=0xffffffff; v[3].tu=(x+w)/64.0f; v[3].tv=y/64.0f;
	
	D3D_Bind (*(int *)draw_backtile->data);

	IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,v,4,NULL);
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
/*
	glDisable (GL_TEXTURE_2D);
	glColor3f (host_basepal[c*3]/255.0,
		host_basepal[c*3+1]/255.0,
		host_basepal[c*3+2]/255.0);

	glBegin (GL_QUADS);

	glVertex2f (x,y);
	glVertex2f (x+w, y);
	glVertex2f (x+w, y+h);
	glVertex2f (x, y+h);

	glEnd ();
	glColor3f (1,1,1);
	glEnable (GL_TEXTURE_2D);
	*/
	RECT destr={x,y,x+w,y+h};
	DDBLTFX stBltFx;
	stBltFx.dwSize=sizeof(stBltFx);
	stBltFx.dwFillColor=c;
	IDirectDrawSurface7_Blt(g_pddsBackBuffer,&destr,NULL,NULL,DDBLT_COLORFILL,&stBltFx);
}
//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
/*	glEnable (GL_BLEND);
	glDisable (GL_TEXTURE_2D);
	glColor4f (0, 0, 0, 0.8);
	glBegin (GL_QUADS);

	glVertex2f (0,0);
	glVertex2f (vid.width, 0);
	glVertex2f (vid.width, vid.height);
	glVertex2f (0, vid.height);

	glEnd ();
	glColor4f (1,1,1,1);
	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
*/
	LPDIRECTDRAWSURFACE7 pSurf;
	D3DTLVERTEX v[4];
//return;
	v[0].sx=0; v[0].sy=vid.height; v[0].sz=0.5f; v[0].rhw=0.5f; v[0].color=D3DRGBA(0.0f,0.0f,0.0f,0.3f); 
	v[1].sx=0; v[1].sy=0; v[1].sz=0.5f; v[1].rhw=0.5f; v[1].color=D3DRGBA(0.0f,0.0f,0.0f,0.3f);
	v[2].sx=vid.width; v[2].sy=vid.height; v[2].sz=0.5f; v[2].rhw=0.5f; v[2].color=D3DRGBA(0.0f,0.0f,0.0f,0.3f);
	v[3].sx=vid.width; v[3].sy=0; v[3].sz=0.5f; v[3].rhw=0.5f; v[3].color=D3DRGBA(0.0f,0.0f,0.0f,0.3f);

//	IDirect3DDevice7_SetTextureStageState( g_pd3dDevice, 0, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	IDirect3DDevice7_GetTexture( g_pd3dDevice, 0, &pSurf );
	IDirect3DDevice7_SetTexture( g_pd3dDevice, 0, NULL );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_SRCBLEND,   D3DBLEND_SRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_DESTBLEND,   D3DBLEND_INVSRCALPHA  );
	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   TRUE );

    IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        FALSE );
	IDirect3DDevice7_DrawPrimitive(g_pd3dDevice,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,v,4,NULL);
    IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ZENABLE,        TRUE );

	IDirect3DDevice7_SetRenderState( g_pd3dDevice, D3DRENDERSTATE_ALPHABLENDENABLE,   FALSE );
//	IDirect3DDevice7_SetTextureStageState( g_pd3dDevice, 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );	
	IDirect3DDevice7_SetTexture( g_pd3dDevice, 0, pSurf );

	Sbar_Changed();

}

//=============================================================================

/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/
void Draw_BeginDisc (void)
{
	if (!draw_disc)
		return;
	//glDrawBuffer  (GL_FRONT);
	Draw_Pic (vid.width - 24, 0, draw_disc);
	//glDrawBuffer  (GL_BACK);
	
}


/*
================
Draw_EndDisc

Erases the disc icon.
Call after completing any disc IO
================
*/
void Draw_EndDisc (void)
{
}

/*
================
D3D_Set2D

Setup as if the screen was 320*200
================
*/
void D3D_Set2D (void)
{
	//glViewport (glx, gly, glwidth, glheight);
	g_vp.dwX=glx;
	g_vp.dwY=gly;            
	g_vp.dwWidth=glwidth;
	g_vp.dwHeight=glheight;
	IDirect3DDevice7_SetViewport( g_pd3dDevice, &g_vp );

/*
	glMatrixMode(GL_PROJECTION);
    glLoadIdentity ();
	glOrtho  (0, vid.width, vid.height, 0, -99999, 99999);

	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity ();

	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);
	glEnable (GL_ALPHA_TEST);
//	glDisable (GL_ALPHA_TEST);

	glColor4f (1,1,1,1);
	*/
}

//====================================================================

/*
================
GL_FindTexture
================
*/
int GL_FindTexture (char *identifier)
{
	int		i;
	gltexture_t	*glt;

	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
	{
		if (!strcmp (identifier, glt->identifier))
			return gltextures[i].texnum;
	}

	return -1;
}

/*
================
GL_ResampleTexture
================
*/
void GL_ResampleTexture (unsigned *in, int inwidth, int inheight, unsigned *out,  int outwidth, int outheight)
{
	int		i, j;
	unsigned	*inrow;
	unsigned	frac, fracstep;

	fracstep = inwidth*0x10000/outwidth;
	for (i=0 ; i<outheight ; i++, out += outwidth)
	{
		inrow = in + inwidth*(i*inheight/outheight);
		frac = fracstep >> 1;
		for (j=0 ; j<outwidth ; j+=4)
		{
			out[j] = inrow[frac>>16];
			frac += fracstep;
			out[j+1] = inrow[frac>>16];
			frac += fracstep;
			out[j+2] = inrow[frac>>16];
			frac += fracstep;
			out[j+3] = inrow[frac>>16];
			frac += fracstep;
		}
	}
}

/*
================
GL_Resample8BitTexture -- JACK
================
*/
void GL_Resample8BitTexture (unsigned char *in, int inwidth, int inheight, unsigned char *out,  int outwidth, int outheight)
{
	int		i, j;
	unsigned	char *inrow;
	unsigned	frac, fracstep;

	fracstep = inwidth*0x10000/outwidth;
	for (i=0 ; i<outheight ; i++, out += outwidth)
	{
		inrow = in + inwidth*(i*inheight/outheight);
		frac = fracstep >> 1;
		for (j=0 ; j<outwidth ; j+=4)
		{
			out[j] = inrow[frac>>16];
			frac += fracstep;
			out[j+1] = inrow[frac>>16];
			frac += fracstep;
			out[j+2] = inrow[frac>>16];
			frac += fracstep;
			out[j+3] = inrow[frac>>16];
			frac += fracstep;
		}
	}
}


/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/
void GL_MipMap (byte *in, int width, int height)
{
	int		i, j;
	byte	*out;

	width <<=2;
	height >>= 1;
	out = in;
	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=8, out+=4, in+=8)
		{
			out[0] = (in[0] + in[4] + in[width+0] + in[width+4])>>2;
			out[1] = (in[1] + in[5] + in[width+1] + in[width+5])>>2;
			out[2] = (in[2] + in[6] + in[width+2] + in[width+6])>>2;
			out[3] = (in[3] + in[7] + in[width+3] + in[width+7])>>2;
		}
	}
}

/*
================
GL_MipMap8Bit

Mipping for 8 bit textures
================
*/
void GL_MipMap8Bit (byte *in, int width, int height)
{
	int		i, j;
	unsigned short     r,g,b;
	byte	*out, *at1, *at2, *at3, *at4;

//	width <<=2;
	height >>= 1;
	out = in;
	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=2, out+=1, in+=2)
		{
			at1 = (byte *) (d_8to24table + in[0]);
			at2 = (byte *) (d_8to24table + in[1]);
			at3 = (byte *) (d_8to24table + in[width+0]);
			at4 = (byte *) (d_8to24table + in[width+1]);

 			r = (at1[0]+at2[0]+at3[0]+at4[0]); r>>=5;
 			g = (at1[1]+at2[1]+at3[1]+at4[1]); g>>=5;
 			b = (at1[2]+at2[2]+at3[2]+at4[2]); b>>=5;

			out[0] = d_15to8table[(r<<0) + (g<<5) + (b<<10)];
		}
	}
}

/*
===============
D3D_Upload32
===============
*/
void D3D_Upload32 (unsigned *data, int width, int height,  qboolean mipmap, qboolean alpha)
{
	int			samples;
static	unsigned	scaled[1024*512];	// [512*256];
	int			scaled_width, scaled_height;
	DWORD dwSurfFlag;

	for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
		;
	for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
		;

	scaled_width >>= (int)gl_picmip.value;
	scaled_height >>= (int)gl_picmip.value;

	if (scaled_width > gl_max_size.value)
		scaled_width = gl_max_size.value;
	if (scaled_height > gl_max_size.value)
		scaled_height = gl_max_size.value;

	if (scaled_width * scaled_height > sizeof(scaled)/4)
		Sys_Error ("GL_LoadTexture: too big");

	samples = alpha ? gl_alpha_format : gl_solid_format;
/*
#if 0
	if (mipmap)
		gluBuild2DMipmaps (GL_TEXTURE_2D, samples, width, height, GL_RGBA, GL_UNSIGNED_BYTE, trans);
	else if (scaled_width == width && scaled_height == height)
		glTexImage2D (GL_TEXTURE_2D, 0, samples, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);
	else
	{
		gluScaleImage (GL_RGBA, width, height, GL_UNSIGNED_BYTE, trans,
			scaled_width, scaled_height, GL_UNSIGNED_BYTE, scaled);
		glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
	}
#else
	*/
	texels += scaled_width * scaled_height;

	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			//glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			dwSurfFlag=D3DTEXTR_32BITSPERPIXEL|D3DTEXTR_TRANSPARENTWHITE;
			if (alpha) dwSurfFlag|=D3DTEXTR_CREATEWITHALPHA;
			if (S_OK==D3DTextr_CreateEmptyTexture( gltextures[numgltextures-1].identifier, scaled_width, scaled_height, 0, dwSurfFlag ) )
				D3DTextr_Restore( gltextures[numgltextures-1].identifier, g_pd3dDevice, data);
			goto done;
		}
		memcpy (scaled, data, width*height*4);
	}
	else
		GL_ResampleTexture (data, width, height, scaled, scaled_width, scaled_height);
//done: ;
	//glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
	dwSurfFlag=D3DTEXTR_32BITSPERPIXEL|D3DTEXTR_TRANSPARENTWHITE;
	if (alpha) dwSurfFlag|=D3DTEXTR_CREATEWITHALPHA;
	if (S_OK==D3DTextr_CreateEmptyTexture( gltextures[numgltextures-1].identifier, scaled_width, scaled_height, 0, dwSurfFlag ) )
		D3DTextr_Restore( gltextures[numgltextures-1].identifier, g_pd3dDevice, scaled);

	if (mipmap)
	{
		int		miplevel;

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			GL_MipMap ((byte *)scaled, scaled_width, scaled_height);
			scaled_width >>= 1;
			scaled_height >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;
//			glTexImage2D (GL_TEXTURE_2D, miplevel, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
		}
	}
done: ;
//#endif

/*
	if (mipmap)
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
	else
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
*/	
}

void D3D_Upload8_EXT (byte *data, int width, int height,  qboolean mipmap, qboolean alpha) 
{
	int			i, s;
	qboolean	noalpha;
	int			p;
	static unsigned j;
	int			samples;
    static	unsigned char scaled[1024*512];	// [512*256];
	int			scaled_width, scaled_height;

	s = width*height;
	// if there are no transparent pixels, make it a 3 component
	// texture even if it was specified as otherwise
	if (alpha)
	{
		noalpha = true;
		for (i=0 ; i<s ; i++)
		{
			if (data[i] == 255)
				noalpha = false;
		}

		if (alpha && noalpha)
			alpha = false;
	}
	for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
		;
	for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
		;

	scaled_width >>= (int)gl_picmip.value;
	scaled_height >>= (int)gl_picmip.value;

	if (scaled_width > gl_max_size.value)
		scaled_width = gl_max_size.value;
	if (scaled_height > gl_max_size.value)
		scaled_height = gl_max_size.value;

	if (scaled_width * scaled_height > sizeof(scaled))
		Sys_Error ("GL_LoadTexture: too big");

	samples = 1; // alpha ? gl_alpha_format : gl_solid_format;

	texels += scaled_width * scaled_height;
/*
	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			glTexImage2D (GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, scaled_width, scaled_height, 0, GL_COLOR_INDEX , GL_UNSIGNED_BYTE, data);
			goto done;
		}
		memcpy (scaled, data, width*height);
	}
	else
		GL_Resample8BitTexture (data, width, height, scaled, scaled_width, scaled_height);

	glTexImage2D (GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, scaled_width, scaled_height, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, scaled);
	if (mipmap)
	{
		int		miplevel;

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			GL_MipMap8Bit ((byte *)scaled, scaled_width, scaled_height);
			scaled_width >>= 1;
			scaled_height >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;
			glTexImage2D (GL_TEXTURE_2D, miplevel, GL_COLOR_INDEX8_EXT, scaled_width, scaled_height, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, scaled);
		}
	}
done: ;


	if (mipmap)
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
	else
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
	*/
}

/*
===============
D3D_Upload8
===============
*/
VID_Is8bit();

void D3D_Upload8 (byte *data, int width, int height,  qboolean mipmap, qboolean alpha)
{
static	unsigned	trans[640*480];		// FIXME, temporary
	int			i, s;
	qboolean	noalpha;
	int			p;

	s = width*height;
	// if there are no transparent pixels, make it a 3 component
	// texture even if it was specified as otherwise
	if (alpha)
	{
		noalpha = true;
		for (i=0 ; i<s ; i++)
		{
			p = data[i];
			if (p == 255)
				noalpha = false;
			trans[i] = d_8to24table[p];
		}

		if (alpha && noalpha)
			alpha = false;
	}
	else
	{
		if (s&3)
			Sys_Error ("GL_Upload8: s&3");
		for (i=0 ; i<s ; i+=4)
		{
			trans[i] = d_8to24table[data[i]];
			trans[i+1] = d_8to24table[data[i+1]];
			trans[i+2] = d_8to24table[data[i+2]];
			trans[i+3] = d_8to24table[data[i+3]];
		}
	}

 	if (VID_Is8bit() && !alpha && (data!=scrap_texels[0])) {
 		D3D_Upload8_EXT (data, width, height, mipmap, alpha);
		return;
	}
	D3D_Upload32 (trans, width, height, mipmap, alpha);
}

/*
================
D3D_LoadTexture
================
*/
int D3D_LoadTexture (char *identifier, int width, int height, byte *data, qboolean mipmap, qboolean alpha)
{
	qboolean	noalpha;
	int			i, p, s;
	gltexture_t	*glt;

	// see if the texture is allready present
	if (identifier[0])
	{
		for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
		{
			if (!strcmp (identifier, glt->identifier))
			{
				if (width != glt->width || height != glt->height)
					Sys_Error ("GL_LoadTexture: cache mismatch");
				return gltextures[i].texnum;
			}
		}
	}
//	else {
		glt = &gltextures[numgltextures];
		numgltextures++;
//	}

	if ('\0'==identifier[0])
		sprintf(glt->identifier,"%d",texture_extension_number);
	else
		strcpy (glt->identifier, identifier);
	glt->texnum = texture_extension_number;
	glt->width = width;
	glt->height = height;
	glt->mipmap = mipmap;
	//glt->lpSurface7 = NULL;

//	D3D_Bind(texture_extension_number );

	D3D_Upload8 (data, width, height, mipmap, alpha);

	return texture_extension_number++;
}


int D3D_LoadTexture32 (char *identifier, int width, int height, byte *data, qboolean mipmap, qboolean alpha)
{
	qboolean	noalpha;
	int			i, p, s;
	gltexture_t	*glt;
	DWORD dwSurfFlag;

	// see if the texture is allready present
	if (identifier[0])
	{
		for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
		{
			if (!strcmp (identifier, glt->identifier))
			{
				if (width != glt->width || height != glt->height) {
					//Sys_Error ("GL_LoadTexture: cache mismatch");
					D3DTextr_Invalidate( glt->identifier );
					D3DTextr_DestroyTexture( glt->identifier );
					dwSurfFlag=D3DTEXTR_32BITSPERPIXEL|D3DTEXTR_TRANSPARENTWHITE;
					if (alpha) dwSurfFlag|=D3DTEXTR_CREATEWITHALPHA;
					if (S_OK!=D3DTextr_CreateEmptyTexture( glt->identifier, width, height, 0, dwSurfFlag ) )
						return -1;
				}
				D3DTextr_Restore( glt->identifier, g_pd3dDevice, data);
				return gltextures[i].texnum;
			}
		}
	}
//	else {
		glt = &gltextures[numgltextures];
		numgltextures++;
//	}

	if ('\0'==identifier[0])
		sprintf(glt->identifier,"%d",texture_extension_number);
	else
		strcpy (glt->identifier, identifier);
	glt->texnum = texture_extension_number;
	glt->width = width;
	glt->height = height;
	glt->mipmap = mipmap;
	//glt->lpSurface7 = NULL;

//	D3D_Bind(texture_extension_number );

	D3D_Upload32 (data, width, height, mipmap, alpha);

	return texture_extension_number++;
}


void D3D_ReLoad8(char *identifier,byte *data, int width, int height,  qboolean mipmap, qboolean alpha)
{
static	unsigned	trans[640*480];		// FIXME, temporary
	int			i, s;
	qboolean	noalpha;
	int			p;
/*
	int			samples;
static	unsigned	scaled[1024*512];	// [512*256];
	int			scaled_width, scaled_height;
*/


	s = width*height;
	// if there are no transparent pixels, make it a 3 component
	// texture even if it was specified as otherwise
	if (alpha)
	{
		noalpha = true;
		for (i=0 ; i<s ; i++)
		{
			p = data[i];
			if (p == 255)
				noalpha = false;
			trans[i] = d_8to24table[p];
		}

		if (alpha && noalpha)
			alpha = false;
	}
	else
	{
		if (s&3)
			Sys_Error ("GL_Upload8: s&3");
		for (i=0 ; i<s ; i+=4)
		{
			trans[i] = d_8to24table[data[i]];
			trans[i+1] = d_8to24table[data[i+1]];
			trans[i+2] = d_8to24table[data[i+2]];
			trans[i+3] = d_8to24table[data[i+3]];
		}
	}
	D3DTextr_Restore( identifier, g_pd3dDevice, trans);	
/*
	for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
		;
	for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
		;

	scaled_width >>= (int)gl_picmip.value;
	scaled_height >>= (int)gl_picmip.value;

	if (scaled_width > gl_max_size.value)
		scaled_width = gl_max_size.value;
	if (scaled_height > gl_max_size.value)
		scaled_height = gl_max_size.value;

	if (scaled_width * scaled_height > sizeof(scaled)/4)
		Sys_Error ("GL_LoadTexture: too big");

	samples = alpha ? gl_alpha_format : gl_solid_format;
	texels += scaled_width * scaled_height;

	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			D3DTextr_Restore( identifier, g_pd3dDevice, trans);	
			//goto done;
			return;
		}
		memcpy (scaled, trans, width*height*4);
	}
	else
		GL_ResampleTexture (trans, width, height, scaled, scaled_width, scaled_height);

	D3DTextr_Restore( identifier, g_pd3dDevice, scaled);	
*/
}


/*
================
D3D_LoadPicTexture
================
*/
int D3D_LoadPicTexture (qpic_t *pic)
{
	return D3D_LoadTexture ("", pic->width, pic->height, pic->data, false, true);
}

/****************************************/

//static GLenum oldtarget = TEXTURE0_SGIS;

void D3D_SelectTexture (DWORD target) 
{
	if (!gl_mtexable)
		return;
	if (target == oldtarget) 
		return;
	oldtarget = target;
}

