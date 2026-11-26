#!/usr/bin/env python3
"""Parametric case generator for the Pocket recorder. Writes print-ready STLs + previews.

  pip install manifold3d trimesh matplotlib
  python3 hardware/case/generate.py            # all variants -> hardware/case/stl/
  python3 hardware/case/generate.py slim       # one variant

Coordinates (mm): x = width, y = height (portrait), z = thickness; back face at z = 0, front
face at z = D. The REC/MODE edge is +x, which is the TOP edge when held sideways.
Every size is a parameter in VARIANTS: measure your parts and adjust before printing.
"""

from __future__ import annotations

import math
import sys
from dataclasses import dataclass, field, replace
from pathlib import Path

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold, set_circular_segments

set_circular_segments(64)
OUT = Path(__file__).resolve().parent / "stl"


@dataclass
class CaseParams:
    name: str
    W: float = 58.0             # width (portrait)
    H: float = 88.0             # height (portrait)
    D: float = 11.0             # total thickness
    R: float = 7.0              # corner radius (plan view)
    wall: float = 1.2
    back_t: float = 1.4         # back plate (MagSafe ring recessed into it)
    front_t: float = 1.2
    chamfer: float = 0.0        # front-edge chamfer for the "pebble" look
    screen: tuple = (25.0, 50.0, 0.0, 10.0)      # window w, h, x, y
    ring_y: float = 15.0        # MagSafe ring centre (29 mm below the top edge on 88 mm)
    ring_od: float = 55.6       # magnet ring sticker + 0.6 clearance
    ring_id: float = 45.4
    ring_depth: float = 0.8
    camera: tuple | None = (0.0, 33.0, 6.6)      # x, y, hole diameter (back)
    ldr: tuple | None = (8.5, 33.0, 3.4)
    rec_y: float = -27.0        # REC dimple on the +x edge (top-right when held sideways)
    mode_y: float = 16.0        # MODE groove on the +x edge
    grip_lines: bool = True
    usb: tuple = (0.0, 9.8, 4.4)                 # x, slot width, slot height (bottom edge)
    usb_z: float | None = None  # centre height of the USB slot; None = middle of the cavity
    mic_x: float = -9.0
    switch_x: float = -18.0
    vents: bool = False
    boss_inset: float = 4.6
    notes: str = ""


VARIANTS = {
    "slim": CaseParams(
        "slim", notes="Main design. 58x88x11. Needs a ~4 mm flat LiPo and the camera moved off the "
                      "Sense board on a flex extension."),
    "pebble": CaseParams(
        "pebble", R=13.0, chamfer=1.6,
        notes="Same internals as slim, softer look: large corner radius and chamfered front edge."),
    "thin": CaseParams(
        "thin", D=9.5, camera=None, ldr=None,
        notes="No camera. 9.5 mm. For the plain XIAO ESP32S3 + separate mic board build (no Sense board)."),
    "chunky": CaseParams(
        "chunky", W=64.0, H=94.0, D=17.0, R=8.0, ring_y=17.0, camera=(0.0, -32.0, 7.0), ldr=(9.0, -32.0, 3.4),
        rec_y=-28.0, mode_y=18.0, usb=(0.0, 10.5, 5.5), vents=True,
        notes="Fits today's parts unmodified (XIAO+Sense with camera attached, 1300 mAh cell up to "
              "10 mm). For bring-up and first field tests."),
}


# ----------------------------------------------------------------- primitives

def rrect(w: float, h: float, r: float, x: float = 0.0, y: float = 0.0) -> CrossSection:
    r = min(r, w / 2 - 0.01, h / 2 - 0.01)
    return CrossSection.square((w - 2 * r, h - 2 * r), center=True).offset(r, JoinType.Round).translate((x, y))


def slab(cs: CrossSection, z0: float, z1: float) -> Manifold:
    return Manifold.extrude(cs, z1 - z0).translate((0, 0, z0))


def cyl(d: float, z0: float, z1: float, x: float = 0.0, y: float = 0.0) -> Manifold:
    return Manifold.cylinder(z1 - z0, d / 2, d / 2).translate((x, y, z0))


def box(sx: float, sy: float, sz: float, cx: float, cy: float, cz: float) -> Manifold:
    return Manifold.cube((sx, sy, sz), center=True).translate((cx, cy, cz))


def bosses(p: CaseParams):
    # Keep each 5.8 mm boss inside the rounded corner: its centre must sit at least
    # (R - wall - 3.2) from the corner arc's centre along the diagonal.
    i = max(p.boss_inset, p.R - (p.R - p.wall - 3.2) / math.sqrt(2))
    return [(sx * (p.W / 2 - i), sy * (p.H / 2 - i)) for sx in (-1, 1) for sy in (-1, 1)]


# ----------------------------------------------------------------- parts

