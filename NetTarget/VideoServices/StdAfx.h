// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#ifdef _WIN32
#define VC_EXTRALEAN		// Exclude rarely-used stuff from Windows headers
#include <afxwin.h>         // MFC core and standard components
#include <afxext.h>         // MFC extensions
#include <afxtempl.h>
#include <mmsystem.h>
#include <mmreg.h>
#define DIRECTSOUND_VERSION 0x0700
#include <dsound.h>

// Ensure DirectSound constants are defined
#ifndef DSBCAPS_CTRLDEFAULT
#define DSBCAPS_CTRLDEFAULT 0x00000000
#endif

#ifndef DSBCAPS_STATIC
#define DSBCAPS_STATIC 0x00000002
#endif

#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>			// MFC support for Windows 95 Common Controls
#endif // _AFX_NO_AFXCMN_SUPPORT
#else
#include "../Platform/MfcCompat.h"
#endif

#include <typeinfo>




