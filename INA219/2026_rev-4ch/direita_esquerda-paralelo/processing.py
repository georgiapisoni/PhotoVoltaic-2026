"""Clean the PV measurements for one day and generate analysis plots."""

import csv
from datetime import date, datetime
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

BASE_DIR = Path(__file__).resolve().parent
INPUT_FILE = BASE_DIR / "data" / "PV_IV.CSV"
TARGET_DATE = date(2026, 8, 25)
OUTPUT_DIR = BASE_DIR / "output"
CLEANED_FILE = OUTPUT_DIR / f"PV_IV_{TARGET_DATE:%Y-%m-%d}.csv"
PLOTS_DIR = OUTPUT_DIR / "pv_plots"


def filter_csv_by_date(input_file: Path, output_file: Path) -> int:
    """Write rows from TARGET_DATE to output_file and return their count."""
    output_file.parent.mkdir(parents=True, exist_ok=True)

    with input_file.open("r", newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        if not reader.fieldnames or "date" not in reader.fieldnames:
            raise ValueError(f"{input_file} does not contain a 'date' column")

        rows = []
        for line_number, row in enumerate(reader, start=2):
            try:
                row_date = datetime.strptime(row["date"].strip(), "%d/%m/%Y").date()
            except (AttributeError, ValueError) as error:
                raise ValueError(
                    f"Invalid date on line {line_number}: {row.get('date')!r}"
                ) from error

            if row_date == TARGET_DATE:
                rows.append(row)

        with output_file.open("w", newline="", encoding="utf-8") as destination:
            writer = csv.DictWriter(destination, fieldnames=reader.fieldnames)
            writer.writeheader()
            writer.writerows(rows)

    return len(rows)

# ============================================================
# CONFIGURATION
# ============================================================

PANELS = {
    1: "Hexagonal Direita",
    2: "Hexagonal Esquerda",
    3: "Plana Direita",
    4: "Plana Esquerda",
}

# Used only when automatically choosing one representative I-V snapshot.
MIN_LIGHT_PROXY_PCT = 5.0
OC_CURRENT_TOLERANCE_MA = 20.0

# Optional known sensor ceilings. Your current file contains exact 6400 mA
# readings on panel 3, so those are treated as saturated measurements.
SATURATION_LIMITS_MA = {
    3: 6400.0,
}

# Set this to a timestamp if you want a specific instant, e.g.
# MANUAL_TIMESTAMP = "2026-08-25 09:44:00"
MANUAL_TIMESTAMP = None


# ============================================================
# READ + DERIVE ELECTRICAL VARIABLES
# ============================================================

def read_pv_csv(filename):
    df = pd.read_csv(filename)

    df["datetime"] = pd.to_datetime(
        df["date"].astype(str) + " " + df["time"].astype(str),
        dayfirst=True,
        errors="coerce",
    )
    df = df.dropna(subset=["datetime"]).sort_values("datetime").reset_index(drop=True)

    for p in PANELS:
        for state in ("load", "oc", "isc"):
            bus = f"{state}BusV{p}"
            shunt = f"{state}ShuntmV{p}"
            current = f"{state}CurrentmA{p}"

            # INA219 source-side voltage: V+ = bus voltage + shunt drop.
            df[f"{state}V{p}"] = df[bus] + df[shunt] / 1000.0
            df[f"{state}I_A{p}"] = df[current] / 1000.0
            df[f"{state}P_W{p}"] = df[f"{state}V{p}"] * df[f"{state}I_A{p}"]

        df[f"Voc{p}"] = df[f"ocV{p}"]
        df[f"Isc_A{p}"] = df[f"iscI_A{p}"]
        df[f"Vload{p}"] = df[f"loadV{p}"]
        df[f"Iload_A{p}"] = df[f"loadI_A{p}"]
        df[f"Pload_W{p}"] = df[f"loadP_W{p}"]

    return df


# ============================================================
# DATA-QUALITY CHECKS
# ============================================================

def valid_snapshot_mask(df):
    mask = df["ldrPct"].fillna(0) >= MIN_LIGHT_PROXY_PCT

    for p in PANELS:
        mask &= df[f"loadCurrentmA{p}"] > 0
        mask &= df[f"ocCurrentmA{p}"].abs() <= OC_CURRENT_TOLERANCE_MA

        if p in SATURATION_LIMITS_MA:
            limit = SATURATION_LIMITS_MA[p]
            mask &= df[f"iscCurrentmA{p}"] < 0.999 * limit

    return mask


def print_quality_report(df):
    daylight = df["ldrPct"].fillna(0) >= 20

    print("\nDATA QUALITY REPORT")
    print("-------------------")

    for p, name in PANELS.items():
        g = df.loc[daylight].copy()

        median_abs_ioc = g[f"ocCurrentmA{p}"].abs().median()
        ratio = g[f"Isc_A{p}"] / g[f"Iload_A{p}"].replace(0, np.nan)
        median_ratio = ratio.replace([np.inf, -np.inf], np.nan).median()

        sat_count = 0
        if p in SATURATION_LIMITS_MA:
            limit = SATURATION_LIMITS_MA[p]
            sat_count = int((g[f"iscCurrentmA{p}"] >= 0.999 * limit).sum())

        print(
            f"{name}: median |Ioc| = {median_abs_ioc:.2f} mA, "
            f"median Isc/Iload = {median_ratio:.3f}, "
            f"saturated Isc samples = {sat_count}"
        )

        # This is a diagnostic, not a hard deletion rule.
        if pd.notna(median_ratio) and median_ratio < 0.5:
            print(
                f"  WARNING: {name} has Isc much smaller than load current. "
                "That is not physically consistent with an ideal PV I-V curve; "
                "check the short-circuit relay/path, settling delay, wiring, or sensor range."
            )


def choose_snapshot(df):
    if MANUAL_TIMESTAMP is not None:
        target = pd.Timestamp(MANUAL_TIMESTAMP)
        idx = (df["datetime"] - target).abs().idxmin()
        return df.loc[idx]

    candidates = df.loc[valid_snapshot_mask(df)].copy()

    if candidates.empty:
        raise RuntimeError(
            "No snapshot passed the filters. Set MANUAL_TIMESTAMP or relax "
            "MIN_LIGHT_PROXY_PCT / OC_CURRENT_TOLERANCE_MA."
        )

    # Choose the simultaneous instant with the highest total measured load power.
    # This is more robust here than choosing maximum LDR or maximum Isc.
    candidates["_total_load_power"] = sum(
        candidates[f"Pload_W{p}"].clip(lower=0) for p in PANELS
    )

    return candidates.loc[candidates["_total_load_power"].idxmax()]


# ============================================================
# PLOTTING
# ============================================================

def format_time_axis(ax):
    locator = mdates.AutoDateLocator()
    ax.xaxis.set_major_locator(locator)
    ax.xaxis.set_major_formatter(mdates.ConciseDateFormatter(locator))
    ax.grid(True, alpha=0.25)
    ax.legend()
    ax.figure.tight_layout()


def plot_iv_profile(row, outdir):
    """
    IMPORTANT:
    The CSV contains only three intended operating states per timestamp:
        Isc, load point, Voc.
    Therefore this is a three-point I-V PROFILE, not a measured continuous sweep.

    By definition the ideal endpoints are plotted as:
        (0 V, Isc) and (Voc, 0 A)
    with the measured load point between them.
    """
    fig, ax = plt.subplots(figsize=(8, 5.5))

    for p, name in PANELS.items():
        V = np.array([0.0, row[f"Vload{p}"], row[f"Voc{p}"]], dtype=float)
        I = np.array([row[f"Isc_A{p}"], row[f"Iload_A{p}"], 0.0], dtype=float)

        order = np.argsort(V)
        ax.plot(V[order], I[order], marker="o", linestyle="--", label=name)

        # Flag a clearly suspicious short-circuit reading.
        if row[f"Isc_A{p}"] < 0.5 * row[f"Iload_A{p}"]:
            ax.annotate(
                f"{name}: verificar Isc",
                (0.0, row[f"Isc_A{p}"]),
                xytext=(8, 8),
                textcoords="offset points",
                fontsize=8,
            )

    ax.set_title(
        "Perfil I-V de três pontos\n"
        f"{row['datetime']:%Y-%m-%d %H:%M:%S} | LDR = {row['ldrPct']:.1f}%"
    )
    ax.set_xlabel("Tensão fotovoltaica (V)")
    ax.set_ylabel("Corrente fotovoltaica (A)")
    ax.grid(True, alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "01_iv_profile.png", dpi=200)
    plt.close(fig)


def plot_pv_profile(row, outdir):
    """Three-point power-voltage profile using the same intended PV endpoints."""
    fig, ax = plt.subplots(figsize=(8, 5.5))

    for p, name in PANELS.items():
        V = np.array([0.0, row[f"Vload{p}"], row[f"Voc{p}"]], dtype=float)
        P = np.array([0.0, row[f"Pload_W{p}"], 0.0], dtype=float)

        order = np.argsort(V)
        ax.plot(V[order], P[order], marker="o", linestyle="--", label=name)

    ax.set_title(
        "Perfil P-V de três pontos\n"
        f"{row['datetime']:%Y-%m-%d %H:%M:%S}"
    )
    ax.set_xlabel("Tensão fotovoltaica (V)")
    ax.set_ylabel("Potência elétrica (W)")
    ax.grid(True, alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "02_pv_profile.png", dpi=200)
    plt.close(fig)


def plot_by_day(df, y_columns, ylabel, title, filename, outdir):
    fig, ax = plt.subplots(figsize=(10, 5))

    days = list(df["datetime"].dt.date.unique())

    for p, name in PANELS.items():
        for j, day in enumerate(days):
            g = df.loc[df["datetime"].dt.date == day]
            y = g[y_columns[p]].copy()

            # Hide known saturation values when plotting Isc.
            if y_columns[p] == f"Isc_A{p}" and p in SATURATION_LIMITS_MA:
                lim_A = SATURATION_LIMITS_MA[p] / 1000.0
                y = y.mask(y >= 0.999 * lim_A)

            ax.plot(
                g["datetime"],
                y,
                linewidth=1,
                label=name if j == 0 else "_nolegend_",
            )

    ax.set_title(title)
    ax.set_xlabel("Horário")
    ax.set_ylabel(ylabel)
    format_time_axis(ax)
    fig.savefig(outdir / filename, dpi=200)
    plt.close(fig)


def plot_light_proxy(df, outdir):
    fig, ax = plt.subplots(figsize=(10, 4.5))

    for day in df["datetime"].dt.date.unique():
        g = df.loc[df["datetime"].dt.date == day]
        ax.plot(g["datetime"], g["ldrPct"], linewidth=1)

    ax.set_title("Indicador de luminosidade LDR ao longo do tempo")
    ax.set_xlabel("Horário")
    ax.set_ylabel("Luminosidade LDR (%)")
    ax.grid(True, alpha=0.25)

    locator = mdates.AutoDateLocator()
    ax.xaxis.set_major_locator(locator)
    ax.xaxis.set_major_formatter(mdates.ConciseDateFormatter(locator))
    fig.tight_layout()
    fig.savefig(outdir / "06_light_proxy_time.png", dpi=200)
    plt.close(fig)


def plot_power_vs_light(df, outdir):
    fig, ax = plt.subplots(figsize=(8, 5.5))

    mask = df["ldrPct"] >= MIN_LIGHT_PROXY_PCT

    for p, name in PANELS.items():
        ax.scatter(
            df.loc[mask, "ldrPct"],
            df.loc[mask, f"Pload_W{p}"],
            s=12,
            alpha=0.45,
            label=name,
        )

    ax.set_title("Potência no ponto de carga em função da luminosidade")
    ax.set_xlabel("Indicador de luminosidade LDR (%)")
    ax.set_ylabel("Potência no ponto de carga (W)")
    ax.grid(True, alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "07_power_vs_light.png", dpi=200)
    plt.close(fig)


def plot_state_power_over_time(df, outdir):
    """Plot measured power for the Isc, Voc and load states over time."""
    states = (
        ("isc", "Curto-circuito (Isc)"),
        ("oc", "Circuito aberto (Voc)"),
        ("load", "Carga"),
    )
    fig, axes = plt.subplots(3, 1, figsize=(12, 11), sharex=True)

    for ax, (state, state_name) in zip(axes, states):
        for p, panel_name in PANELS.items():
            power = df[f"{state}P_W{p}"].copy()

            # Do not plot measurements at the known current-sensor ceiling.
            if state == "isc" and p in SATURATION_LIMITS_MA:
                limit = SATURATION_LIMITS_MA[p]
                power = power.mask(df[f"iscCurrentmA{p}"] >= 0.999 * limit)

            ax.plot(df["datetime"], power, linewidth=1, label=panel_name)

        ax.axhline(0, color="black", linewidth=0.7, alpha=0.5)
        ax.set_title(f"Potência medida — {state_name}")
        ax.set_ylabel("Potência (W)")
        ax.grid(True, alpha=0.25)
        ax.legend(ncol=2, fontsize=8)

    locator = mdates.AutoDateLocator()
    axes[-1].xaxis.set_major_locator(locator)
    axes[-1].xaxis.set_major_formatter(mdates.ConciseDateFormatter(locator))
    axes[-1].set_xlabel("Horário")
    fig.suptitle(
        "Potência em função do tempo nos três estados de medição",
        fontsize=14,
    )
    fig.tight_layout()
    fig.savefig(outdir / "08_potencia_estados_tempo.png", dpi=200)
    plt.close(fig)


def save_snapshot_summary(row, outdir):
    rows = []

    for p, name in PANELS.items():
        rows.append(
            {
                "panel": name,
                "timestamp": row["datetime"],
                "light_proxy_pct": row["ldrPct"],
                "Voc_V": row[f"Voc{p}"],
                "Isc_A": row[f"Isc_A{p}"],
                "Vload_V": row[f"Vload{p}"],
                "Iload_A": row[f"Iload_A{p}"],
                "Pload_W": row[f"Pload_W{p}"],
                # Useful hardware/data-quality diagnostic only.
                "Isc_over_Iload": (
                    row[f"Isc_A{p}"] / row[f"Iload_A{p}"]
                    if row[f"Iload_A{p}"] != 0
                    else np.nan
                ),
            }
        )

    summary = pd.DataFrame(rows)
    summary.to_csv(outdir / "snapshot_summary.csv", index=False)

    print("\nSELECTED SNAPSHOT")
    print("-----------------")
    print(summary.to_string(index=False, float_format=lambda x: f"{x:.4f}"))


# ============================================================
# MAIN
# ============================================================

def main():
    row_count = filter_csv_by_date(INPUT_FILE, CLEANED_FILE)
    print(
        f"Saved {row_count} rows for {TARGET_DATE:%d/%m/%Y} "
        f"to {CLEANED_FILE}"
    )
    if row_count == 0:
        raise RuntimeError(f"No measurements found for {TARGET_DATE:%d/%m/%Y}")

    PLOTS_DIR.mkdir(parents=True, exist_ok=True)

    # All calculations and plots use the cleaned, single-day CSV.
    df = read_pv_csv(CLEANED_FILE)

    print(f"Loaded {len(df)} rows")
    print(f"Time range: {df['datetime'].min()} -> {df['datetime'].max()}")

    print_quality_report(df)

    snapshot = choose_snapshot(df)
    print(f"\nAutomatically selected snapshot: {snapshot['datetime']}")

    save_snapshot_summary(snapshot, PLOTS_DIR)
    plot_iv_profile(snapshot, PLOTS_DIR)
    plot_pv_profile(snapshot, PLOTS_DIR)

    plot_by_day(
        df,
        {p: f"Voc{p}" for p in PANELS},
        "$V_{OC}$ (V)",
        "Tensão de circuito aberto ao longo do tempo",
        "03_voc_time.png",
        PLOTS_DIR,
    )

    plot_by_day(
        df,
        {p: f"Isc_A{p}" for p in PANELS},
        "$I_{SC}$ (A)",
        "Corrente de curto-circuito ao longo do tempo",
        "04_isc_time.png",
        PLOTS_DIR,
    )

    plot_by_day(
        df,
        {p: f"Pload_W{p}" for p in PANELS},
        "Potência no ponto de carga (W)",
        "Potência medida no ponto de carga ao longo do tempo",
        "05_load_power_time.png",
        PLOTS_DIR,
    )

    plot_light_proxy(df, PLOTS_DIR)
    plot_power_vs_light(df, PLOTS_DIR)
    plot_state_power_over_time(df, PLOTS_DIR)

    print(f"\nSaved plots to: {PLOTS_DIR}")


if __name__ == "__main__":
    main()
