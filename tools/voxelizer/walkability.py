"""Walkability / connectivity analysis of the voxel world vs the BSP's player hull."""
from __future__ import annotations

import numpy as np

HEAD = 4  # half cells of clearance a 72-unit player needs (4 x 20 = 80 >= 72)


def standable(Hs: np.ndarray) -> np.ndarray:
    """Hs: (nzh, sy, sx) True = blocked half cell. Node (h,y,x) = feet at the bottom of half cell h."""
    nzh = Hs.shape[0]
    air = ~Hs
    S = np.zeros_like(Hs)
    clear = np.ones_like(Hs)
    for k in range(HEAD):
        sh = np.zeros_like(Hs)
        sh[: nzh - k] = air[k:]
        clear &= sh
    S[1:] = Hs[:-1] & clear[1:]
    return S


def _air_run(air: np.ndarray, lo_off: int, hi_off: int) -> np.ndarray:
    """R[h] = air at all half cells h+lo_off .. h+hi_off (inclusive), False when out of range above? (out of
    range above the grid counts as air)."""
    nzh = air.shape[0]
    R = np.ones_like(air)
    for k in range(lo_off, hi_off + 1):
        sh = np.ones_like(air)  # beyond top = open sky
        if k >= 0:
            sh[: nzh - k] = air[k:]
        else:
            sh[-k:] = air[: nzh + k]
            sh[:-k] = False
        R &= sh
    return R


def landing_map(Hs: np.ndarray):
    """land[h,y,x] = feet height where a player box released at h ends up after falling (-1 if the box
    does not fit at h)."""
    nzh = Hs.shape[0]
    air = ~Hs
    box_ok = _air_run(air, 0, HEAD - 1)
    land = np.full(Hs.shape, -1, np.int32)
    land[0] = np.where(box_ok[0], 0, -1)
    for h in range(1, nzh):
        land[h] = np.where(box_ok[h], np.where(Hs[h - 1], h, land[h - 1]), -1)
    return land, box_ok


