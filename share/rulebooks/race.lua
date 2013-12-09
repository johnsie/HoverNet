
Rulebook 'Race' {
	title = 'Race',
	description = 'First player to complete the number of laps wins',

	rules = {
		laps = 5,
	},

	on_pre_game = function(session)
		print('on_pre_game in rulebook')
		print('laps = ' .. session.rules.laps)
		print('num players = ' .. session:get_num_players())
	end,

	on_player_joined = function(session, player)
		local player_name = 'Player ' .. (player.index + 1)

		-- Set up the player properties and events.
		player.props.lap = 1
		player:on_start(function()
			-- Set up the racing HUD.
			-- We start with the default HUD for a race and add our lap counter.
			local hud = player.hud
			hud:use_race_default()
			player.props.lap_counter = hud:add_counter(Hud.S, "Lap", 1,
				session.rules.laps)
		end)
		player:on_finish_line(function()
			local lap = player.props.lap + 1
			player.props.lap = lap

			print(player_name .. ' is on lap ' ..
				lap .. '/' .. session.rules.laps ..
				' (' .. (session.time / 1000) .. ')')

			player.props.lap_counter.value = lap
			if lap > session.rules.laps then
				print(player_name .. ' finished race!')
				player:finish()
			end
		end)
		player:on_finish(function()
			--TODO: Set up postgame HUD.
			player.hud:clear()
		end)

		--TODO: Set up the pregame HUD for the player.
	end,

	on_post_game = function(session)
		print('on_session_end in rulebook')
	end,
}
