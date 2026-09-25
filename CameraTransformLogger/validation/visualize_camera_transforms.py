#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", str(Path(__file__).resolve().parent / ".matplotlib"))

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import plotly.graph_objects as go


MATRIX_COLUMNS = [
    "m00", "m01", "m02", "m03",
    "m10", "m11", "m12", "m13",
    "m20", "m21", "m22", "m23",
    "m30", "m31", "m32", "m33",
]


def default_csv_path(script_dir: Path) -> Path:
    return script_dir / "data" / "camera_transform_readings.csv"


def load_capture(csv_path: Path, max_rows: int | None) -> pd.DataFrame:
    frame = pd.read_csv(csv_path, nrows=max_rows)
    missing = [column for column in ["elapsed_ms", "label", *MATRIX_COLUMNS] if column not in frame.columns]
    if missing:
        raise ValueError(f"Missing expected columns: {missing}")

    frame["time_s"] = frame["elapsed_ms"] / 1000.0
    for column in MATRIX_COLUMNS:
        frame[column] = frame[column].astype(float)
    return frame


def matrices_for(group: pd.DataFrame) -> np.ndarray:
    return group[MATRIX_COLUMNS].to_numpy(dtype=float).reshape((-1, 4, 4))


def row_major_position(matrices: np.ndarray) -> np.ndarray:
    return matrices[:, 3, 0:3]


