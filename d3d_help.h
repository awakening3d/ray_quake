//d3d_help.h

#ifndef D3D_HELP_H
#define D3D_HELP_H

#define g_PI       (3.14159265358979323846f) // Pi
#define g_2_PI     (6.28318530717958623200f) // 2 * Pi
#define g_PI_DIV_2 (1.57079632679489655800f) // Pi / 2
#define g_PI_DIV_4 (0.78539816339744827900f) // Pi / 4
#define g_INV_PI   (0.31830988618379069122f) // 1 / Pi

#define AngleToRadian(a)	((a)/180.0f*g_PI)	//½Ç¶Èµ½»¡¶È

VOID D3DUtil_SetIdentityMatrix( D3DMATRIX* pm );
HRESULT D3DUtil_SetProjectionMatrix( D3DMATRIX* pmat, FLOAT fFOV, FLOAT fAspect,
                                     FLOAT fNearPlane, FLOAT fFarPlane );
HRESULT D3DUtil_SetViewMatrix( D3DMATRIX* pm, D3DVECTOR* pvFrom,
                               D3DVECTOR* pvAt, D3DVECTOR* pvWorldUp );
D3DVALUE mMagnitude(LPD3DVECTOR pv);
D3DVALUE mSquareMagnitude (LPD3DVECTOR pv);
D3DVALUE mDotProduct (D3DVECTOR* pv1, D3DVECTOR* pv2);
D3DVECTOR mCrossProduct (D3DVECTOR* pv1,D3DVECTOR* pv2);
VOID D3DUtil_SetRotationMatrix( D3DMATRIX* pmat, FLOAT fRads, D3DVALUE fX, D3DVALUE fY, D3DVALUE fZ );
VOID D3DMath_MatrixMultiply( D3DMATRIX* pq, D3DMATRIX* pa, D3DMATRIX* pb );
VOID D3DUtil_RotateMatrix( D3DMATRIX* pmat, FLOAT fRads, D3DVALUE fX, D3DVALUE fY, D3DVALUE fZ );
VOID D3DUtil_SetTranslateMatrix( D3DMATRIX* pm, FLOAT tx, FLOAT ty, FLOAT tz );
VOID D3DUtil_TranslateMatrix( D3DMATRIX* pm, FLOAT tx, FLOAT ty, FLOAT tz );
VOID D3DUtil_SetScaleMatrix( D3DMATRIX* pm, FLOAT sx, FLOAT sy, FLOAT sz );
VOID D3DUtil_ScaleMatrix( D3DMATRIX* pm, FLOAT sx, FLOAT sy, FLOAT sz );

VOID D3DUtil_SaveMatrix(LPDIRECT3DDEVICE7 pd3dDevice);
VOID D3DUtil_RestoreMatrix(LPDIRECT3DDEVICE7 pd3dDevice);

VOID D3DUtil_SetMaterial(LPDIRECT3DDEVICE7 pd3dDevice,D3DVALUE r,D3DVALUE g,D3DVALUE b,D3DVALUE a);

LPCSTR GetErrorString(HRESULT hr);

#endif
