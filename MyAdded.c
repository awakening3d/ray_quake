
long ceil_cw=0, single_cw=0, full_cw=0, cw=0, pushed_cw=0;
long fpenv[8]={0,0,0,0,0,0,0,0};

void Sys_SetFPCW (void)
{
	__asm
	{
	fnstcw ds:word ptr[cw]
	mov eax,ds:dword ptr[cw]
#if	id386
	and ah,0F0h
	or ah,003h	// round mode, 64-bit precision
#endif
	mov ds:dword ptr[full_cw],eax

#if	id386
	and ah,0F0h
	or ah,00Ch	// chop mode, single precision
#endif
	mov ds:dword ptr[single_cw],eax

#if	id386
	and ah,0F0h
	or ah,008h	// ceil mode, single precision
#endif
	mov ds:dword ptr[ceil_cw],eax
	}
}

void Sys_PushFPCW_SetHigh (void)
{
	__asm
	{
	fnstcw ds:word ptr[pushed_cw]
	fldcw ds:word ptr[full_cw]
	}
}

void Sys_PopFPCW (void)
{
	__asm
	{
	fldcw ds:word ptr[pushed_cw]
	}
}

void MaskExceptions (void)
{
	__asm
	{
	fnstenv ds:dword ptr[fpenv]
	or ds:dword ptr[fpenv],03Fh
	fldenv ds:dword ptr[fpenv]
	}
}

void Sys_HighFPPrecision(void)
{
	__asm
	{
	fldcw ds:word ptr[full_cw]
	}
}

void Sys_LowFPPrecision(void)
{
	__asm
	{
	fldcw ds:word ptr[single_cw]
	}
}

void R_SurfacePatch(void)
{
}