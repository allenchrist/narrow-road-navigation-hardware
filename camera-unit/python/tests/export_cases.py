"""Writes synthetic lane pictures and Python's answers for the C++ test.

Usage: python tests/export_cases.py <folder>
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)

from scene import render_lane, write_pgm  # noqa: E402
from widthlib import Calibration, homography_from_camera, measure_width  # noqa: E402

W, H = 320, 240

CASES = [
    dict(name="lane_2_5", road_width_m=2.5),
    dict(name="lane_3_0", road_width_m=3.0),
    dict(name="lane_3_5_offset", road_width_m=3.5, road_offset_m=0.5),
    dict(name="lane_4_0", road_width_m=4.0),
    dict(name="lane_6_0", road_width_m=6.0),
    dict(name="open_road", road_width_m=14.0, walls=False),
]


def main(folder: str) -> None:
    os.makedirs(folder, exist_ok=True)
    cal = Calibration(homography_from_camera(W, H, 1.2, 12.0, 66.0), W, H)
    index = []
    for case in CASES:
        options = {k: v for k, v in case.items() if k != "name"}
        img = render_lane(W, H, camera_height_m=1.2, pitch_deg=12.0, hfov_deg=66.0, **options)
        write_pgm(os.path.join(folder, case["name"] + ".pgm"), img)
        r = measure_width(img, cal)
        index.append(
            {
                "name": case["name"],
                "trueWidth": case["road_width_m"],
                "python": {"valid": r.valid, "wide": r.wide, "width": r.width},
            }
        )
    with open(os.path.join(folder, "cases.json"), "w") as f:
        json.dump({"imageToGround": cal.to_ground.flatten().tolist(), "cases": index}, f, indent=2)
    print(f"Wrote {len(index)} cases to {folder}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "cases")
