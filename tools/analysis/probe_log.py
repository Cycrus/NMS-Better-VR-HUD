"""Load a FrameProbe CSV into per-tag pandas DataFrames.

    from probe_log import load
    log = load(path)            # dict: tag -> DataFrame
    log["SNAP"].local[...]      # matrix columns are exposed as numpy arrays via helpers below
"""
import sys
from collections import defaultdict

import numpy as np
import pandas as pd

BASE = ["t_us", "thread", "frame"]


def m16(prefix):
    return [f"{prefix}{i}" for i in range(16)]


def m12(prefix):
    return [f"{prefix}{i}" for i in range(12)]


COLUMNS = {
    "SNAP": ["mode", "handle", "slot", "parent", "parent_slot", "type"]
    + m16("local") + m16("world") + m16("pworld") + m16("cam")
    + ["pose_seq", "pose_age_us", "pose_valid"] + m12("pose")
    + m16("plocal") + ["origin0", "origin1", "origin2"],   # FrameProbe 2+
    "AM": ["handle", "caller_rva", "is_main", "want_lock", "status", "replaced"]
    + m16("game") + m16("repl") + m16("cam") + m16("alocal") + m16("aworld") + ["origin0", "origin1", "origin2"],
    "HB": ["caller_rva", "object", "h10", "h14", "a", "b", "c"]
    + [f"vecA{i}" for i in range(4)] + [f"vecB{i}" for i in range(4)] + [f"vecC{i}" for i in range(4)] + ["view_rva"],
    "POSE": ["seq", "result", "valid"] + m12("pose") + m16("cam"),
    "VU0": m16("cam"),
    "VU1": m16("cam"),
    "CHAIN": ["hud", "depth", "handle", "slot", "type", "node"] + m16("local") + m16("world"),
    "VIEW": ["view_rva"] + m16("vcam") + [f"copy{i}" for i in range(20)],
    "NODE": ["hud", "handle", "node", "hex"],
    "MODE": ["mode", "name"],
    "MARK": ["n"],
    "TRACK": ["handle", "index"],
    "REJECT": ["handle"] + m16("local"),
}

HEX_COLUMNS = {"handle", "parent", "caller_rva", "object", "h10", "h14", "view_rva", "hud", "node"}


def load(path):
    rows = defaultdict(list)
    info = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\n").split(",")
            tag = parts[0]
            if tag == "INFO":
                info.append(",".join(parts[4:]))
                continue
            if tag not in COLUMNS and tag not in ("VU0", "VU1"):
                info.append(line.rstrip("\n"))   # plain-text messages from the shared code
                continue
            rows[tag].append(parts[1:])

    out = {"INFO": info}
    for tag, data in rows.items():
        cols = BASE + COLUMNS.get(tag, [])
        width = max(len(r) for r in data)
        if len(cols) < width:
            cols = cols + [f"x{i}" for i in range(width - len(cols))]
        df = pd.DataFrame(data, columns=cols[:width])
        for c in df.columns:
            if c in HEX_COLUMNS:
                df[c] = df[c].map(lambda v: int(v, 16) if isinstance(v, str) and v.startswith("0x") else v)
            elif c not in ("name", "hex"):
                df[c] = pd.to_numeric(df[c], errors="coerce")
        out[tag] = df
    return out


def mats(df, prefix):
    """(N,4,4) row-major matrices from 16 columns."""
    return df[m16(prefix)].to_numpy(dtype=np.float64).reshape(-1, 4, 4)


def poses(df, prefix="pose"):
    """OpenVR 3x4 (column-vector) -> (N,4,4) engine row-vector convention."""
    p = df[m12(prefix)].to_numpy(dtype=np.float64).reshape(-1, 3, 4)
    out = np.zeros((len(p), 4, 4))
    out[:, :3, :3] = np.transpose(p[:, :, :3], (0, 2, 1))
    out[:, 3, :3] = p[:, :, 3]
    out[:, 3, 3] = 1
    return out


if __name__ == "__main__":
    log = load(sys.argv[1])
    for tag, df in log.items():
        print(f"{tag:7} {len(df)}")
