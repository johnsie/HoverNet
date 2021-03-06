
/* StdAfx.h
	Precompiled header for ParcelDump. */

#ifdef _WIN32

#	define VC_EXTRALEAN							// Exclude rarely-used stuff from Windows headers

	// Minimum Windows version: XP
#	define WINVER 0x0501

#	define _CRT_SECURE_NO_DEPRECATE
#	define _SCL_SECURE_NO_DEPRECATE

#	include <windows.h>
#	include <typeinfo>

#	pragma warning(disable: 4251)
#	pragma warning(disable: 4275)

#	include "../../include/config-win32.h"

#else

#	include "../../include/compat/unix.h"
#	include "../../config.h"

#	include <string.h>
#	include <strings.h>

#endif

// Prefer Boost::Filesystem v3 on Boost 1.44+.
#include <boost/version.hpp>
#if BOOST_VERSION >= 104400
#	define BOOST_FILESYSTEM_VERSION 3
#	define BOOST_FILESYSTEM_NO_DEPRECATED
#else
#	define BOOST_FILESYSTEM_VERSION 2
#endif

#include <math.h>
#include <stdio.h>

#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

#ifndef _WIN32
	// Xlib.h must be included *after* boost/foreach.hpp as a workaround for
	// https://svn.boost.org/trac/boost/ticket/3000
#	include <X11/Xlib.h>
#endif

