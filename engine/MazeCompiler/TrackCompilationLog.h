// TrackCompilationLog.h
//
// Copyright (c) 2013 Michael Imamura.
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
//

#pragma once

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
namespace MazeCompiler {

/**
 * Simple logging interface for track compilation progress reporting.
 * @author Michael Imamura
 */
struct MR_DllDeclare TrackCompilationLog
{
	virtual void Info(const std::string &msg) = 0;
	virtual void Warn(const std::string &msg) = 0;
};
typedef std::shared_ptr<TrackCompilationLog> TrackCompilationLogPtr;

}  // namespace MazeCompiler
}  // namespace HoverRace

#undef MR_DllDeclare
