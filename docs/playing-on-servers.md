# Playing on a HoverNet server

HoverNet 2.1 can race on the new HoverNet servers as well as on the older online lobby. On a HoverNet server the server runs the race, so
every player sees the same result and a craft cannot cheat its way across the line. This page is for players. If you want to run a server,
see the [operator guide](operators.md).

You need the address of a server (its owner gives it to you). There is no public one built into the game.

## Connecting

1. Choose **HOVERNET SERVERS** on the main menu (also on the pause menu and the results screen).
2. Type the server's **address**, for example `race.example.org:9610`. In the browser the address starts with `wss://`, for example
   `wss://race.example.org/auth/`.
3. If the owner gave you a long **server key** (64 characters), paste it in. It makes the connection private and makes sure you are talking to
   the real server. Leave it empty for a plain connection. The browser has no key field: the web address itself is secure.
4. Type the **name** other players will see, then press **Connect**. The game remembers the address and your name.

## Races

- **Join** a race in the list, or **Watch** one that has already started (you see it a few seconds behind, so nobody can cheat by watching).
- **Host a race:** pick a track, the number of players and laps, and whether weapons are on, then **Host race**. The race starts when the
  players are in.
- **Time trial:** host a race with **one player**. It starts at once and you race the clock alone; the server saves it as a verifiable replay.
- **Private race:** type a **code** of 6 to 16 characters when you host. The race is not listed. The screen shows you the race's **number** while
  you wait: tell your friends the number and the code, and they type both under **A friend's private race**. A wrong code looks like a race that does
  not exist.

If the server refuses something, the screen says why in plain words (full, restarting, wrong code, not allowed here).

## During and after the race

- You drive as always. Your craft feels immediate even on a slow connection; the server's answer quietly corrects it.
- **Chat:** just type, and press **Enter** to send. It works after you have finished too.
- **When you cross the finish line the race goes on.** You are told to press **Enter** for your results; take your time, drive on, chat.
  On a touch screen use **MENU**. The results screen lets you keep driving, start another race, or go back to the servers.
- **Watching:** **Left** and **Right** (or **[** and **]**) move the camera to the other craft.
- If your connection drops, the game tries to take your place back for about 15 seconds.

## Good to know

- Your name is not an account: anyone can use any name, and nothing here proves who you are.
- The server's owner can mute or ban names and addresses. Reporting a name from inside the game is not built yet.
- Recorded races are public replays of what the server saw (the inputs of everyone in the race, and their names are not stored in them).
