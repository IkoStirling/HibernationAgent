"""Build the Asterlan character pack from the user's two PixelLab ZIP exports.

Requires Pillow only at import time. Source archives are read-only. Pixel data is
never rescaled: smaller canvases receive transparent padding, and sleeping poses
are translated by whole pixels to share the standing ground line.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
from zipfile import ZipFile

from PIL import Image


class Export:
    def __init__(self, path: Path):
        self.path = path
        self.archive = ZipFile(path)
        try:
            entries = self.archive.infolist()
            if len(entries) > 1024 or len({e.filename for e in entries}) != len(entries):
                raise ValueError("ambiguous or oversized ZIP")
            if sum(e.file_size for e in entries) > 128 * 1024 * 1024:
                raise ValueError("ZIP exceeds import limit")
            if self.archive.getinfo("metadata.json").file_size > 1024 * 1024:
                raise ValueError("oversized metadata")
            self.metadata = json.loads(self.archive.read("metadata.json"))
            if len(self.metadata["states"]) != 1:
                raise ValueError("expected one character state per export")
            self.state = self.metadata["states"][0]
        except Exception:
            self.archive.close()
            raise

    def image(self, name: str) -> Image.Image:
        p = PurePosixPath(name)
        if p.is_absolute() or ".." in p.parts or "\\" in name or ":" in name or p.suffix != ".png":
            raise ValueError("invalid PNG path")
        if self.archive.getinfo(name).file_size > 8 * 1024 * 1024:
            raise ValueError("oversized PNG")
        with Image.open(io.BytesIO(self.archive.read(name))) as image:
            if image.format != "PNG" or not (8 <= image.width <= 512 and 8 <= image.height <= 512):
                raise ValueError("unsupported frame")
            return image.convert("RGBA")

    def animation(self, name: str, direction="south") -> list[str]:
        # Metadata arrays, rather than filename sorting, define frame order.
        frames = self.state["frames"]["animations"][name][direction]
        if not frames or len(frames) > 256:
            raise ValueError("invalid frame count")
        return frames

    def close(self):
        self.archive.close()


def build(idle_path: Path, sleep_path: Path, output: Path, fps=6.0):
    if not 1 <= fps <= 60:
        raise ValueError("fps must be between 1 and 60")
    idle = Export(idle_path)
    try:
        sleeping = Export(sleep_path)
        try:
            return build_pack(idle, sleeping, output, fps)
        finally:
            sleeping.close()
    finally:
        idle.close()


def build_pack(idle: Export, sleeping: Export, output: Path, fps: float):
    framesets = [s.state["frames"] for s in (idle, sleeping)]
    dimensions = []
    for source, groups in zip((idle, sleeping), framesets):
        names = list(groups["rotations"].values())
        for directions in groups["animations"].values():
            for frames in directions.values():
                names.extend(frames)
        dimensions.extend(source.image(name).size for name in names)
    canvas = max(max(size) for size in dimensions)
    if canvas > 512:
        raise ValueError("canvas too large")
    reference = idle.image(idle.state["frames"]["rotations"]["south"])
    reference_pad = (canvas - reference.height) // 2
    ground = reference.getchannel("A").point(lambda a: 255 if a >= 32 else 0).getbbox()[3] + reference_pad
    if output.exists() and any(output.iterdir()):
        manifest = output / "character.json"
        if not manifest.exists() or json.loads(manifest.read_text(encoding="utf-8"))["id"] != "asterlan-pixel":
            raise ValueError("output is not an existing Asterlan pack; refusing overwrite")
    output.mkdir(parents=True, exist_ok=True)
    records = []

    def write(source: Export, name: str, target: str, ground_align=False, dx=0):
        image = source.image(name)
        x, y = (canvas - image.width) // 2 + dx, (canvas - image.height) // 2
        if ground_align:
            bounds = image.getchannel("A").point(lambda a: 255 if a >= 32 else 0).getbbox()
            y = ground - bounds[3]
        bounds = image.getchannel("A").getbbox()
        if not bounds or min(x + bounds[0], y + bounds[1]) < 0 or x + bounds[2] > canvas or y + bounds[3] > canvas:
            raise ValueError("normalization would clip visible pixels")
        normalized = Image.new("RGBA", (canvas, canvas))
        normalized.paste(image, (x, y))
        destination = output / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        normalized.save(destination, optimize=True)
        records.append({"image": target, "archive": source.path.name, "source": name,
                        "source_size": list(image.size), "offset": [x, y]})
        return target

    def clip(source: Export, name: str, direction="south", grounded=False, initial_dx=0):
        files = source.animation(name, direction)
        if not re.fullmatch(r"[a-z0-9_-]+", name):
            raise ValueError("invalid action name")
        result = []
        for index, file in enumerate(files):
            dx = round(initial_dx * (1 - index / max(1, len(files) - 1)))
            target = write(source, file, f"frames/{name}/{direction}/{index:03}.png", grounded, dx)
            # Distribute rounding so 9 frames at 6 FPS total exactly 1500 ms.
            ms = round((index + 1) * 1000 / fps) - round(index * 1000 / fps)
            result.append({"image": target, "ms": ms})
        return result

    for source, folder in ((idle, "standing"), (sleeping, "sleeping")):
        for direction, name in source.state["frames"]["rotations"].items():
            if direction not in {"north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"}:
                raise ValueError("unknown direction")
            write(source, name, f"poses/{folder}/{direction}.png", folder == "sleeping")

    breathing = clip(idle, "breathing")
    blink = clip(idle, "eyes-blind")
    turn = clip(idle, "trun")
    # The user repaired the source export. Preserve its complete turn order;
    # the former one-frame workaround is intentionally removed.
    walking = clip(idle, "walking", "east")
    clip(idle, "walking", "south")  # Retain supplied forward walk for future direction selection.
    quiet = clip(idle, "idle")
    held = clip(idle, "drag-hold")
    landing = clip(idle, "drag-drop")
    # Export starts with five standing anticipation frames. The desktop has
    # already performed the physical drop, so begin at contact/compression.
    landing_contact = landing[5:]
    lie_down = clip(idle, "on-sleep", grounded=True, initial_dx=3)
    wake_up = clip(sleeping, "walk-up", grounded=True, initial_dx=3)
    # No sleep-breathing animation was supplied. Hold the actual authored lying
    # pose; never substitute the unwanted closed-eyes standing preset.
    sleep_pose = [{"image": "poses/sleeping/south.png", "ms": 1200}]
    stand_pose = [{"image": "poses/standing/south.png", "ms": 200}]
    # Normal idle is the user's idle/south clip. Heavy breathing is a separate
    # fatigue pose, never an implicit idle, listening or speaking fallback.
    eating = [dict(frame, hand=[canvas // 2 + 1, ground - 40]) for frame in quiet]
    manifest = {
        "version": 1, "id": "asterlan-pixel", "canvas": [canvas, canvas],
        "anchor": [canvas // 2, ground], "hitbox": [18, 18, canvas - 12, ground + 2],
        "palette": "original",
        "states": {
            "idle": [{"id": "quiet-idle", "loop": quiet}],
            "breathing": [{"id": "tired-breathing", "loop": breathing}],
            "blink": [{"id": "blink", "loop": blink}],
            "walk": [{"id": "right-walk", "start": turn, "loop": walking,
                      "end": list(reversed(turn))}],
            "dragged": [{"id": "suspended", "loop": held}],
            # Temporary side pickup reuses the authored turn in reverse. A
            # dedicated PixelLab pickup clip can replace this start later.
            "dragged_side": [{"id": "side-pickup", "start": [dict(frame, ms=55) for frame in reversed(turn)], "loop": held}],
            "landing": [{"id": "soft-landing", "start": landing_contact, "loop": stand_pose}],
            "resting": [{"id": "lying-sleep", "start": lie_down, "loop": sleep_pose, "end": wake_up}],
            "listening": [{"id": "quiet-attention", "loop": quiet}],
            "eating": [{"id": "holding-food", "loop": eating}],
            "curious": [{"id": "quiet-observation", "loop": quiet}],
        },
        "fallbacks": {"receiving": "listening", "preparing": "listening", "thinking": "preparing", "speaking": "idle"},
    }
    (output / "character.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    report = {
        "format": "pixellab-import-1", "canvas": [canvas, canvas], "ground": ground, "fps": fps,
        "sources": [{"archive": s.path.name, "sha256": hashlib.sha256(s.path.read_bytes()).hexdigest(),
                     "export_version": s.metadata.get("export_version")} for s in (idle, sleeping)],
        "notes": ["Export contains no frame timing; default 6 FPS is confirmed by the user's PixelLab editor screenshot.",
                  "Whole-pixel translation and transparent padding only; no scaling or repainting.",
                  "Sleep loop is the supplied static lying pose. Mouth visemes and dedicated eating motion are not supplied.",
                  "Standing-to-sleep and wake-up are grounded; source PNGs remain in the original ZIPs.",
                  "Return-to-front uses the supplied turn frames in reverse.",
                  "Landing starts at source drag-drop frame 005, omitting five standing anticipation frames after the physical drop; all source frames are retained.",
                  "Side pickup reuses reverse turn frames at 55 ms while the desktop lifts the sprite; a dedicated pickup sequence is not yet supplied.",
                  "All supplied turn frames are used. The previous temporary exclusion of frame 001 was removed after the source was repaired."],
        "frames": records,
    }
    (output / "source.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {"pack": str(output), "canvas": canvas, "ground": ground, "images": len(records)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idle", type=Path, required=True)
    parser.add_argument("--sleep", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fps", type=float, default=6, help="Editor playback rate; the ZIP omits timing (default: 6)")
    args = parser.parse_args()
    print(json.dumps(build(args.idle, args.sleep, args.output, args.fps), ensure_ascii=False))
