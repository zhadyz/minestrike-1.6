"""MCW1 voxel world file writer/reader.

Little endian:
  char magic[4] = "MCW1"; int32 version = 1; int32 sx, sy, sz; float origin[3]; float blockSize;
  int32 paletteCount; paletteCount x char name[32] (entry 0 = "air");
  int32 runCount; runCount x { uint32 length; uint16 cell; uint16 pad = 0 }
RLE over flat index (bz*sy + by)*sx + bx. cell = paletteIndex (low 10 bits) | state << 10.
"""
from __future__ import annotations

import struct

import numpy as np


def encode_rle(flat: np.ndarray):
    flat = np.asarray(flat, np.uint16)
    if flat.size == 0:
        return np.zeros(0, np.uint32), np.zeros(0, np.uint16)
    change = np.nonzero(flat[1:] != flat[:-1])[0] + 1
    starts = np.concatenate([[0], change])
    ends = np.concatenate([change, [flat.size]])
    return (ends - starts).astype(np.uint32), flat[starts]


def write_mcw(path: str, cells: np.ndarray, palette: list[str], origin, block_size=40.0):
    """cells: (sz, sy, sx) uint16 (palette index | state << 10)."""
    sz, sy, sx = cells.shape
    assert palette[0] == "air"
    assert len(palette) <= 1024
    lengths, values = encode_rle(cells.reshape(-1))
    with open(path, "wb") as f:
        f.write(b"MCW1")
        f.write(struct.pack("<i", 1))
        f.write(struct.pack("<iii", sx, sy, sz))
        f.write(struct.pack("<fff", *[float(o) for o in origin]))
        f.write(struct.pack("<f", float(block_size)))
        f.write(struct.pack("<i", len(palette)))
        for name in palette:
            b = name.encode("ascii")
            assert len(b) < 32
            f.write(b + b"\0" * (32 - len(b)))
        f.write(struct.pack("<i", len(lengths)))
        rec = np.zeros(len(lengths), dtype=np.dtype([("len", "<u4"), ("cell", "<u2"), ("pad", "<u2")]))
        rec["len"] = lengths
        rec["cell"] = values
        f.write(rec.tobytes())
    return len(lengths)


def read_mcw(path: str):
    with open(path, "rb") as f:
        buf = f.read()
    if buf[:4] != b"MCW1":
        raise ValueError("bad magic")
    ver, sx, sy, sz = struct.unpack_from("<iiii", buf, 4)
    if ver != 1:
        raise ValueError(f"version {ver}")
    origin = struct.unpack_from("<fff", buf, 20)
    bs = struct.unpack_from("<f", buf, 32)[0]
    npal = struct.unpack_from("<i", buf, 36)[0]
    off = 40
    palette = []
    for i in range(npal):
        palette.append(buf[off:off + 32].split(b"\0")[0].decode("ascii"))
        off += 32
    nrun = struct.unpack_from("<i", buf, off)[0]
    off += 4
    rec = np.frombuffer(buf, dtype=np.dtype([("len", "<u4"), ("cell", "<u2"), ("pad", "<u2")]), count=nrun, offset=off)
    off += nrun * 8
    if off != len(buf):
        raise ValueError(f"trailing bytes: {len(buf) - off}")
    total = int(rec["len"].astype(np.int64).sum())
    if total != sx * sy * sz:
        raise ValueError(f"RLE covers {total} cells, expected {sx*sy*sz}")
    if np.any(rec["pad"] != 0):
        raise ValueError("nonzero pad")
    flat = np.repeat(rec["cell"], rec["len"].astype(np.int64))
    cells = flat.reshape(sz, sy, sx)
    if np.any((cells & 0x3FF) >= npal):
        raise ValueError("palette index out of range")
    return dict(size=(sx, sy, sz), origin=origin, block_size=bs, palette=palette, cells=cells, runs=nrun)
