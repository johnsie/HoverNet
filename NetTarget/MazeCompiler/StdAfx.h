// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

// #define VC_EXTRALEAN		// Exclude rarely-used stuff from Windows headers

#ifdef _WIN32
#include <afxwin.h>         // MFC core and standard components
#include <afxext.h>         // MFC extensions
#include <afxtempl.h>
#else
#include "../Platform/MfcCompat.h"
#include <strings.h>
#define stricmp strcasecmp
#define strcmpi strcasecmp
#endif

#include <typeinfo>

#if defined(_WIN32) && !defined(_AFX_NO_AFXCMN_SUPPORT)
#include <afxcmn.h>			// MFC support for Windows 95 Common Controls
#endif // _AFX_NO_AFXCMN_SUPPORT


