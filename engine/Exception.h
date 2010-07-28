
// Exception.h
// Base exception.
//
// Copyright (c) 2010 Michael Imamura.
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.
//
// A copy of the license should have been attached to the package from which
// you have taken this file. If you can not find the license you can not use
// this file.
//
//
// The author makes no representations about the suitability of
// this software for any purpose.  It is provided "as is" "AS IS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.
//
// See the License for the specific language governing permissions
// and limitations under the License.

#pragma once

#include <exception>

#ifdef _WIN32
#	ifdef MR_ENGINE
#		define MR_DllDeclare   __declspec( dllexport )
#	else
#		define MR_DllDeclare   __declspec( dllimport )
#	endif
#else
#	define MR_DllDeclare
#endif

namespace HoverRace {

/**
 * Base exception, providing constructors for setting the message.
 * @author Michael Imamura
 */
class MR_DllDeclare Exception : public std::exception
{
	typedef std::exception SUPER;

	public:
		Exception() : SUPER() { }
		Exception(const std::string &msg) : SUPER(), msg(msg) { }
		Exception(const char *msg) : SUPER(), msg(msg) { }
		virtual ~Exception() throw() { }

		virtual const char* what() const throw() { return msg.c_str(); }

	protected:
		std::string &GetMessage() { return msg; }

	private:
		std::string msg;
};

/**
 * Exception indicating an unimplemented bit of code has been hit.
 * @author Michael Imamura
 */
class MR_DllDeclare UnimplementedExn : public Exception
{
	typedef Exception SUPER;

	public:
		UnimplementedExn() : SUPER() { }
		UnimplementedExn(const std::string &msg) : SUPER(msg) { }
		UnimplementedExn(const char *msg) : SUPER(msg) { }
		virtual ~UnimplementedExn() throw() { }
};

}  // namespace HoverRace

#undef MR_DllDeclare