def row_major_axes(matrices: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    return matrices[:, 0, 0:3], matrices[:, 1, 0:3], matrices[:, 2, 0:3]


def make_translation_png(frame: pd.DataFrame, out_path: Path) -> None:
    fig, axes = plt.subplots(3, 1, figsize=(13, 8), sharex=True)
    axis_names = ["x", "y", "z"]

    for label, group in frame.groupby("label", sort=False):
        matrices = matrices_for(group)
        pos = row_major_position(matrices)
        time = group["time_s"].to_numpy()

        for index, axis in enumerate(axes):
            axis.plot(time, pos[:, index], label=label)
            axis.set_ylabel(axis_names[index])
            axis.grid(True, alpha=0.3)

    axes[-1].set_xlabel("time (s)")
    axes[0].legend(loc="best")
    fig.suptitle("Player Camera Matrix Translation")
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def make_axis_lengths_png(frame: pd.DataFrame, out_path: Path) -> None:
    fig, axis = plt.subplots(figsize=(13, 7))

    for label, group in frame.groupby("label", sort=False):
        matrices = matrices_for(group)
        time = group["time_s"].to_numpy()
        right, up, forward = row_major_axes(matrices)

        axis.plot(time, np.linalg.norm(right, axis=1), label=f"{label} right")
        axis.plot(time, np.linalg.norm(up, axis=1), "--", label=f"{label} up")
        axis.plot(time, np.linalg.norm(forward, axis=1), ":", label=f"{label} forward")

    axis.set_xlabel("time (s)")
    axis.set_ylabel("axis vector length")
    axis.set_title("Matrix Axis Lengths (valid rotation axes should stay near 1)")
    axis.grid(True, alpha=0.3)
    axis.legend(loc="best", ncols=2)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def make_axis_components_png(frame: pd.DataFrame, out_path: Path) -> None:
    fig, axes = plt.subplots(3, 1, figsize=(13, 9), sharex=True)
    axis_names = ["right", "up", "forward"]

    for label, group in frame.groupby("label", sort=False):
        matrices = matrices_for(group)
        time = group["time_s"].to_numpy()
        basis_axes = row_major_axes(matrices)

        for index, axis in enumerate(axes):
            vectors = basis_axes[index]
            axis.plot(time, vectors[:, 0], label=f"{label} x")
            axis.plot(time, vectors[:, 1], "--", label=f"{label} y")
            axis.plot(time, vectors[:, 2], ":", label=f"{label} z")
            axis.set_ylabel(axis_names[index])
            axis.grid(True, alpha=0.3)
            axis.legend(loc="best", ncols=3)

    axes[-1].set_xlabel("time (s)")
    fig.suptitle("Player Camera Rotation Axis Components")
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def make_orthogonality_png(frame: pd.DataFrame, out_path: Path) -> None:
    fig, axis = plt.subplots(figsize=(13, 7))

    for label, group in frame.groupby("label", sort=False):
        matrices = matrices_for(group)
        time = group["time_s"].to_numpy()
        right, up, forward = row_major_axes(matrices)

        axis.plot(time, np.einsum("ij,ij->i", right, up), label=f"{label} right.up")
        axis.plot(time, np.einsum("ij,ij->i", right, forward), "--", label=f"{label} right.forward")
        axis.plot(time, np.einsum("ij,ij->i", up, forward), ":", label=f"{label} up.forward")

    axis.axhline(0.0, color="black", linewidth=0.8)
    axis.set_xlabel("time (s)")
    axis.set_ylabel("dot product")
    axis.set_title("Matrix Axis Orthogonality (valid rotation axes should stay near 0)")
    axis.grid(True, alpha=0.3)
    axis.legend(loc="best", ncols=2)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def make_m33_png(frame: pd.DataFrame, out_path: Path) -> None:
    fig, axis = plt.subplots(figsize=(13, 4))

    for label, group in frame.groupby("label", sort=False):
        axis.plot(group["time_s"].to_numpy(), group["m33"].to_numpy(dtype=float), label=label)

    axis.set_xlabel("time (s)")
    axis.set_ylabel("m33")
    axis.set_title("Player Camera Matrix m33")
    axis.grid(True, alpha=0.3)
    axis.legend(loc="best")
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def make_interactive_3d(frame: pd.DataFrame, out_path: Path, axis_stride: int) -> None:
    figure = go.Figure()

    colors = {
        "right": "red",
        "up": "green",
        "forward": "blue",
    }

    for label, group in frame.groupby("label", sort=False):
        matrices = matrices_for(group)
        pos = row_major_position(matrices)
        right, up, forward = row_major_axes(matrices)

        figure.add_trace(
            go.Scatter3d(
                x=pos[:, 0],
                y=pos[:, 1],
                z=pos[:, 2],
                mode="lines",
                name=f"{label} path",
            )
        )

        for axis_name, vectors in [("right", right), ("up", up), ("forward", forward)]:
            xs: list[float | None] = []
            ys: list[float | None] = []
            zs: list[float | None] = []

            for index in range(0, len(pos), axis_stride):
                origin = pos[index]
                end = origin + vectors[index] * 0.08
                xs.extend([origin[0], end[0], None])
                ys.extend([origin[1], end[1], None])
                zs.extend([origin[2], end[2], None])

            figure.add_trace(
                go.Scatter3d(
                    x=xs,
                    y=ys,
                    z=zs,
                    mode="lines",
                    line={"color": colors[axis_name], "width": 4},
                    name=f"{label} {axis_name}",
                    showlegend=False,
                )
            )

    figure.update_layout(
        title="Player Camera Transform Path (red/right, green/up, blue/forward)",
        scene={
            "xaxis_title": "x",
            "yaxis_title": "y",
            "zaxis_title": "z",
            "aspectmode": "data",
        },
        legend={"orientation": "h"},
    )
    figure.write_html(out_path, include_plotlyjs="cdn")


def print_summary(frame: pd.DataFrame) -> None:
    print(f"Loaded {len(frame)} rows")
    for label, group in frame.groupby("label", sort=False):
        matrices = matrices_for(group)
        pos = row_major_position(matrices)
        right, up, forward = row_major_axes(matrices)
        print(
            f"{label}: samples={len(group)} "
            f"pos_min={pos.min(axis=0)} pos_max={pos.max(axis=0)} "
            f"axis_len_mean="
            f"({np.linalg.norm(right, axis=1).mean():.4f}, "
            f"{np.linalg.norm(up, axis=1).mean():.4f}, "
            f"{np.linalg.norm(forward, axis=1).mean():.4f})"
        )


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Visualize NMS player camera transform matrix captures.")
    parser.add_argument("--csv", type=Path, default=default_csv_path(script_dir))
    parser.add_argument("--max-rows", type=int, default=None)
    parser.add_argument("--axis-stride", type=int, default=25)
    parser.add_argument("--out-dir", type=Path, default=script_dir)
    args = parser.parse_args()

    frame = load_capture(args.csv, args.max_rows)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    make_translation_png(frame, args.out_dir / "camera_translation_candidates.png")
    make_axis_components_png(frame, args.out_dir / "camera_axis_components.png")
    make_axis_lengths_png(frame, args.out_dir / "camera_axis_lengths.png")
    make_orthogonality_png(frame, args.out_dir / "camera_axis_orthogonality.png")
    make_m33_png(frame, args.out_dir / "camera_m33.png")
    make_interactive_3d(frame, args.out_dir / "camera_transform_paths_3d.html", args.axis_stride)
    print_summary(frame)
    print(f"Wrote visualization files to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
