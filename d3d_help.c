//d3d_help.c
#include "quakedef.h"
#include "d3d_help.h"

static D3DMATRIX gmatWorld,gmatView,gmatProj;

VOID D3DUtil_SetIdentityMatrix( D3DMATRIX* pm )
{
    pm->_12 = pm->_13 = pm->_14 = pm->_21 = pm->_23 = pm->_24 = 0.0f;
    pm->_31 = pm->_32 = pm->_34 = pm->_41 = pm->_42 = pm->_43 = 0.0f;
    pm->_11 = pm->_22 = pm->_33 = pm->_44 = 1.0f;
}


D3DVALUE mMagnitude(LPD3DVECTOR pv)
{
   return (D3DVALUE) sqrt(mSquareMagnitude(pv));
}

D3DVALUE mSquareMagnitude (LPD3DVECTOR pv)
{
   return pv->x*pv->x + pv->y*pv->y + pv->z*pv->z;
}

void mNormalize (LPD3DVECTOR pv)
{
	D3DVALUE fLen;
	fLen=mMagnitude(pv);
	pv->x/=fLen; pv->y/=fLen; pv->z/=fLen;
}


D3DVALUE mDotProduct (D3DVECTOR* pv1, D3DVECTOR* pv2)
{
   return pv1->x*pv2->x + pv1->y * pv2->y + pv1->z*pv2->z;
}

D3DVECTOR mCrossProduct (D3DVECTOR* pv1, D3DVECTOR* pv2)
{
	D3DVECTOR result;

	result.x = pv1->y * pv2->z - pv1->z * pv2->y;
	result.y = pv1->z * pv2->x - pv1->x * pv2->z;
	result.z = pv1->x * pv2->y - pv1->y * pv2->x;
	return result;
}


HRESULT D3DUtil_SetProjectionMatrix( D3DMATRIX* pmat, FLOAT fFOV, FLOAT fAspect,
                                     FLOAT fNearPlane, FLOAT fFarPlane )
{
	FLOAT w,h,Q;
	if( fabs(fFarPlane-fNearPlane) < 0.01f )
        return E_INVALIDARG;
    if( fabs(sin(fFOV/2)) < 0.01f )
        return E_INVALIDARG;

    w = fAspect * ( cos(fFOV/2)/sin(fFOV/2) );
    h =   1.0f  * ( cos(fFOV/2)/sin(fFOV/2) );
    Q = fFarPlane / ( fFarPlane - fNearPlane );

    ZeroMemory( pmat, sizeof(D3DMATRIX) );
    pmat->_11 = w;
    pmat->_22 = h;
    pmat->_33 = Q;
    pmat->_34 = 1.0f;
    pmat->_43 = -Q*fNearPlane;

    return S_OK;
}

HRESULT D3DUtil_SetViewMatrix( D3DMATRIX* pm, D3DVECTOR* pvFrom,
                               D3DVECTOR* pvAt, D3DVECTOR* pvWorldUp )
{
    // Get the z basis vector, which points straight ahead. This is the
    // difference from the eyepoint to the lookat point.
    D3DVECTOR vView;
	FLOAT fLength;
	FLOAT fDotProduct;
	D3DVECTOR vUp;
	D3DVECTOR vRight;

	vView.x = pvAt->x - pvFrom->x;
	vView.y = pvAt->y - pvFrom->y;
	vView.z = pvAt->z - pvFrom->z;

    fLength = mMagnitude( &vView );
    if( fLength < 1e-6f )
        return E_INVALIDARG;

    // Normalize the z basis vector
//    vView /= fLength;
	vView.x /= fLength;
	vView.y /= fLength;
	vView.z /= fLength;

    // Get the dot product, and calculate the projection of the z basis
    // vector onto the up vector. The projection is the y basis vector.
    fDotProduct = mDotProduct( pvWorldUp, &vView );

    vUp.x = pvWorldUp->x - fDotProduct * vView.x;
	vUp.y = pvWorldUp->y - fDotProduct * vView.y;
	vUp.z = pvWorldUp->z - fDotProduct * vView.z;

    // If this vector has near-zero length because the input specified a
    // bogus up vector, let's try a default up vector
    if( 1e-6f > ( fLength = mMagnitude( &vUp ) ) )
    {
//        vUp = D3DVECTOR( 0.0f, 1.0f, 0.0f ) - vView.y * vView;
		vUp.x = 0.0f - vView.y * vView.x;
		vUp.y = 1.0f - vView.y * vView.y;
		vUp.z = 0.0f - vView.y * vView.z;

        // If we still have near-zero length, resort to a different axis.
        if( 1e-6f > ( fLength = mMagnitude( &vUp ) ) )
        {
//            vUp = D3DVECTOR( 0.0f, 0.0f, 1.0f ) - vView.z * vView;
			vUp.x = 0.0f - vView.z * vView.x;
			vUp.y = 0.0f - vView.z * vView.y;
			vUp.z = 1.0f - vView.z * vView.z;

            if( 1e-6f > ( fLength = mMagnitude( &vUp ) ) )
                return E_INVALIDARG;
        }
    }

    // Normalize the y basis vector
    //vUp /= fLength;
	vUp.x /= fLength;
	vUp.y /= fLength;
	vUp.z /= fLength;

    // The x basis vector is found simply with the cross product of the y
    // and z basis vectors
    vRight = mCrossProduct( &vUp, &vView );

    // Start building the matrix. The first three rows contains the basis
    // vectors used to rotate the view to point at the lookat point
    D3DUtil_SetIdentityMatrix( pm );
    pm->_11 = vRight.x;    pm->_12 = vUp.x;    pm->_13 = vView.x;
    pm->_21 = vRight.y;    pm->_22 = vUp.y;    pm->_23 = vView.y;
    pm->_31 = vRight.z;    pm->_32 = vUp.z;    pm->_33 = vView.z;

    // Do the translation values (rotations are still about the eyepoint)
    pm->_41 = - mDotProduct( pvFrom, &vRight );
    pm->_42 = - mDotProduct( pvFrom, &vUp );
    pm->_43 = - mDotProduct( pvFrom, &vView );

    return S_OK;
}


