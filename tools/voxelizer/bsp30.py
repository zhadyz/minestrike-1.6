"""GoldSrc BSP v30 + WAD3 reader with vectorised point-contents queries (numpy).

Only what the voxelizer needs: entities, planes, nodes, leafs, clipnodes, models, faces, texinfo,
miptex (names + embedded pixel data), edges/surfedges/vertices.
"""
from __future__ import annotations

import os
import re
import struct

import numpy as np

CONTENTS_EMPTY = -1
CONTENTS_SOLID = -2
CONTENTS_WATER = -3
CONTENTS_SLIME = -4
CONTENTS_LAVA = -5
CONTENTS_SKY = -6
CONTENTS_ORIGIN = -7
CONTENTS_CLIP = -8
CONTENTS_TRANSLUCENT = -15

LUMP_NAMES = [
    "entities", "planes", "textures", "vertices", "visibility", "nodes", "texinfo", "faces",
    "lighting", "clipnodes", "leafs", "marksurfaces", "edges", "surfedges", "models",
]

# Standard GoldSrc hull sizes (mins, maxs) for hulls 0..3
HULL_SIZES = [
    ((0, 0, 0), (0, 0, 0)),
    ((-16, -16, -36), (16, 16, 36)),
    ((-32, -32, -32), (32, 32, 32)),
    ((-16, -16, -18), (16, 16, 18)),
]


def parse_entities(text: str) -> list[dict]:
    ents = []
    for block in re.findall(r"\{(.*?)\}", text, re.S):
        d: dict = {}
        for k, v in re.findall(r'"([^"]*)"\s*"([^"]*)"', block):
            d[k] = v
        ents.append(d)
    return ents


class Miptex:
    __slots__ = ("name", "width", "height", "pixels", "palette")

    def __init__(self, name, width, height, pixels=None, palette=None):
        self.name = name
        self.width = width
        self.height = height
        self.pixels = pixels  # (h, w) uint8 indices of mip 0, or None if external
        self.palette = palette  # (256, 3) uint8 or None

    def rgb(self):
        if self.pixels is None or self.palette is None:
            return None
        return self.palette[self.pixels]

    def average_colour(self):
        img = self.rgb()
        if img is None:
            return None
        if self.name.startswith("{"):
            mask = self.pixels != 255
            if mask.any():
                return img[mask].reshape(-1, 3).mean(axis=0)
        return img.reshape(-1, 3).mean(axis=0)