def reachability(Hs: np.ndarray, seeds: list[tuple[int, int, int]], max_up: int = 1, return_pred: bool = False):
    """BFS over standing nodes. From a standing node a player can move to a 4-neighbour column after rising
    u = 0..max_up half cells in place (u=1: 20-unit step, walkable with stepsize 24; u=2: jump (45u);
    u=3: crouch-jump (~63u)); in the neighbour column the box must fit at the raised height and the player
    then falls to the floor below. Returns reached (nzh, sy, sx) bool."""
    nzh, sy, sx = Hs.shape
    air = ~Hs
    land, box_ok = landing_map(Hs)
    up_clear = {u: _air_run(air, HEAD, HEAD + u - 1) for u in range(1, max_up + 1)}
    S = standable(Hs)
    reached = np.zeros(Hs.shape, bool)
    fh, fy, fx = [], [], []
    for (h, y, x) in seeds:
        if 0 <= h < nzh and S[h, y, x] and not reached[h, y, x]:
            reached[h, y, x] = True
            fh.append(h)
            fy.append(y)
            fx.append(x)
    fh, fy, fx = np.array(fh, int), np.array(fy, int), np.array(fx, int)
    while len(fh):
        nh, ny, nx = [], [], []
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            y2 = fy + dy
            x2 = fx + dx
            inb = (y2 >= 0) & (y2 < sy) & (x2 >= 0) & (x2 < sx)
            for u in range(0, max_up + 1):
                h2 = fh + u
                ok = inb & (h2 < nzh)
                if u > 0:
                    ok &= up_clear[u][np.minimum(fh, nzh - 1), fy, fx]
                idx = np.nonzero(ok)[0]
                if len(idx) == 0:
                    continue
                hl = land[h2[idx], y2[idx], x2[idx]]
                good = hl >= 0
                idx = idx[good]
                hl = hl[good]
                nh.append(hl)
                ny.append(y2[idx])
                nx.append(x2[idx])
        if not nh:
            break
        nh = np.concatenate(nh)
        ny = np.concatenate(ny)
        nx = np.concatenate(nx)
        new = ~reached[nh, ny, nx]
        nh, ny, nx = nh[new], ny[new], nx[new]
        if len(nh) == 0:
            break
        key = np.unique((nh * sy + ny) * sx + nx)
        nh = key // (sy * sx)
        ny = (key // sx) % sy
        nx = key % sx
        reached[nh, ny, nx] = True
        fh, fy, fx = nh, ny, nx
    return reached & S


def legit_mask(G, shape, origin, cls="crouch"):
    """Voxel nodes that correspond to BSP-reachable standing spots (same/adjacent column within 16u, +-1
    half cell)."""
    ox, oy, oz = origin
    nzh, sy, sx = shape
    L = np.zeros(shape, bool)
    m = G["reach"][cls]
    hf = (G["z"][m] - 36 - oz) / 20.0
    h0 = np.round(hf).astype(int)
    for ddx in (-16, 16):
        for ddy in (-16, 16):
            bx = np.floor((G["x"][m] + ddx - ox) / 40).astype(int)
            by = np.floor((G["y"][m] + ddy - oy) / 40).astype(int)
            ok = (bx >= 0) & (bx < sx) & (by >= 0) & (by < sy)
            for dh in (-1, 0, 1):
                h = np.clip(h0 + dh, 0, nzh - 1)
                L[h[ok], by[ok], bx[ok]] = True
    return L


def coverage(G, R, origin, cls="walk"):
    """Fraction of BSP-reachable nodes (class cls) that have a reached voxel node in a column overlapped by
    the player's 32x32 footprint, within +-1 half cell. Returns (fraction, covered mask over all nodes)."""
    ox, oy, oz = origin
    nzh, sy, sx = R.shape
    ok = np.zeros(len(G["x"]), bool)
    h0 = np.round((G["z"] - 36 - oz) / 20.0).astype(int)
    for ddx in (-16, 16):
        for ddy in (-16, 16):
            bx = np.floor((G["x"] + ddx - ox) / 40).astype(int)
            by = np.floor((G["y"] + ddy - oy) / 40).astype(int)
            inb = (bx >= 0) & (bx < sx) & (by >= 0) & (by < sy)
            for dh in (-1, 0, 1):
                h = np.clip(h0 + dh, 0, nzh - 1)
                ok[inb] |= R[h[inb], by[inb], bx[inb]]
    m = G["reach"][cls]
    return float(ok[m].mean()), ok


def block_escapes(H, seeds_fn, legit, log=print, max_up=3, min_cluster=20, max_iter=40):
    """Stop players from jumping/crouch-jumping out of dust2's playable space (onto the open-sky plateau,
    roofs, rock slopes that are too steep to stand on in GoldSrc). Voxel nodes reachable with crouch-jumps
    that have no BSP counterpart and belong to a large connected group are 'escapes'; for every such node
    adjacent to a legit reached node we raise its column so that the climb from the legit node is >= 80
    units (beyond a crouch-jump). Mutates H; returns changes."""
    from scipy import ndimage as ndi
    nzh, sy, sx = H.shape
    changes = []
    for it in range(max_iter):
        R = reachability(H, seeds_fn(H), max_up=max_up)
        X = R & ~legit
        if not X.any():
            break
        g = X.any(axis=0)
        lab, n = ndi.label(g, structure=np.ones((3, 3)))
        sizes = ndi.sum(X.sum(axis=0), lab, range(1, n + 1))
        big = np.zeros(n + 1, bool)
        big[1:] = sizes >= min_cluster
        EX = X & big[lab][None, :, :]
        if not EX.any():
            break
        LR = R & legit
        hz, hy, hx = np.nonzero(EX)
        target_of = {}
        floor_of = {}
        for h, y, x in zip(hz.tolist(), hy.tolist(), hx.tolist()):
            target = None
            for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                y2, x2 = y + dy, x + dx
                if not (0 <= y2 < sy and 0 <= x2 < sx):
                    continue
                hs = np.nonzero(LR[:, y2, x2])[0]
                hs = hs[hs >= h - max_up]
                if len(hs):
                    t = int(hs.max()) + 4
                    target = t if target is None else max(target, t)
            if target is not None and target > h:
                key = (y, x)
                target_of[key] = max(target_of.get(key, target), target)
                floor_of[key] = min(floor_of.get(key, h), h)
        n_cols = 0
        for (y, x), target in target_of.items():
            h = floor_of[(y, x)]
            top = min(target, nzh)
            H[h:top, y, x] = True
            changes.append((int(h), int(top), int(y), int(x)))
            n_cols += 1
        log(f"    escape block iter {it}: {int(EX.sum())} escape nodes, raised {n_cols} columns")
        if n_cols == 0:
            break
    return changes


def node_at(S_or_R: np.ndarray, origin, x: float, y: float, z_feet: float, tol_half: int = 3):
    """Nearest node (h, by, bx) in mask at world (x,y) with floor closest to z_feet."""
    ox, oy, oz = origin
    bx = int(np.floor((x - ox) / 40.0))
    by = int(np.floor((y - oy) / 40.0))
    h0 = int(round((z_feet - oz) / 20.0))
    nzh = S_or_R.shape[0]
    best = None
    for dh in sorted(range(-tol_half, tol_half + 1), key=abs):
        h = h0 + dh
        if 0 <= h < nzh and 0 <= by < S_or_R.shape[1] and 0 <= bx < S_or_R.shape[2] and S_or_R[h, by, bx]:
            best = (h, by, bx)
            break
    return best


def bsp_standing_samples(bsp, origin, sx, sy, zlo, zhi, step=2.0):
    """For every block column centre, scan hull 1 (standing player) contents in z and return the list of
    standing origins (x, y, z_origin) where the hull is free and the hull 2 units below is blocked."""
    ox, oy, oz = origin
    xs = ox + (np.arange(sx) + 0.5) * 40
    ys = oy + (np.arange(sy) + 0.5) * 40
    zs = np.arange(zlo, zhi, step)
    X, Y = np.meshgrid(xs, ys)
    out = []
    for i in range(0, X.size, 2000):
        px = X.ravel()[i:i + 2000]
        py = Y.ravel()[i:i + 2000]
        n = len(px)
        P = np.stack([np.repeat(px, len(zs)), np.repeat(py, len(zs)), np.tile(zs, n)], 1)
        c = bsp.point_contents(P, 0, 1).reshape(n, len(zs))
        free = c == -1
        # free at k and blocked at k-1
        st = free[:, 1:] & ~free[:, :-1]
        a, k = np.nonzero(st)
        for ai, ki in zip(a.tolist(), k.tolist()):
            out.append((px[ai], py[ai], zs[ki + 1]))
    return np.array(out)


def bsp_reach_graph(bsp, x0, y0, nx, ny, spacing, zlo, zhi, seeds_xyz, log=print, solid_models=()):
    """Reachability in the original BSP using hull 1 (standing player, includes clip brushes).

    Nodes: standing origins found by scanning z at column centres on a `spacing` grid. Edges to the 4
    neighbouring columns when the hull can rise in place to max(z1,z2), slide across, and drop down to z2
    (all checked with hull-1 point contents every 4 units). Edge classes by rise dz: <=18 walk,
    <=45 jump, <=63 crouch-jump. Returns dict with node arrays and reach masks per class."""
    xs = x0 + (np.arange(nx) + 0.5) * spacing
    ys = y0 + (np.arange(ny) + 0.5) * spacing
    zs = np.arange(zlo, zhi, 2.0)
    X, Y = np.meshgrid(xs, ys)  # (ny, nx)
    hmins = np.array([-16.0, -16.0, -36.0])
    hmaxs = np.array([16.0, 16.0, 36.0])

    def pc1(P):
        """hull-1 contents of the world plus solid brush entities (crates)."""
        c = bsp.point_contents(P, 0, 1)
        for m in solid_models:
            mn = bsp.models["mins"][m] + hmins - 1
            mx = bsp.models["maxs"][m] + hmaxs + 1
            inb = np.all((P >= mn) & (P <= mx), axis=1) & (c == -1)
            if inb.any():
                cm = bsp.point_contents(P[inb], m, 1)
                sub = c[inb]
                sub[cm == -2] = -2
                c[inb] = sub
        return c

    Xf, Yf = X.ravel(), Y.ravel()
    node_col = []
    node_z = []
    for i in range(0, Xf.size, 3000):
        px = Xf[i:i + 3000]
        py = Yf[i:i + 3000]
        n = len(px)
        P = np.stack([np.repeat(px, len(zs)), np.repeat(py, len(zs)), np.tile(zs, n)], 1)
        c = pc1(P).reshape(n, len(zs))
        free = c == -1
        st = free[:, 1:] & ~free[:, :-1]
        a, k = np.nonzero(st)
        node_col.append(i + a)
        node_z.append(zs[k + 1])
    node_col = np.concatenate(node_col)
    node_z = np.concatenate(node_z)
    nn = len(node_col)
    ci = node_col % nx
    cj = node_col // nx
    # per column list of nodes
    order = np.argsort(node_col, kind="stable")
    col_sorted = node_col[order]
    starts = np.searchsorted(col_sorted, np.arange(nx * ny))
    ends = np.searchsorted(col_sorted, np.arange(nx * ny), side="right")
    E1, E2, DZ = [], [], []
    for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        ti = ci + di
        tj = cj + dj
        ok = (ti >= 0) & (ti < nx) & (tj >= 0) & (tj < ny)
        src = np.nonzero(ok)[0]
        tcol = tj[src] * nx + ti[src]
        cnt = ends[tcol] - starts[tcol]
        rep = np.repeat(src, cnt)
        offs = np.concatenate([np.arange(s, e) for s, e in zip(starts[tcol], ends[tcol])]) if len(src) else np.zeros(0, int)
        dst = order[offs]
        dz = node_z[dst] - node_z[rep]
        keep = dz <= 64
        E1.append(rep[keep])
        E2.append(dst[keep])
        DZ.append(dz[keep])
    e1 = np.concatenate(E1)
    e2 = np.concatenate(E2)
    dz = np.concatenate(DZ)
    # path check points
    z1 = node_z[e1]
    z2 = node_z[e2]
    zc = np.maximum(z1, z2) + 1.0
    p1 = np.stack([X.ravel()[node_col[e1]], Y.ravel()[node_col[e1]]], 1)
    p2 = np.stack([X.ravel()[node_col[e2]], Y.ravel()[node_col[e2]]], 1)
    good = np.ones(len(e1), bool)
    # horizontal slide at zc
    for t in np.linspace(0, 1, int(spacing // 4) + 1):
        q = p1 + (p2 - p1) * t
        c = pc1(np.stack([q[:, 0], q[:, 1], zc], 1))
        good &= c == -1
    # rise in column 1 (z1 -> zc) and drop in column 2 (zc -> z2)
    for k in range(1, 17):
        za = np.minimum(z1 + 4.0 * k, zc)
        c = pc1(np.stack([p1[:, 0], p1[:, 1], za], 1))
        good &= c == -1
        zb = np.minimum(z2 + 4.0 * k, zc)
        c = pc1(np.stack([p2[:, 0], p2[:, 1], zb], 1))
        good &= c == -1
    e1, e2, dz = e1[good], e2[good], dz[good]
    from scipy.sparse import csr_matrix
    from scipy.sparse.csgraph import breadth_first_order
    # seeds: nearest node to each seed position
    seed_nodes = []
    for (sx_, sy_, sz_) in seeds_xyz:
        d = (X.ravel()[node_col] - sx_) ** 2 + (Y.ravel()[node_col] - sy_) ** 2 + ((node_z - sz_) * 2) ** 2
        seed_nodes.append(int(np.argmin(d)))
    reach = {}
    for name, lim in (("walk", 18.5), ("jump", 45.5), ("crouch", 63.5)):
        m = dz <= lim
        A = csr_matrix((np.ones(m.sum(), np.int8), (e1[m], e2[m])), shape=(nn, nn))
        r = np.zeros(nn, bool)
        for s in seed_nodes:
            if not r[s]:
                o = breadth_first_order(A, s, directed=True, return_predecessors=False)
                r[o] = True
        reach[name] = r
    log(f"  BSP hull-1 graph: {nn} standing nodes, {len(e1)} edges; reachable from T spawn: walk {reach['walk'].sum()}, "
        f"jump {reach['jump'].sum()}, crouch-jump {reach['crouch'].sum()}")
    return dict(x=X.ravel()[node_col], y=Y.ravel()[node_col], z=node_z, reach=reach, e1=e1, e2=e2, dz=dz)


def map_bsp_nodes(G, S, origin, tol=2):
    """Map BSP graph nodes to voxel standable nodes (h, by, bx) in the same block column (closest floor
    within +-tol half cells); -1 where none."""
    ox, oy, oz = origin
    nzh, sy, sx = S.shape
    bx = np.floor((G["x"] - ox) / 40.0).astype(int)
    by = np.floor((G["y"] - oy) / 40.0).astype(int)
    hf = (G["z"] - 36 - oz) / 20.0
    h0 = np.round(hf).astype(int)
    out = np.full(len(bx), -1)
    for dh in sorted(range(-tol, tol + 1), key=abs):
        h = h0 + dh
        ok = (out < 0) & (h >= 0) & (h < nzh) & (bx >= 0) & (bx < sx) & (by >= 0) & (by < sy)
        idx = np.nonzero(ok)[0]
        hit = S[h[idx], by[idx], bx[idx]]
        out[idx[hit]] = h[idx[hit]]
    return out, by, bx


def step_repair(H, G, origin, log=print, max_iter=8):
    """Make every BSP walk edge (rise <= 18u, source walk-reachable in dust2) walkable in the voxel world:
    where the voxel floors of the two block columns differ by >= 40u, raise the lower floor by a half cell
    (bottom slab / slab->full) if headroom allows, else lower the higher floor by a half cell.
    Mutates H (half-cell solid grid). Returns list of changes."""
    m = (G["dz"] <= 18.5) & G["reach"]["walk"][G["e1"]]
    e1 = G["e1"][m]
    e2 = G["e2"][m]
    changes = []
    nzh, sy, sx = H.shape
    for it in range(max_iter):
        S = standable(H)
        h, by, bx = map_bsp_nodes(G, S, origin)
        ok = (h[e1] >= 0) & (h[e2] >= 0) & ((by[e1] != by[e2]) | (bx[e1] != bx[e2]))
        a = e1[ok]
        b = e2[ok]
        dh = h[b] - h[a]
        bad = dh >= 2
        a, b = a[bad], b[bad]
        done = set()
        n = 0
        for ia, ib in zip(a.tolist(), b.tolist()):
            ka = (by[ia], bx[ia])
            kb = (by[ib], bx[ib])
            if ka in done or kb in done:
                continue
            ha, hb = h[ia], h[ib]
            ya, xa = ka
            yb, xb = kb
            # raise column a (lower floor) by one half cell?
            if ha + HEAD < nzh and not H[ha, ya, xa] and not H[ha + HEAD, ya, xa] and all(not H[ha + k, ya, xa] for k in range(HEAD + 1)):
                H[ha, ya, xa] = True
                changes.append(("raise", int(ha), int(ya), int(xa)))
                done.add(ka)
                n += 1
            elif hb - 2 >= 0 and H[hb - 1, yb, xb] and H[hb - 2, yb, xb]:
                H[hb - 1, yb, xb] = False
                changes.append(("lower", int(hb - 1), int(yb), int(xb)))
                done.add(kb)
                n += 1
        log(f"    step repair iter {it}: {n} columns adjusted ({int(bad.sum())} too-high BSP walk edges)")
        if n == 0:
            break
    return changes


SHAPE_CUBE, SHAPE_SLAB, SHAPE_DOOR, SHAPE_CROSS = 1, 2, 4, 6


def cell_boxes(shape: int, state: int):
    """Local boxes (mins, maxs) in block units, mirroring mc_world.h semantics."""
    T = 3.0 / 16.0
    if shape == SHAPE_CUBE:
        return [((0, 0, 0), (1, 1, 1))]
    if shape == SHAPE_SLAB:
        return [((0, 0, 0.5), (1, 1, 1))] if state & 1 else [((0, 0, 0), (1, 1, 0.5))]
    if shape == SHAPE_DOOR:
        f = state & 3
        opened = bool(state & 4)
        hr = bool(state & 16)
        s = (f + (3 if hr else 1)) % 4 if opened else f
        return [[((1 - T, 0, 0), (1, 1, 1)), ((0, 1 - T, 0), (1, 1, 1)), ((0, 0, 0), (T, 1, 1)), ((0, 0, 0), (1, T, 1))][s]]
    return []


def box_hits_world(cells, shape_of_pal, origin, mins, maxs, eps=1.0 / 1024):
    """True if the world-space box penetrates any voxel box by more than eps."""
    ox, oy, oz = origin
    sz, sy, sx = cells.shape
    lo = np.floor((np.array(mins) - [ox, oy, oz]) / 40.0).astype(int)
    hi = np.floor((np.array(maxs) - [ox, oy, oz]) / 40.0).astype(int)
    for bz in range(lo[2], hi[2] + 1):
        for by in range(lo[1], hi[1] + 1):
            for bx in range(lo[0], hi[0] + 1):
                if not (0 <= bx < sx and 0 <= by < sy and 0 <= bz < sz):
                    continue
                c = int(cells[bz, by, bx])
                pal = c & 0x3FF
                if pal == 0:
                    continue
                for bmn, bmx in cell_boxes(shape_of_pal[pal], c >> 10):
                    wmn = np.array([ox + (bx + bmn[0]) * 40, oy + (by + bmn[1]) * 40, oz + (bz + bmn[2]) * 40])
                    wmx = np.array([ox + (bx + bmx[0]) * 40, oy + (by + bmx[1]) * 40, oz + (bz + bmx[2]) * 40])
                    pen = np.minimum(np.array(maxs), wmx) - np.maximum(np.array(mins), wmn)
                    if np.all(pen > eps):
                        return True
    return False
