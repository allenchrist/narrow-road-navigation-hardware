"""Synthetic camera pictures of a lane between two walls, for testing.

A pinhole camera at a known height and tilt looks down a straight road of a
known width with a wall on each side, like the narrow lanes of Coimbatore.
Each pixel's ray is followed until it hits the ground or a wall.
"""

from __future__ import annotations

import math

import numpy as np


def _render_lane(
    width: int = 320,
    height: int = 240,
    road_width_m: float = 3.0,
    road_offset_m: float = 0.0,
    camera_height_m: float = 1.2,
    pitch_deg: float = 12.0,
    hfov_deg: float = 66.0,
    walls: bool = True,
    seed: int = 1,
    noise: float = 6.0,
) -> np.ndarray:
    rng = np.random.default_rng(seed)
    f = (width / 2) / math.tan(math.radians(hfov_deg) / 2)
    cx, cy = width / 2, height / 2
    s, c = math.sin(math.radians(pitch_deg)), math.cos(math.radians(pitch_deg))

    u, v = np.meshgrid(np.arange(width) + 0.0, np.arange(height) + 0.0)
    xn, yn = (u - cx) / f, (v - cy) / f
    # World axes: x right, y ahead, z up. Camera right = (1,0,0),
    # down = (0,-s,-c), forward = (0,c,-s).
    dx = xn
    dy = -s * yn + c
    dz = -c * yn - s

    # Rays that miss the ground or a wall give inf, which is expected
    t_ground = np.where(dz < 0, camera_height_m / -dz, np.inf)
    left_x, right_x = road_offset_m - road_width_m / 2, road_offset_m + road_width_m / 2
    t_wall = np.full(dx.shape, np.inf)
    if walls:
        t_r = np.where(dx > 0, right_x / dx, np.inf)
        t_l = np.where(dx < 0, left_x / dx, np.inf)
        t_wall = np.minimum(t_r, t_l)
        z_at = camera_height_m + t_wall * dz
        t_wall = np.where((z_at >= 0) & (z_at <= 4.0), t_wall, np.inf)

    img = np.full((height, width), 215.0)  # sky
    ground = t_ground < t_wall
    wall = (t_wall < t_ground) & np.isfinite(t_wall)

    gx, gy = dx * t_ground, dy * t_ground
    # Road: dark asphalt with a little texture; off the road: dusty verge
    road_tone = 95 + 8 * np.sin(gy * 3.1) * np.sin(gx * 2.3)
    verge_tone = 150 + 10 * np.sin(gy * 1.7)
    on_road = (gx >= left_x) & (gx <= right_x)
    img[ground] = np.where(on_road, road_tone, verge_tone)[ground]

    # Walls: light plaster with darker doors and windows along them
    wy, wz = dy * t_wall, camera_height_m + dz * t_wall
    plaster = 185 + 6 * np.sin(wy * 0.9)
    openings = (np.sin(wy * 1.3) > 0.6) & (wz > 0.3) & (wz < 2.2)
    img[wall] = np.where(openings, 70, plaster)[wall]

    img += rng.normal(0, noise, img.shape)
    return np.clip(img, 0, 255).astype(np.uint8)


def write_pgm(path: str, img: np.ndarray) -> None:
    h, w = img.shape
    with open(path, "wb") as f:
        f.write(f"P5\n{w} {h}\n255\n".encode())
        f.write(img.tobytes())


def render_lane(*args, **kwargs) -> np.ndarray:
    """A grayscale picture of the lane; see _render_lane for the options."""
    with np.errstate(divide="ignore", invalid="ignore"):
        return _render_lane(*args, **kwargs)
