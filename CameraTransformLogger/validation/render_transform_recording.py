#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

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
    missing = [column for column in ["elapsed_ms", "sample", "label", *MATRIX_COLUMNS] if column not in frame.columns]
    if missing:
        raise ValueError(f"Missing expected columns: {missing}")

    frame["time_s"] = frame["elapsed_ms"] / 1000.0
    for column in MATRIX_COLUMNS:
        frame[column] = frame[column].astype(float)
    return frame


def matrix_from_row(row: pd.Series) -> np.ndarray:
    return row[MATRIX_COLUMNS].to_numpy(dtype=float).reshape((4, 4))


def position_from_matrix(matrix: np.ndarray) -> np.ndarray:
    return matrix[3, 0:3]


def axes_from_matrix(matrix: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    return matrix[0, 0:3], matrix[1, 0:3], matrix[2, 0:3]


def normalize(vector: np.ndarray) -> np.ndarray:
    length = np.linalg.norm(vector)
    if length <= 1e-8:
        return vector
    return vector / length


def axis_line(origin: np.ndarray, vector: np.ndarray, scale: float) -> tuple[list[float], list[float], list[float]]:
    end = origin + normalize(vector) * scale
    return [origin[0], end[0]], [origin[1], end[1]], [origin[2], end[2]]


def path_trace(group: pd.DataFrame, label: str) -> go.Scatter3d:
    matrices = group[MATRIX_COLUMNS].to_numpy(dtype=float).reshape((-1, 4, 4))
    positions = matrices[:, 3, 0:3]
    return go.Scatter3d(
        x=positions[:, 0],
        y=positions[:, 1],
        z=positions[:, 2],
        mode="lines",
        line={"width": 3},
        name=f"{label} path",
    )


def marker_trace(origin: np.ndarray, label: str) -> go.Scatter3d:
    return go.Scatter3d(
        x=[origin[0]],
        y=[origin[1]],
        z=[origin[2]],
        mode="markers",
        marker={"size": 5},
        name=f"{label} origin",
    )


def axis_trace(origin: np.ndarray, vector: np.ndarray, scale: float, name: str, color: str) -> go.Scatter3d:
    x, y, z = axis_line(origin, vector, scale)
    return go.Scatter3d(
        x=x,
        y=y,
        z=z,
        mode="lines",
        line={"color": color, "width": 8},
        name=name,
    )


def frame_axis_trace(origin: np.ndarray, vector: np.ndarray, scale: float, color: str) -> go.Scatter3d:
    x, y, z = axis_line(origin, vector, scale)
    return go.Scatter3d(
        x=x,
        y=y,
        z=z,
        mode="lines",
        line={"color": color, "width": 8},
        showlegend=False,
    )


def render_recording(
    frame: pd.DataFrame,
    labels: list[str] | None,
    out_path: Path,
    frame_stride: int,
    axis_scale: float,
) -> None:
    if labels is None:
        labels = list(frame["label"].drop_duplicates())

    groups = {
        label: group.sort_values("elapsed_ms").reset_index(drop=True)
        for label, group in frame.groupby("label", sort=False)
        if label in labels
    }
    if not groups:
        raise ValueError(f"No requested labels found. Available labels: {sorted(frame['label'].unique())}")

    selected_count = min(len(group) for group in groups.values())
    frame_indices = list(range(0, selected_count, max(1, frame_stride)))
    if frame_indices[-1] != selected_count - 1:
        frame_indices.append(selected_count - 1)

    data: list[go.Scatter3d] = []
    for label, group in groups.items():
        data.append(path_trace(group, label))

    first_index = frame_indices[0]
    for label, group in groups.items():
        matrix = matrix_from_row(group.iloc[first_index])
        origin = position_from_matrix(matrix)
        right, up, forward = axes_from_matrix(matrix)
        data.extend(
            [
                marker_trace(origin, label),
                axis_trace(origin, right, axis_scale, f"{label} right", "red"),
                axis_trace(origin, up, axis_scale, f"{label} up", "green"),
                axis_trace(origin, forward, axis_scale, f"{label} forward", "blue"),
            ]
        )

    animated_frames = []
    path_trace_count = len(groups)
    for index in frame_indices:
        traces: list[go.Scatter3d] = []
        for label, group in groups.items():
            matrix = matrix_from_row(group.iloc[index])
            origin = position_from_matrix(matrix)
            right, up, forward = axes_from_matrix(matrix)
            traces.extend(
                [
                    go.Scatter3d(
                        x=[origin[0]],
                        y=[origin[1]],
                        z=[origin[2]],
                        mode="markers",
                        marker={"size": 5},
                        showlegend=False,
                    ),
                    frame_axis_trace(origin, right, axis_scale, "red"),
                    frame_axis_trace(origin, up, axis_scale, "green"),
                    frame_axis_trace(origin, forward, axis_scale, "blue"),
                ]
            )

        animated_frames.append(
            go.Frame(
                data=traces,
                traces=list(range(path_trace_count, path_trace_count + len(traces))),
                name=str(index),
                layout={"title_text": f"Camera transform recording - sample {index}"},
            )
        )

    all_positions = []
    for group in groups.values():
        matrices = group[MATRIX_COLUMNS].to_numpy(dtype=float).reshape((-1, 4, 4))
        all_positions.append(matrices[:, 3, 0:3])
    positions = np.vstack(all_positions)
    mins = positions.min(axis=0)
    maxs = positions.max(axis=0)
    center = (mins + maxs) / 2.0
    radius = max(float(np.max(maxs - mins)) / 2.0, axis_scale * 2.0, 0.1)

    figure = go.Figure(data=data, frames=animated_frames)
    figure.update_layout(
        title="Camera Transform Recording",
        scene={
            "xaxis": {"title": "x", "range": [center[0] - radius, center[0] + radius]},
            "yaxis": {"title": "y", "range": [center[1] - radius, center[1] + radius]},
            "zaxis": {"title": "z", "range": [center[2] - radius, center[2] + radius]},
            "aspectmode": "cube",
        },
        updatemenus=[
            {
                "type": "buttons",
                "showactive": False,
                "buttons": [
                    {
                        "label": "Play",
                        "method": "animate",
                        "args": [None, {"frame": {"duration": 60, "redraw": True}, "fromcurrent": True}],
                    },
                    {
                        "label": "Pause",
                        "method": "animate",
                        "args": [[None], {"frame": {"duration": 0, "redraw": False}, "mode": "immediate"}],
                    },
                ],
            }
        ],
        sliders=[
            {
                "steps": [
                    {
                        "method": "animate",
                        "label": frame_obj.name,
                        "args": [[frame_obj.name], {"mode": "immediate", "frame": {"duration": 0, "redraw": True}}],
                    }
                    for frame_obj in animated_frames
                ],
                "currentvalue": {"prefix": "sample "},
            }
        ],
        legend={"orientation": "h"},
    )
    figure.write_html(out_path, include_plotlyjs="cdn")


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Render an animated 3D transform recording from player camera matrix CSV data.")
    parser.add_argument("--csv", type=Path, default=default_csv_path(script_dir))
    parser.add_argument("--labels", nargs="+", default=None)
    parser.add_argument("--max-rows", type=int, default=None)
    parser.add_argument("--frame-stride", type=int, default=3)
    parser.add_argument("--axis-scale", type=float, default=0.12)
    parser.add_argument("--out", type=Path, default=script_dir / "camera_transform_recording_render.html")
    args = parser.parse_args()

    frame = load_capture(args.csv, args.max_rows)
    render_recording(frame, args.labels, args.out, args.frame_stride, args.axis_scale)
    print(f"Wrote animated 3D render to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
