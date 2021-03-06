
// GamePeer.h
// Scripting peer for system-level control of the game.
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

#include <luabind/luabind.hpp>
#include <luabind/object.hpp>

#include "../../../engine/Script/Handlers.h"
#include "../../../engine/Script/Peer.h"

namespace HoverRace {
	namespace Client {
		namespace HoverScript {
			class ConfigPeer;
			class SessionPeer;
			typedef boost::shared_ptr<SessionPeer> SessionPeerPtr;
		}
		class GameDirector;
		class Rulebook;
		typedef boost::shared_ptr<Rulebook> RulebookPtr;
	}
	namespace Script {
		class Core;
	}
}

namespace HoverRace {
namespace Client {
namespace HoverScript {

/**
 * Scripting peer for system-level control of the game.
 * @author Michael Imamura
 */
class GamePeer : public Script::Peer {
	typedef Script::Peer SUPER;
	public:
		GamePeer(Script::Core *scripting, GameDirector *gameDirector);
		virtual ~GamePeer();

	public:
		static void Register(Script::Core *scripting);

	public:
		void OnInit();
		void OnShutdown();
		void OnSessionStart(SessionPeerPtr sessionPeer);
		void OnSessionEnd(SessionPeerPtr sessionPeer);

		RulebookPtr RequestedNewSession();

	protected:
		void VerifyInitialized() const;

	public:
		ConfigPeer *LGetConfig();

		bool LIsInitialized();

		void LOnInit(const luabind::object &fn);
		void LOnInit_N(const std::string &name, const luabind::object &fn);

		void LOnShutdown(const luabind::object &fn);
		void LOnShutdown_N(const std::string &name, const luabind::object &fn);

		void LOnSessionBegin(const luabind::object &fn);
		void LOnSessionBegin_N(const std::string &name, const luabind::object &fn);

		void LOnSessionEnd(const luabind::object &fn);
		void LOnSessionEnd_N(const std::string &name, const luabind::object &fn);

		void LStartPractice(const std::string &track);
		void LStartPractice_R(const std::string &track, const luabind::object &rules);

		void LShutdown();

	private:
		Script::Core *scripting;
		GameDirector *gameDirector;
		bool initialized;
		Script::Handlers onInit;
		Script::Handlers onShutdown;
		Script::Handlers onSessionStart;
		Script::Handlers onSessionEnd;
		RulebookPtr deferredStart;
};

}  // namespace HoverScript
}  // namespace Client
}  // namespace HoverRace