def shell(p: CaseParams) -> Manifold:
    """Front + side walls. Bottom (z = back_t) is open; the back plate screws on."""
    z0, z1 = p.back_t, p.D
    outer = rrect(p.W, p.H, p.R)
    if p.chamfer > 0:
        c = p.chamfer
        body = Manifold.hull(slab(outer, z0, z1 - c) + slab(rrect(p.W - 2 * c, p.H - 2 * c, p.R - c), z1 - 0.01, z1))
    else:
        body = slab(outer, z0, z1)
    cavity = slab(rrect(p.W - 2 * p.wall, p.H - 2 * p.wall, p.R - p.wall), z0 - 1, z1 - p.front_t)
    s = body - cavity

    # screen window
    sw, sh, sx, sy = p.screen
    s -= slab(rrect(sw, sh, 2.5, sx, sy), z1 - p.front_t - 1, z1 + 1)
    # grip lines on the lower third (where the right hand holds it sideways)
    if p.grip_lines:
        for y in (-27.0, -30.5, -34.0, -37.5):
            s -= box(30.0, 0.7, 0.8, 0.0, y, z1)
    # REC dimple and MODE groove on the +x edge, mid-height of the walls
    zc = (z0 + z1) / 2
    s -= Manifold.sphere(3.6).translate((p.W / 2 + 2.7, p.rec_y, zc))
    s -= box(1.2, 12.0, 1.6, p.W / 2, p.mode_y, zc)
    # bottom edge: USB-C, mic hole, switch slot
    ux, uw, uh = p.usb
    uz = p.usb_z if p.usb_z is not None else (z0 + z1 - p.front_t) / 2
    usb = Manifold.extrude(rrect(uw, uh, uh / 2 - 0.01), 2 * p.wall + 2).rotate((90, 0, 0))
    s -= usb.translate((ux, -p.H / 2 + p.wall + 1, uz))
    s -= Manifold.cylinder(2 * p.wall + 2, 0.8, 0.8).rotate((90, 0, 0)).translate((p.mic_x, -p.H / 2 + p.wall + 1, uz))
    s -= box(7.4, 2 * p.wall + 2, 3.2, p.switch_x, -p.H / 2, uz)
    # side vents (chunky dev case)
    if p.vents:
        for k in range(5):
            s -= box(2 * p.wall + 2, 1.6, 6.0, -p.W / 2, -10.0 + k * 4.0, zc)
    # screw bosses for M2 heat-set inserts (3.2 mm hole, 4 mm deep from the back)
    for bx, by in bosses(p):
        s += cyl(5.8, z0, z1 - p.front_t + 0.01, bx, by) - cyl(3.2, z0 - 0.1, z0 + 4.0, bx, by)
    return s


def back(p: CaseParams) -> Manifold:
    b = slab(rrect(p.W, p.H, p.R), 0.0, p.back_t)
    # MagSafe ring recess, open to the outside so the ring sits flush against the phone
    ring = CrossSection.circle(p.ring_od / 2) - CrossSection.circle(p.ring_id / 2)
    b -= slab(ring.translate((0, p.ring_y)), -0.1, p.ring_depth)
    b -= box(5.6, 7.6, 2 * p.ring_depth, 0.0, p.ring_y - (p.ring_od / 2 + 3.4), 0.0)  # alignment tab
    for hole in (p.camera, p.ldr):
        if hole:
            b -= cyl(hole[2], -1, p.back_t + 1, hole[0], hole[1])
    for bx, by in bosses(p):  # M2 clearance + counterbore for flat heads
        b -= cyl(2.4, -1, p.back_t + 1, bx, by)
        b -= cyl(4.2, -1, 0.6, bx, by)
    # locating lip that drops inside the shell walls (cut away around the bosses)
    gap = 0.25
    lip_out = rrect(p.W - 2 * p.wall - 2 * gap, p.H - 2 * p.wall - 2 * gap, p.R - p.wall - gap)
    lip_in = rrect(p.W - 2 * p.wall - 2 * gap - 2.4, p.H - 2 * p.wall - 2 * gap - 2.4, p.R - p.wall - gap - 1.2)
    lip = slab(lip_out - lip_in, p.back_t, p.back_t + 1.6)
    for bx, by in bosses(p):
        lip -= cyl(7.0, p.back_t - 1, p.back_t + 3, bx, by)
    return b + lip


def desk_stand(p: CaseParams = VARIANTS["slim"], tilt_deg: float = 15.0) -> Manifold:
    """Holds the recorder sideways, screen to you, camera to the front of the room. Grips it
    with a steel ring sticker (55/45 mm) in the recess; the centre is open for the camera."""
    width = 70.0
    base = slab(rrect(width, 64.0, 4.0), 0.0, 4.0).translate((0, 14, 0))
    plate_h = p.W + 6.0
    ring_z = 6.0 + p.W / 2  # device rests on the lip; its ring centre is W/2 above that edge
    plate = box(width, 3.2, plate_h, 0, 0, plate_h / 2)
    ring = CrossSection.circle(27.8) - CrossSection.circle(22.2)
    recess = Manifold.extrude(ring, 1.0).rotate((90, 0, 0)).translate((0, -0.6, ring_z))
    cam_hole = Manifold.cylinder(6.0, 11.0, 11.0).rotate((90, 0, 0)).translate((0, 3.0, ring_z))
    lip = box(width, 6.0, 6.0, 0, -4.0, 3.0)  # the device's long edge rests here
    support = (plate - recess - cam_hole + lip).rotate((-tilt_deg, 0, 0)).translate((0, 18, 3.5))
    braces = Manifold()
    for x in (-width / 2 + 5, width / 2 - 5):  # behind the plate (+y), base to plate
        braces += Manifold.hull(box(5, 1, 1, x, 44, 4.5) + box(5, 1, 1, x, 28.5, 34))
    return base + support + braces


