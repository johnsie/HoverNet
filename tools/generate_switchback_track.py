#!/usr/bin/env python3
"""Generate the editable MazeCompiler source for the Switchback circuit."""

from __future__ import annotations

import math
from pathlib import Path


OUTPUT = Path("NetTarget/TrackSources/Switchback.track.txt")
SCALE = 0.60
HALF_WIDTH = 16.0

# A deliberately irregular multi-lobe route. Frequent direction changes keep
# every sector active while retaining enough spacing for the 32-metre track.
RAW_CENTRELINE = [
    (0, 0), (70, -35), (145, 10), (220, -25), (295, 35),
    (355, 95), (405, 170), (385, 245), (420, 315), (355, 365),
    (280, 345), (215, 390), (145, 350), (75, 375), (20, 315),
    (55, 250), (25, 185), (95, 145), (165, 175), (225, 140),
    (270, 205), (235, 270), (165, 305), (95, 265), (20, 230),
    (-60, 270), (-135, 225), (-215, 250), (-290, 205), (-370, 220),
    (-430, 160), (-405, 90), (-455, 25), (-415, -45), (-345, -20),
    (-285, -85), (-210, -55), (-145, -110), (-75, -65),
]

CENTRELINE = [(x * SCALE, y * SCALE) for x, y in RAW_CENTRELINE]
ROOM_COUNT = len(CENTRELINE)

# A raised approach followed by a lower landing produces the same kind of
# jump used by Steeplechase. Yellow floor texture 66 calls out each launch.
ROOM_FLOORS = {10: 1.0, 11: -0.8, 29: 1.0, 30: -0.8}


def boundary_points() -> tuple[list[tuple[float, float]], list[tuple[float, float]]]:
    left: list[tuple[float, float]] = []
    right: list[tuple[float, float]] = []
    for index, (x, y) in enumerate(CENTRELINE):
        previous = CENTRELINE[(index - 1) % ROOM_COUNT]
        following = CENTRELINE[(index + 1) % ROOM_COUNT]
        dx = following[0] - previous[0]
        dy = following[1] - previous[1]
        length = math.hypot(dx, dy)
        nx = -dy / length
        ny = dx / length
        left.append((x + nx * HALF_WIDTH, y + ny * HALF_WIDTH))
        right.append((x - nx * HALF_WIDTH, y - ny * HALF_WIDTH))
    return left, right


LEFT, RIGHT = boundary_points()


def point_on_segment(room: int, fraction: float) -> tuple[float, float]:
    start = CENTRELINE[room - 1]
    end = CENTRELINE[room % ROOM_COUNT]
    return (start[0] + (end[0] - start[0]) * fraction,
            start[1] + (end[1] - start[1]) * fraction)


def pad_vertices(room: int, start: float = 0.25, end: float = 0.55) -> list[tuple[float, float]]:
    index = room - 1
    following = room % ROOM_COUNT

    def interpolate(points: list[tuple[float, float]], fraction: float) -> tuple[float, float]:
        first = points[index]
        second = points[following]
        return (first[0] + (second[0] - first[0]) * fraction,
                first[1] + (second[1] - first[1]) * fraction)

    # Clockwise, matching the room winding required by collision detection.
    return [interpolate(LEFT, start), interpolate(LEFT, end),
            interpolate(RIGHT, end), interpolate(RIGHT, start)]


def append_feature(lines: list[str], feature_id: int, room: int, texture: int,
                   start: float = 0.25, end: float = 0.55) -> None:
    floor = ROOM_FLOORS.get(room, 0.0)
    lines.extend(["[Feature]", f"Id={feature_id}", f"Parent={room}",
                  f"floor={floor - 0.05:.3f}, 1, 51",
                  f"ceiling={floor:.3f}, 1, {texture}"])
    for x, y in pad_vertices(room, start, end):
        lines.append(f"wall={x:.3f}, {y:.3f}, 1, 58")
    lines.append("")