def _parse_miptex(buf: bytes, off: int) -> Miptex:
    name = buf[off:off + 16].split(b"\0")[0].decode("latin-1")
    w, h = struct.unpack_from("<II", buf, off + 16)
    offs = struct.unpack_from("<4I", buf, off + 24)
    if offs[0] == 0:
        return Miptex(name, w, h)
    pix = np.frombuffer(buf, np.uint8, w * h, off + offs[0]).reshape(h, w).copy()
    # palette follows mip 3 : int16 count then count*3 bytes
    m3 = off + offs[3] + (w // 8) * (h // 8)
    n = struct.unpack_from("<h", buf, m3)[0]
    pal = np.frombuffer(buf, np.uint8, n * 3, m3 + 2).reshape(n, 3).copy()
    if n < 256:
        pal = np.vstack([pal, np.zeros((256 - n, 3), np.uint8)])
    return Miptex(name, w, h, pix, pal)


class Wad3:
    def __init__(self, path: str):
        self.path = path
        with open(path, "rb") as f:
            self.buf = f.read()
        magic, n, ofs = struct.unpack_from("<4sii", self.buf, 0)
        if magic not in (b"WAD3", b"WAD2"):
            raise ValueError(f"{path}: not a WAD3 ({magic!r})")
        self.entries = {}
        for i in range(n):
            filepos, disksize, size, typ, comp, _p1, _p2, name = struct.unpack_from("<iiibbbb16s", self.buf, ofs + i * 32)
            nm = name.split(b"\0")[0].decode("latin-1").lower()
            self.entries[nm] = (filepos, disksize, typ)

    def names(self):
        return list(self.entries.keys())

    def miptex(self, name: str):
        e = self.entries.get(name.lower())
        if e is None or e[2] != 0x43:
            return None
        return _parse_miptex(self.buf, e[0])


class Bsp30:
    def __init__(self, path: str):
        self.path = path
        with open(path, "rb") as f:
            self.buf = buf = f.read()
        ver = struct.unpack_from("<i", buf, 0)[0]
        if ver != 30:
            raise ValueError(f"{path}: BSP version {ver}, expected 30")
        self.lumps = {}
        for i, nm in enumerate(LUMP_NAMES):
            o, l = struct.unpack_from("<ii", buf, 4 + i * 8)
            self.lumps[nm] = (o, l)

        def lump(nm):
            o, l = self.lumps[nm]
            return buf[o:o + l]

        self.entities_text = lump("entities").split(b"\0")[0].decode("latin-1")
        self.entities = parse_entities(self.entities_text)

        pl = np.frombuffer(lump("planes"), dtype=np.dtype([("n", "<f4", 3), ("d", "<f4"), ("t", "<i4")]))
        self.plane_normal = pl["n"].astype(np.float64)
        self.plane_dist = pl["d"].astype(np.float64)
        self.plane_type = pl["t"].copy()

        nd = np.frombuffer(lump("nodes"), dtype=np.dtype([("p", "<i4"), ("c", "<i2", 2), ("mins", "<i2", 3),
                                                          ("maxs", "<i2", 3), ("ff", "<u2"), ("nf", "<u2")]))
        self.node_plane = nd["p"].astype(np.int64)
        self.node_child = nd["c"].astype(np.int64)
        self.node_mins = nd["mins"].copy()
        self.node_maxs = nd["maxs"].copy()

        lf = np.frombuffer(lump("leafs"), dtype=np.dtype([("c", "<i4"), ("vis", "<i4"), ("mins", "<i2", 3),
                                                          ("maxs", "<i2", 3), ("fm", "<u2"), ("nm", "<u2"),
                                                          ("amb", "u1", 4)]))
        self.leaf_contents = lf["c"].astype(np.int64)
        self.leaf_mins = lf["mins"].copy()
        self.leaf_maxs = lf["maxs"].copy()

        cn = np.frombuffer(lump("clipnodes"), dtype=np.dtype([("p", "<i4"), ("c", "<i2", 2)]))
        self.clip_plane = cn["p"].astype(np.int64)
        self.clip_child = cn["c"].astype(np.int64)

        md = np.frombuffer(lump("models"), dtype=np.dtype([("mins", "<f4", 3), ("maxs", "<f4", 3), ("origin", "<f4", 3),
                                                           ("head", "<i4", 4), ("visleafs", "<i4"), ("ff", "<i4"),
                                                           ("nf", "<i4")]))
        self.models = md

        self.vertices = np.frombuffer(lump("vertices"), "<f4").reshape(-1, 3).astype(np.float64)
        self.edges = np.frombuffer(lump("edges"), "<u2").reshape(-1, 2).astype(np.int64)
        self.surfedges = np.frombuffer(lump("surfedges"), "<i4").astype(np.int64)
        ti = np.frombuffer(lump("texinfo"), dtype=np.dtype([("v", "<f4", (2, 4)), ("mip", "<i4"), ("flags", "<i4")]))
        self.texinfo_vecs = ti["v"].astype(np.float64)
        self.texinfo_miptex = ti["mip"].copy()
        self.texinfo_flags = ti["flags"].copy()
        fc = np.frombuffer(lump("faces"), dtype=np.dtype([("p", "<u2"), ("side", "<i2"), ("fe", "<i4"), ("ne", "<i2"),
                                                          ("ti", "<i2"), ("styles", "u1", 4), ("light", "<i4")]))
        self.face_plane = fc["p"].astype(np.int64)
        self.face_side = fc["side"].astype(np.int64)
        self.face_firstedge = fc["fe"].astype(np.int64)
        self.face_numedges = fc["ne"].astype(np.int64)
        self.face_texinfo = fc["ti"].astype(np.int64)
        self.marksurfaces = np.frombuffer(lump("marksurfaces"), "<u2").astype(np.int64)

        # textures
        tb = lump("textures")
        self.miptex: list[Miptex] = []
        if tb:
            n = struct.unpack_from("<i", tb, 0)[0]
            offs = struct.unpack_from(f"<{n}i", tb, 4)
            for o in offs:
                if o < 0:
                    self.miptex.append(Miptex("", 0, 0))
                else:
                    self.miptex.append(_parse_miptex(tb, o))

    # ------------------------------------------------------------------ geometry helpers
    def worldspawn(self) -> dict:
        return self.entities[0]

    def face_vertices(self, f: int) -> np.ndarray:
        fe = self.face_firstedge[f]
        ne = self.face_numedges[f]
        se = self.surfedges[fe:fe + ne]
        idx = np.where(se >= 0, self.edges[np.abs(se), 0], self.edges[np.abs(se), 1])
        return self.vertices[idx]

    def face_normal(self, f: int) -> np.ndarray:
        n = self.plane_normal[self.face_plane[f]].copy()
        if self.face_side[f]:
            n = -n
        return n

    def face_texture_name(self, f: int) -> str:
        return self.miptex[self.texinfo_miptex[self.face_texinfo[f]]].name

    def model_faces(self, m: int) -> range:
        return range(int(self.models["ff"][m]), int(self.models["ff"][m] + self.models["nf"][m]))

    # ------------------------------------------------------------------ point contents
    def point_contents(self, pts: np.ndarray, model: int = 0, hull: int = 0, chunk: int = 1 << 22) -> np.ndarray:
        """Vectorised point contents. pts (N,3). hull 0 uses nodes/leafs, hulls 1-3 clipnodes.
        For submodels the points are taken relative to the model's origin (GoldSrc brush models are
        compiled in world space; origin is only nonzero for origin-brush entities)."""
        pts = np.asarray(pts, dtype=np.float64).reshape(-1, 3)
        out = np.empty(len(pts), np.int64)
        head = int(self.models["head"][model][hull])
        for s in range(0, len(pts), chunk):
            out[s:s + chunk] = self._pc_chunk(pts[s:s + chunk], head, hull)
        return out

    def box_contents_mask(self, mins: np.ndarray, maxs: np.ndarray, model: int = 0) -> np.ndarray:
        """For each axis-aligned box (N,3 mins/maxs) return a bitmask of the hull-0 leaf contents it touches:
        1 = EMPTY, 2 = SOLID, 4 = SKY, 8 = anything else. A single bit means the box is uniform."""
        mins = np.asarray(mins, np.float64)
        maxs = np.asarray(maxs, np.float64)
        n = len(mins)
        cen = (mins + maxs) * 0.5
        half = (maxs - mins) * 0.5
        mask = np.zeros(n, np.uint8)
        head = int(self.models["head"][model][0])

        def bits(contents):
            b = np.full(contents.shape, 8, np.uint8)
            b[contents == CONTENTS_EMPTY] = 1
            b[contents == CONTENTS_SOLID] = 2
            b[contents == CONTENTS_SKY] = 4
            return b

        box = np.arange(n)
        node = np.full(n, head, np.int64)
        if head < 0:
            mask[:] = bits(np.array([self.leaf_contents[-1 - head]]))[0]
            return mask
        while box.size:
            pl = self.node_plane[node]
            nrm = self.plane_normal[pl]
            s = (cen[box] * nrm).sum(axis=1) - self.plane_dist[pl]
            r = (half[box] * np.abs(nrm)).sum(axis=1)
            go0 = (s + r) >= 0  # some part has d >= 0
            go1 = (s - r) < 0   # some part has d < 0
            nb = np.concatenate([box[go0], box[go1]])
            nn = np.concatenate([self.node_child[node[go0], 0], self.node_child[node[go1], 1]])
            leaf = nn < 0
            if leaf.any():
                np.bitwise_or.at(mask, nb[leaf], bits(self.leaf_contents[-1 - nn[leaf]]))
            box = nb[~leaf]
            node = nn[~leaf]
        return mask

    def _pc_chunk(self, P: np.ndarray, head: int, hull: int) -> np.ndarray:
        n = len(P)
        res = np.empty(n, np.int64)
        if hull == 0:
            planes, children = self.node_plane, self.node_child
        else:
            planes, children = self.clip_plane, self.clip_child
        if head < 0:
            if hull == 0:
                res[:] = self.leaf_contents[-1 - head]
            else:
                res[:] = head
            return res
        idx = np.arange(n)
        cur = np.full(n, head, np.int64)
        while idx.size:
            pl = planes[cur]
            nrm = self.plane_normal[pl]
            d = (P[idx] * nrm).sum(axis=1) - self.plane_dist[pl]
            ch = np.where(d >= 0, children[cur, 0], children[cur, 1])
            leaf = ch < 0
            if leaf.any():
                li = idx[leaf]
                if hull == 0:
                    res[li] = self.leaf_contents[-1 - ch[leaf]]
                else:
                    res[li] = ch[leaf]
            keep = ~leaf
            idx = idx[keep]
            cur = ch[keep]
        return res


def load_wads(bsp: Bsp30, search_dirs: list[str]) -> dict:
    """Return {texname_lower: Miptex with pixels} for all textures used by the bsp, from embedded
    data or from the WADs named in worldspawn 'wad' (looked up by basename in search_dirs)."""
    out = {}
    need = set()
    for mt in bsp.miptex:
        if mt.pixels is not None:
            out[mt.name.lower()] = mt
        elif mt.name:
            need.add(mt.name.lower())
    wadkey = bsp.worldspawn().get("wad", "")
    wads = []
    for w in wadkey.replace("\\", "/").split(";"):
        w = w.strip()
        if not w:
            continue
        base = os.path.basename(w)
        for d in search_dirs:
            p = os.path.join(d, base)
            if os.path.exists(p):
                wads.append(p)
                break
    for p in wads:
        try:
            wad = Wad3(p)
        except Exception:
            continue
        for nm in list(need):
            mt = wad.miptex(nm)
            if mt is not None and mt.pixels is not None:
                out[nm] = mt
                need.discard(nm)
    return out