VOID D3DUtil_SetRotationMatrix( D3DMATRIX* pmat, FLOAT fRads, D3DVALUE fX, D3DVALUE fY, D3DVALUE fZ )
{
    FLOAT     fCos = cos( fRads );
    FLOAT     fSin = sin( fRads );
    D3DVECTOR v;
	v.x=fX; v.y=fY; v.z=fZ;
	mNormalize(&v);

    pmat->_11 = ( v.x * v.x ) * ( 1.0f - fCos ) + fCos;
    pmat->_12 = ( v.x * v.y ) * ( 1.0f - fCos ) - (v.z * fSin);
    pmat->_13 = ( v.x * v.z ) * ( 1.0f - fCos ) + (v.y * fSin);

    pmat->_21 = ( v.y * v.x ) * ( 1.0f - fCos ) + (v.z * fSin);
    pmat->_22 = ( v.y * v.y ) * ( 1.0f - fCos ) + fCos ;
    pmat->_23 = ( v.y * v.z ) * ( 1.0f - fCos ) - (v.x * fSin);

    pmat->_31 = ( v.z * v.x ) * ( 1.0f - fCos ) - (v.y * fSin);
    pmat->_32 = ( v.z * v.y ) * ( 1.0f - fCos ) + (v.x * fSin);
    pmat->_33 = ( v.z * v.z ) * ( 1.0f - fCos ) + fCos;

    pmat->_14 = pmat->_24 = pmat->_34 = 0.0f;
    pmat->_41 = pmat->_42 = pmat->_43 = 0.0f;
    pmat->_44 = 1.0f;
}

VOID D3DMath_MatrixMultiply( D3DMATRIX* pq, D3DMATRIX* pa, D3DMATRIX* pb )
{
    FLOAT* pA = (FLOAT*)pa;
    FLOAT* pB = (FLOAT*)pb;
    FLOAT  pM[16];
	WORD i,j,k;

    ZeroMemory( pM, sizeof(D3DMATRIX) );

    for( i=0; i<4; i++ ) 
        for( j=0; j<4; j++ ) 
            for( k=0; k<4; k++ ) 
                pM[4*i+j] +=  pA[4*i+k] * pB[4*k+j];

    memcpy( pq, pM, sizeof(D3DMATRIX) );
}

VOID D3DUtil_RotateMatrix( D3DMATRIX* pmat, FLOAT fRads, D3DVALUE fX, D3DVALUE fY, D3DVALUE fZ )
{
	D3DMATRIX mat;
	D3DUtil_SetRotationMatrix(&mat,fRads,fX,fY,fZ);
	D3DMath_MatrixMultiply(pmat,&mat,pmat);
}

VOID D3DUtil_SetTranslateMatrix( D3DMATRIX* pm, FLOAT tx, FLOAT ty, FLOAT tz )
{ 
	D3DUtil_SetIdentityMatrix( pm ); 
	pm->_41 = tx; pm->_42 = ty; pm->_43 = tz; 
}