def append_element(lines: list[str], room: int, class_id: int,
                   fraction: float = 0.45) -> None:
    x, y = point_on_segment(room, fraction)
    lines.extend(["[Free_Element]", f"Section={room}",
                  f"Position={x:.3f}, {y:.3f}, {ROOM_FLOORS.get(room, 0.0):.3f}",
                  "Orientation=0.000", f"Element_Type=1, {class_id}", ""])


def generate() -> str:
    length = sum(math.dist(CENTRELINE[index], CENTRELINE[(index + 1) % ROOM_COUNT])
                 for index in range(ROOM_COUNT))
    lines = ["[Header]",
             ("Description=Level: Medium\\n"
              f"Length: {length / 1000:.1f} km\\n"
              "A wide, winding multi-lobe circuit with water gardens, two jumps, and marked service pads.\\n"),
             "Background=NetTarget/mazes/BG-CITY.pcx", ""]

    for index in range(ROOM_COUNT):
        room = index + 1
        following = (index + 1) % ROOM_COUNT
        floor = ROOM_FLOORS.get(room, 0.0)
        floor_texture = 66 if room in (10, 29) else 51
        vertices = [LEFT[index], LEFT[following], RIGHT[following], RIGHT[index]]
        # The visible side surfaces are walls 2 and 4 (the wall texture is
        # attached to the edge ending at each vertex). Their traversal is
        # reversed, so the opposite-handed arrow assets keep both pointing
        # with race direction.
        textures = [52, 52, 53, 53]
        lines.extend(["[Room]", f"Id={room}", f"floor={floor:.3f}, 1, {floor_texture}",
                      "ceiling=12.000, 1, 50"])
        for (x, y), texture in zip(vertices, textures):
            lines.append(f"wall={x:.3f}, {y:.3f}, 1, {texture}")
        lines.append("")

    # Match Steeplechase's visual language: checker line plus FINISH lettering.
    append_feature(lines, 1, 1, 67, 0.48, 0.58)
    append_feature(lines, 2, 1, 69, 0.59, 0.82)
    # Broad blue-bubble sections form visible water gardens around the lap.
    for feature_id, room in enumerate((6, 14, 20, 27, 35), start=3):
        append_feature(lines, feature_id, room, 63, 0.12, 0.88)
    # Boosts are deliberately on flat exit sections, not jump approaches.
    append_feature(lines, 8, 16, 64)
    append_feature(lines, 9, 25, 65)
    append_feature(lines, 10, 37, 64)

    start_x, start_y = point_on_segment(1, 0.10)
    dx = CENTRELINE[1][0] - CENTRELINE[0][0]
    dy = CENTRELINE[1][1] - CENTRELINE[0][1]
    length = math.hypot(dx, dy)
    nx, ny = -dy / length, dx / length
    for player in range(8):
        row = player // 2
        side = -1 if player % 2 == 0 else 1
        x = start_x - dx / length * row * 5.0 + nx * side * 4.0
        y = start_y - dy / length * row * 5.0 + ny * side * 4.0
        lines.extend(["[Initial_Position]", "Section=1",
                      f"Position={x:.3f}, {y:.3f}, 0.000", "Orientation=0.000",
                      f"Team={player + 1}", ""])

    append_element(lines, 1, 202, 0.53)
    append_element(lines, 14, 203)
    append_element(lines, 27, 204)
    append_element(lines, 16, 201)
    append_element(lines, 25, 200)
    append_element(lines, 37, 201)
    append_element(lines, 8, 152)
    append_element(lines, 22, 152)
    append_element(lines, 33, 152)

    lines.append("[Connection_List]")
    for room in range(1, ROOM_COUNT + 1):
        following = room + 1 if room < ROOM_COUNT else 1
        lines.append(f"{room}, 1, {following}, 3")
    lines.append("")
    return "\n".join(lines)


if __name__ == "__main__":
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(generate(), encoding="utf-8")
