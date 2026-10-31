# Writes the Tx line marker icons for STD: a biohazard symbol in a "toxic"
# glowing green for the selected line and a dim outline for the others.
# Three crescents (arcs open away from the centre) around a ring, drawn as
# strokes so Qt's SVG Tiny renderer needs no masks or filters. The glow is
# the same shape drawn wider and translucent underneath.
#   python make-biohazard.py   (writes into this folder)
import math
import pathlib

HERE = pathlib.Path(__file__).resolve().parent


def lobe(theta_deg, dist=21.0, radius=19.0, opening=70.0):
    """An arc of a circle centred `dist` out along theta, open on the outside."""
    t = math.radians(theta_deg)
    cx, cy = dist * math.cos(t), dist * math.sin(t)
    a1 = math.radians(theta_deg + opening / 2)
    a2 = math.radians(theta_deg - opening / 2)
    x1, y1 = cx + radius * math.cos(a1), cy + radius * math.sin(a1)
    x2, y2 = cx + radius * math.cos(a2), cy + radius * math.sin(a2)
    # the long way round, through the inner side of the lobe
    return f"M{x1:.2f},{y1:.2f} A{radius},{radius} 0 1 1 {x2:.2f},{y2:.2f}"


ANGLES = (-90.0, 30.0, 150.0)  # up, lower right, lower left (SVG y points down)
LOBES = " ".join(lobe(a) for a in ANGLES)
RING = "M0,-12 A12,12 0 1 1 -0.01,-12"


def svg(main, glow, glow_opacity, width, ring_width, centre):
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="-50 -50 100 100" width="64" height="64">
  <g fill="none" stroke-linecap="round">
    <path d="{LOBES}" stroke="{glow}" stroke-opacity="{glow_opacity}" stroke-width="{width + 9}"/>
    <path d="{RING}" stroke="{glow}" stroke-opacity="{glow_opacity}" stroke-width="{ring_width + 8}"/>
    <path d="{LOBES}" stroke="{main}" stroke-width="{width}"/>
    <path d="{RING}" stroke="{main}" stroke-width="{ring_width}"/>
  </g>
  <circle cx="0" cy="0" r="5" fill="{centre}"/>
</svg>
"""


(HERE / "biohazard-on.svg").write_text(svg("#9dff2e", "#4dff00", 0.35, 8, 6, "#0c141c"), encoding="utf-8")
(HERE / "biohazard-off.svg").write_text(svg("#3a4d3c", "#3a4d3c", 0.0, 6, 5, "#0c141c"), encoding="utf-8")
(HERE / "biohazard-hover.svg").write_text(svg("#5f8f4a", "#5f8f4a", 0.0, 7, 5, "#0c141c"), encoding="utf-8")
print("wrote biohazard-on.svg, biohazard-off.svg, biohazard-hover.svg")