VOID D3DUtil_TranslateMatrix( D3DMATRIX* pm, FLOAT tx, FLOAT ty, FLOAT tz )
{
	D3DMATRIX mat;
	D3DUtil_SetTranslateMatrix(&mat,tx,ty,tz);
	D3DMath_MatrixMultiply(pm,&mat,pm);
}

VOID D3DUtil_SetScaleMatrix( D3DMATRIX* pm, FLOAT sx, FLOAT sy, FLOAT sz )
{ 
	D3DUtil_SetIdentityMatrix( pm );
	pm->_11 = sx; pm->_22 = sy; pm->_33 = sz; 
}

VOID D3DUtil_ScaleMatrix( D3DMATRIX* pm, FLOAT sx, FLOAT sy, FLOAT sz )
{
	D3DMATRIX mat;
	D3DUtil_SetScaleMatrix(&mat,sx,sy,sz);
	D3DMath_MatrixMultiply(pm,&mat,pm);
}

VOID D3DUtil_SaveMatrix(LPDIRECT3DDEVICE7 pd3dDevice)
{
	IDirect3DDevice7_GetTransform(pd3dDevice,D3DTRANSFORMSTATE_WORLD,&gmatWorld);
	IDirect3DDevice7_GetTransform(pd3dDevice,D3DTRANSFORMSTATE_VIEW,&gmatView);
	IDirect3DDevice7_GetTransform(pd3dDevice,D3DTRANSFORMSTATE_PROJECTION,&gmatProj);
}

VOID D3DUtil_RestoreMatrix(LPDIRECT3DDEVICE7 pd3dDevice)
{
	IDirect3DDevice7_SetTransform(pd3dDevice,D3DTRANSFORMSTATE_WORLD,&gmatWorld);
	IDirect3DDevice7_SetTransform(pd3dDevice,D3DTRANSFORMSTATE_VIEW,&gmatView);
	IDirect3DDevice7_SetTransform(pd3dDevice,D3DTRANSFORMSTATE_PROJECTION,&gmatProj);
}

VOID D3DUtil_SetMaterial(LPDIRECT3DDEVICE7 pd3dDevice,D3DVALUE r,D3DVALUE g,D3DVALUE b,D3DVALUE a)
{
	D3DMATERIAL7 mtrl;
    ZeroMemory( &mtrl, sizeof(D3DMATERIAL7) );
    mtrl.diffuse.r = mtrl.ambient.r =  r;
    mtrl.diffuse.g = mtrl.ambient.g =  g;
    mtrl.diffuse.b = mtrl.ambient.b =  b;
    mtrl.diffuse.a = mtrl.ambient.a =  a;
	IDirect3DDevice7_SetMaterial(pd3dDevice,&mtrl);
}

LPCSTR GetErrorString(HRESULT hr)
{
	switch (hr)
	{
	case DDERR_INCOMPATIBLEPRIMARY	:
		return "DDERR_INCOMPATIBLEPRIMARY";
	case DDERR_INVALIDCAPS	:
		return "DDERR_INVALIDCAPS";
	case DDERR_INVALIDOBJECT  :
		return "DDERR_INVALIDOBJECT";
	case DDERR_INVALIDPARAMS  :
		return "DDERR_INVALIDPARAMS";
	case DDERR_INVALIDPIXELFORMAT  :
		return "DDERR_INVALIDPIXELFORMAT";
	case DDERR_NOALPHAHW  :
		return "DDERR_NOALPHAHW";
	case DDERR_NOCOOPERATIVELEVELSET  :
		return "DDERR_NOCOOPERATIVELEVELSET";
	case DDERR_NODIRECTDRAWHW  :
		return "DDERR_NODIRECTDRAWHW";
	case DDERR_NOEMULATION  :
		return "DDERR_NOEMULATION";
	case DDERR_NOEXCLUSIVEMODE  :
		return "DDERR_NOEXCLUSIVEMODE";
	case DDERR_NOFLIPHW  :
		return "DDERR_NOFLIPHW";
	case DDERR_NOMIPMAPHW  :
		return "DDERR_NOMIPMAPHW";
	case DDERR_NOOVERLAYHW  :
		return "DDERR_NOOVERLAYHW";
	case DDERR_NOZBUFFERHW  :
		return "DDERR_NOZBUFFERHW";
	case DDERR_OUTOFMEMORY  :
		return "DDERR_OUTOFMEMORY";
	case DDERR_OUTOFVIDEOMEMORY  :
		return "DDERR_OUTOFVIDEOMEMORY";
	case DDERR_PRIMARYSURFACEALREADYEXISTS  :
		return "DDERR_PRIMARYSURFACEALREADYEXISTS";
	case DDERR_UNSUPPORTEDMODE  :
		return "DDERR_UNSUPPORTEDMODE";
	}
	return "";
}