def shirt_clip() -> Manifold:
    """Clips to a pocket, collar or lanyard; the recorder snaps on magnetically (steel ring
    sticker in the recess). Print in PETG, flat side down."""
    plate = slab(rrect(58.0, 58.0, 8.0), 0.0, 2.4)
    ring = CrossSection.circle(27.8) - CrossSection.circle(22.2)
    plate -= slab(ring, 1.6, 2.5)
    plate -= cyl(16.0, -1, 3, 0, 0)  # camera window
    tongue = box(18.0, 44.0, 1.8, 0, 2.0, -2.9)
    bend = (Manifold.cylinder(18.0, 3.2, 3.2) - Manifold.cylinder(18.2, 1.4, 1.4).translate((0, 0, -0.1)))
    bend = bend.rotate((0, 90, 0)).translate((-9.0, 24.0, -0.8))
    bend = bend - box(20.0, 4.0, 8.0, 0, 21.5, -0.8)  # keep only the upper half-loop
    bump = Manifold.cylinder(18.0, 1.0, 1.0).rotate((0, 90, 0)).translate((-9.0, -19.0, -1.9))
    return plate + tongue + bend + bump


# ----------------------------------------------------------------- export + preview

def to_trimesh(m: Manifold) -> trimesh.Trimesh:
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))


def export(m: Manifold, name: str, flip: bool = False) -> trimesh.Trimesh:
    t = to_trimesh(m.rotate((180, 0, 0)) if flip else m)
    t.apply_translation(-t.bounds[0])  # sit on the bed at the origin
    OUT.mkdir(parents=True, exist_ok=True)
    t.export(OUT / f"{name}.stl")
    return t


def preview(meshes: list[tuple[str, trimesh.Trimesh]], path: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection

    fig = plt.figure(figsize=(4.2 * len(meshes), 4.2))
    light = np.array([0.4, -0.5, 0.75])
    light /= np.linalg.norm(light)
    for i, (title, t) in enumerate(meshes):
        ax = fig.add_subplot(1, len(meshes), i + 1, projection="3d", proj_type="ortho")
        v, f = trimesh.remesh.subdivide_to_size(t.vertices, t.faces, max_edge=2.5)
        t = trimesh.Trimesh(v, f, process=False)
        shade = np.clip(t.face_normals @ light, 0, 1) * 0.6 + 0.35
        colors = np.stack([shade * 0.55, shade * 0.56, shade * 0.53, np.ones_like(shade)], axis=1)
        ax.add_collection3d(Poly3DCollection(t.vertices[t.faces], facecolors=colors, linewidths=0))
        mn, mx = t.bounds
        c, r = (mn + mx) / 2, (mx - mn).max() / 2
        ax.set_xlim(c[0] - r, c[0] + r); ax.set_ylim(c[1] - r, c[1] + r); ax.set_zlim(c[2] - r, c[2] + r)
        ax.view_init(elev=32, azim=-62)
        ax.set_box_aspect((1, 1, 1))
        ax.set_axis_off()
        ax.set_title(title, fontsize=10)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


def build(names: list[str]) -> None:
    for n in names:
        p = VARIANTS[n]
        s, b = shell(p), back(p)
        assert s.status().name == "NoError" and b.status().name == "NoError", n
        ts = export(s, f"{n}_shell", flip=True)  # print front-face down: smooth screen side
        tb = export(b, f"{n}_back")              # print outside-face down: flat MagSafe face
        assembled = to_trimesh(s + b)
        preview([(f"{n}: front", assembled), (f"{n}: shell (print pose)", ts), (f"{n}: back", tb)],
                OUT / f"{n}_preview.png")
        print(f"{n:8s} {p.W:.0f} x {p.H:.0f} x {p.D:.1f} mm  shell {ts.volume / 1000:.1f} cm3, "
              f"back {tb.volume / 1000:.1f} cm3, watertight={ts.is_watertight and tb.is_watertight}")
    if not names or set(names) == set(VARIANTS):
        st = export(desk_stand(), "desk_stand")
        cl = export(shirt_clip(), "shirt_clip")
        preview([("desk stand", st), ("shirt clip", cl)], OUT / "accessories_preview.png")
        print(f"accessories: stand {st.volume / 1000:.1f} cm3, clip {cl.volume / 1000:.1f} cm3")


if __name__ == "__main__":
    build(sys.argv[1:] or list(VARIANTS))
