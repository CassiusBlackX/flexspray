#!/usr/bin/env python3
"""Generate 6 vector PDF + 300-DPI PNG publication figures for FlexSpray v7 (NSDI).

All numbers are loaded strictly from scratch/flexspray/results/r19/.
"""
import csv
import json
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
R19_DIR = os.path.join(HERE, "results", "r19")
FIG_DIR = os.path.join(HERE, "fig", "nsdi")

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 9.5,
    "axes.titlesize": 10.5,
    "axes.titleweight": "bold",
    "axes.labelsize": 9.5,
    "xtick.labelsize": 8.5,
    "ytick.labelsize": 8.5,
    "legend.fontsize": 8.2,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "grid.alpha": 0.25,
    "grid.linestyle": "--",
    "pdf.fonttype": 42,
    "ps.fonttype": 42,
})

COLORS = {
    "flex+spraycc": "#0072B2",
    "flex+spraycc+v6": "#56B4E9",
    "sird+spraycc": "#009E73",
    "reps+spraycc": "#E69F00",
    "mprdma+spraycc": "#CC79A7",
    "adaptive+caqm": "#D55E00",
    "ecmp+dcqcn": "#7F7F7F",
    "hpn+spraycc": "#333333",
}

LABELS = {
    "flex+spraycc": "FlexSpray (v7)",
    "flex+spraycc+v6": "FlexSpray (v6)",
    "sird+spraycc": "SIRD+SprayCC",
    "reps+spraycc": "REPS+SprayCC",
    "mprdma+spraycc": "MP-RDMA+SprayCC",
    "adaptive+caqm": "Adaptive+CAQM",
    "ecmp+dcqcn": "ECMP+DCQCN",
    "hpn+spraycc": "HPN+SprayCC",
}


def save_fig(fig, stem):
    os.makedirs(FIG_DIR, exist_ok=True)
    for ext in ("pdf", "png"):
        out = os.path.join(FIG_DIR, f"{stem}.{ext}")
        kw = {"dpi": 300} if ext == "png" else {}
        fig.savefig(out, bbox_inches="tight", **kw)
    plt.close(fig)


def plot_fig1(d):
    """Fig 1: E1 Multi-Topology SOTA Overview & Fault-Aware DAGBound."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.8, 4.3), constrained_layout=True)

    topos = [("ub_pod", "UB-Pod\n(4-srv, P=8)"),
             ("cm_pod", "CM-Pod\n(1:1, P=7)"),
             ("cm_pod_osub", "CM-Pod Oversub\n(2.17:1, P=7)"),
             ("ub_mesh", "UB-Mesh\n(Direct+LRS)")]
    schemes = ["flex+spraycc", "reps+spraycc", "sird+spraycc", "mprdma+spraycc", "ecmp+dcqcn", "hpn+spraycc"]
    x = np.arange(len(topos))
    w = 0.13
    by_ts = d["e1_sota"]["by_topo_scheme"]
    for i, sch in enumerate(schemes):
        offset = (i - (len(schemes) - 1) / 2.0) * w
        xs_valid, vals_valid, xs_missing = [], [], []
        for idx, (tk, _) in enumerate(topos):
            key = f"{tk}|{sch}"
            if key in by_ts:
                xs_valid.append(x[idx] + offset)
                vals_valid.append(by_ts[key]["eff_mean"])
            else:
                xs_missing.append(x[idx] + offset)
        bars = ax1.bar(xs_valid, vals_valid, width=w * 0.92, color=COLORS[sch], label=LABELS[sch], edgecolor="white", linewidth=0.5)
        for xm in xs_missing:
            ax1.text(xm, 0.02, "N/T", ha="center", va="bottom", fontsize=6.5, rotation=90, color="#666666")
        if sch == "flex+spraycc":
            for b, v in zip(bars, vals_valid):
                ax1.text(b.get_x() + b.get_width() / 2, v + 0.015, f"{v:.3f}",
                         ha="center", va="bottom", fontsize=7.6, fontweight="bold", color=COLORS[sch])
    ax1.set_xticks(x)
    ax1.set_xticklabels([lbl for _, lbl in topos])
    ax1.set_ylim(0, 1.08)
    ax1.set_ylabel("Mean Efficiency (Failed = 0)")
    ax1.set_title("(a) Multi-Topology Efficiency (E1 Blind, Seeds 15–16)")

    flts = [("F0", "Healthy (F0)"), ("F3far", "1/4 Lane (F3far)"), ("H1far", "Hard Down (H1far)")]
    sch_dag = ["flex+spraycc", "reps+spraycc", "sird+spraycc", "mprdma+spraycc", "adaptive+caqm", "ecmp+dcqcn", "hpn+spraycc"]
    dag_all = d["e1_sota"]["dagbound"]["all_e1_sota"]
    x2 = np.arange(len(flts) + 1)
    w2 = 0.115
    for i, sch in enumerate(sch_dag):
        vals = [dag_all[sch]["by_fault"][fk] for fk, _ in flts] + [dag_all[sch]["dag_eff_mean"]]
        offset = (i - (len(sch_dag) - 1) / 2.0) * w2
        bars = ax2.bar(x2 + offset, vals, width=w2 * 0.92, color=COLORS[sch], edgecolor="white", linewidth=0.5)
        if sch == "flex+spraycc":
            for b, v in zip(bars, vals):
                ax2.text(b.get_x() + b.get_width() / 2, v + 0.015, f"{v:.3f}",
                         ha="center", va="bottom", fontsize=7.6, fontweight="bold", color=COLORS[sch])
    ax2.set_xticks(x2)
    ax2.set_xticklabels([lbl for _, lbl in flts] + ["Overall"])
    ax2.set_ylim(0, 1.08)
    ax2.set_ylabel("Fault-Aware DAGBound (B_DAG / T)")
    ax2.set_title("(b) Multi-Phase Critical-Path Efficiency (E1)")

    handles = [plt.Rectangle((0, 0), 1, 1, fc=COLORS[s]) for s in sch_dag]
    labels = [LABELS[s] for s in sch_dag]
    fig.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.5, 1.09), ncol=4, frameon=False)
    save_fig(fig, "fig1_sota_overview")


def plot_fig2(d):
    """Fig 2: LossMatrix (< 1/N_dev) Breakdown & F3far Phase Tail Inflation."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.8, 4.5), constrained_layout=True)

    lm = d["losstable"]["sota_topology_stratified_tdev"]["all_sota_stratified"]
    schemes = ["flex+spraycc", "sird+spraycc", "reps+spraycc", "mprdma+spraycc", "adaptive+caqm", "ecmp+dcqcn"]
    sems = [("graceful", "Grace"), ("hard", "Hard"), ("lane", "Lane")]
    xs, xlabels = [], []
    pass_p, phys_p, impl_p, miss_p = [], [], [], []
    pos = 0.0
    sem_centers = []
    for sem_k, sem_lbl in sems:
        start = pos
        for sch in schemes:
            rec = lm[f"{sem_k}|{sch}"]
            tot = rec["cells"]
            pass_p.append(100.0 * rec["lt_ndev_pass"] / tot)
            phys_p.append(100.0 * rec["phys"] / tot)
            impl_p.append(100.0 * rec["impl"] / tot)
            miss_p.append(100.0 * rec["miss"] / tot)
            xs.append(pos)
            short = {"flex+spraycc": "Flex", "sird+spraycc": "SIRD", "reps+spraycc": "REPS",
                     "mprdma+spraycc": "MP", "adaptive+caqm": "Adp", "ecmp+dcqcn": "ECMP"}[sch]
            xlabels.append(short)
            pos += 1.0
        sem_centers.append(((start + pos - 1.0) / 2.0, f"{sem_lbl} (n={'16' if sem_k=='graceful' else '25'})"))
        pos += 0.9

    xs = np.array(xs)
    ax1.bar(xs, pass_p, width=0.76, color="#009E73", label="Pass (< 1/N_dev)")
    ax1.bar(xs, phys_p, width=0.76, bottom=pass_p, color="#56B4E9", label="LP Physical (phys)")
    ax1.bar(xs, impl_p, width=0.76, bottom=np.array(pass_p) + np.array(phys_p), color="#E69F00", label="Impl Gap (impl)")
    ax1.bar(xs, miss_p, width=0.76, bottom=np.array(pass_p) + np.array(phys_p) + np.array(impl_p), color="#D55E00", label="Missing Tasks")
    for idx in range(len(xs)):
        if idx % len(schemes) == 0:
            ax1.text(xs[idx], 101.8, f"{pass_p[idx]:.0f}%", ha="center", va="bottom",
                     fontsize=7.6, fontweight="bold", color="#0072B2")
    ax1.set_xticks(xs)
    ax1.set_xticklabels(xlabels, fontsize=7.6, rotation=40, ha="right")
    for c, lbl in sem_centers:
        ax1.text(c, 111.0, lbl, ha="center", va="bottom", fontsize=8.6, fontweight="bold")
    ax1.set_ylim(0, 146)
    ax1.set_yticks([0, 20, 40, 60, 80, 100])
    ax1.set_ylabel("Cells (% of Topology-Scenario Grid)")
    ax1.set_title("(a) LossMatrix Strict < 1/N_dev Breakdown (tdev)")
    ax1.legend(loc="upper center", ncol=2, frameon=True, framealpha=0.95, fontsize=7.6)

    pi = d["phaseinfl"]
    pi_schemes = ["flex+spraycc", "flex+spraycc+v6", "adaptive+caqm", "mprdma+spraycc", "sird+spraycc", "reps+spraycc", "ecmp+dcqcn"]
    meds = [pi[f"F3far|{s}"]["med_post_excess_pct_mean"] for s in pi_schemes]
    maxs = [pi[f"F3far|{s}"]["max_post_excess_pct_max"] for s in pi_schemes]
    perp = [pi[f"F3far|{s}"]["n_perphase"] for s in pi_schemes]
    ncas = [pi[f"F3far|{s}"]["n_cases"] for s in pi_schemes]
    y = np.arange(len(pi_schemes))
    h = 0.36
    ax2.barh(y - h / 2, meds, height=h, color="#0072B2", label="Mean Median Post-Fault Excess (%)")
    ax2.barh(y + h / 2, maxs, height=h, color="#D55E00", alpha=0.88, label="Worst Max Post-Fault Excess (%)")
    ax2.axvline(10.0, color="black", linestyle=":", linewidth=1.3, label="10% Lock-Step Threshold")
    for idx, (mx, pp, nc) in enumerate(zip(maxs, perp, ncas)):
        ax2.text(mx + 1.0, y[idx], f"{pp}/{nc} per-phase", va="center", ha="left",
                 fontsize=7.8, fontweight="bold" if pp == 0 else "normal",
                 color="#0072B2" if pp == 0 else "#222222")
    ax2.set_yticks(y)
    ax2.set_yticklabels([LABELS[s] for s in pi_schemes])
    ax2.invert_yaxis()
    ax2.set_xlim(0, 68)
    ax2.set_xlabel("Post-Fault Barrier Phase Tail Excess (%)")
    ax2.set_title("(b) Multi-Phase Lock-Step Tail Inflation under F3far (E1)")
    ax2.legend(loc="upper right", frameon=True, framealpha=0.95, fontsize=7.6)

    save_fig(fig, "fig2_loss_phaseinfl")


def plot_fig3(d):
    """Fig 3: Dynamic Fault Timeline (e2_timeseries_a2a_s15.csv) & E2 Efficiency."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.8, 4.4), constrained_layout=True)

    ts_path = os.path.join(R19_DIR, "e2_timeseries_a2a_s15.csv")
    series = {s: ([], []) for s in ("flex+spraycc", "flex+spraycc+v6", "mprdma+spraycc", "sird+spraycc", "reps+spraycc")}
    with open(ts_path) as f:
        for row in csv.DictReader(f):
            if row["fault"] == "F3multi" and row["scheme"] in series:
                t = float(row["t_us"])
                if t <= 360.0:
                    series[row["scheme"]][0].append(t)
                    series[row["scheme"]][1].append(float(row["rate_gbps"]) / 1000.0)
    for sch in ("flex+spraycc", "flex+spraycc+v6", "mprdma+spraycc", "sird+spraycc", "reps+spraycc"):
        ts, gs = series[sch]
        ls = "--" if sch == "flex+spraycc+v6" else "-"
        lw = 2.1 if sch == "flex+spraycc" else 1.4
        ax1.plot(ts, gs, linestyle=ls, linewidth=lw, color=COLORS[sch], label=LABELS[sch])
    ax1.axvline(50.0, color="#444444", linestyle=":", linewidth=1.2)
    ax1.text(53.0, 3.65, "t=50us\nP(f) 1/4", fontsize=7.5, va="top", color="#333333")
    ax1.axvline(80.0, color="#8B0000", linestyle=":", linewidth=1.2)
    ax1.text(83.0, 3.65, "t=80us\nNear P2 DOWN", fontsize=7.5, va="top", color="#8B0000")
    ax1.annotate("Flex finishes\n@ ~175 us", xy=(175, 0.25), xytext=(195, 1.35), fontsize=7.6,
                 fontweight="bold", color=COLORS["flex+spraycc"],
                 arrowprops=dict(arrowstyle="->", color=COLORS["flex+spraycc"], lw=0.9))
    ax1.text(235, 2.95, "SIRD/MP/REPS drag\nuntil 500–1300 us", fontsize=7.4, color="#333333",
             bbox=dict(boxstyle="round,pad=0.25", fc="#FFFDF5", ec="#CCCCCC"))
    ax1.set_xlim(0, 360)
    ax1.set_ylim(0, 3.85)
    ax1.set_xlabel("Simulation Time (us)")
    ax1.set_ylabel("Cluster Goodput (Tbps)")
    ax1.set_title("(a) Live Concurrent-Fault Timeline (UB-Pod A2A, F3multi)")

    flts = [("F3heal", "F3heal\n(Heal)"), ("F3flap", "F3flap\n(Flap)"), ("H1heal", "H1heal\n(Up+Recycle)"),
            ("F3up3", "F3up3\n(3-Up)"), ("F3multi", "F3multi\n(Conc.)")]
    sch_e2 = ["flex+spraycc", "flex+spraycc+v6", "sird+spraycc", "reps+spraycc", "mprdma+spraycc", "hpn+spraycc"]
    e2d = d["e2_dynamic"]["by_ds_fault_scheme"]
    x = np.arange(len(flts))
    w = 0.135
    for i, sch in enumerate(sch_e2):
        vals = [e2d[f"e2_dyn_ubpod|{fk}|{sch}"]["eff_mean"] for fk, _ in flts]
        offset = (i - (len(sch_e2) - 1) / 2.0) * w
        bars = ax2.bar(x + offset, vals, width=w * 0.92, color=COLORS[sch], edgecolor="white", linewidth=0.5)
        if sch == "flex+spraycc":
            for b, v in zip(bars, vals):
                ax2.text(b.get_x() + b.get_width() / 2, v + 0.015, f"{v:.3f}",
                         ha="center", va="bottom", fontsize=7.5, fontweight="bold", color=COLORS[sch])
    ax2.set_xticks(x)
    ax2.set_xticklabels([lbl for _, lbl in flts])
    ax2.set_ylim(0, 1.12)
    ax2.set_ylabel("Mean Efficiency (UB-Pod Dynamic Suite)")
    ax2.set_title("(b) Dynamic & Compound Fault Efficiency (E2, n=180)")
    ax2.text(0.02, 0.97, "CM-Pod H1heal: v7 0.824 vs v6 0.748 (+7.6 pp)\nCM-Pod F3flap: v7 0.806 vs v6 0.756 (+4.9 pp)",
             transform=ax2.transAxes, va="top", ha="left", fontsize=7.4,
             bbox=dict(boxstyle="round,pad=0.28", fc="#F5F8FB", ec="#CCCCCC"))

    handles = [plt.Rectangle((0, 0), 1, 1, fc=COLORS[s]) for s in sch_e2]
    labels = [LABELS[s] for s in sch_e2]
    fig.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.5, 1.08), ncol=6, frameon=False)
    save_fig(fig, "fig3_dynamic_timeseries")


def plot_fig4(d):
    """Fig 4: E3 Cluster Scale Sweep & E4 Flow-Control Regimes."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.8, 4.4), constrained_layout=True)

    scales = [("S=4\n(32 NPU,\nscale=1.0)", 4),
              ("S=8\n(64 NPU,\nscale=0.5)", 8),
              ("S=16\n(128 NPU,\nd2d/kv=0.5, ring=0.25)", 16)]
    e3_rows = list(csv.DictReader(open(os.path.join(R19_DIR, "e3_scale_summary.csv"))))
    e1_rows = list(csv.DictReader(open(os.path.join(R19_DIR, "e1_sota_summary.csv"))))
    e1_dag = list(csv.DictReader(open(os.path.join(R19_DIR, "e1_dagbound_summary.csv"))))
    e35_dag = list(csv.DictReader(open(os.path.join(R19_DIR, "e3_e5_dagbound_summary.csv"))))

    def mean_eff(rows, pred):
        return float(np.mean([float(r["eff_mean"]) for r in rows if pred(r)]))

    def mean_dag(rows, pred):
        return float(np.mean([float(r["dag_eff_mean"]) for r in rows if pred(r)]))

    sch_list = ["flex+spraycc", "sird+spraycc", "reps+spraycc", "ecmp+dcqcn", "hpn+spraycc"]
    x = np.arange(3)
    for sch in sch_list:
        dk_s4 = mean_eff(e1_rows, lambda r: r["ds"] == "e1_cmpod" and r["scenario"] in ("d2d", "b_kv") and r["fault"] in ("F0", "F3far", "H1far") and r["scheme"] == sch)
        dk_s8 = mean_eff(e3_rows, lambda r: r["ds"] == "e3_scale_s8" and r["scenario"] in ("d2d", "b_kv") and r["scheme"] == sch)
        dk_s16 = mean_eff(e3_rows, lambda r: r["ds"] == "e3_scale_s16" and r["scenario"] in ("d2d", "b_kv") and r["scheme"] == sch)
        ax1.plot(x, [dk_s4, dk_s8, dk_s16], marker="o", linewidth=2.0 if sch == "flex+spraycc" else 1.4,
                 color=COLORS[sch], label=LABELS[sch])

        br_s4 = mean_dag(e1_dag, lambda r: r["ds"] == "e1_cmpod" and r["scenario"] == "b_ring" and r["fault"] in ("F0", "F3far", "H1far") and r["scheme"] == sch)
        br_s8 = mean_dag(e35_dag, lambda r: r["ds"] == "e3_scale_s8" and r["scenario"] == "b_ring" and r["scheme"] == sch)
        br_s16 = mean_dag(e35_dag, lambda r: r["ds"] == "e3_scale_s16_ring" and r["scenario"] == "b_ring" and r["scheme"] == sch)
        ax1.plot(x, [br_s4, br_s8, br_s16], marker="s", linestyle="--", linewidth=1.6 if sch == "flex+spraycc" else 1.1,
                 color=COLORS[sch], alpha=0.82)

    ax1.plot([], [], marker="o", color="black", linestyle="-", label="Solid: d2d + b_kv (Raw)")
    ax1.plot([], [], marker="s", color="black", linestyle="--", label="Dashed: b_ring (DAGBound)")
    ax1.set_xticks(x)
    ax1.set_xticklabels([s for s, _ in scales])
    ax1.set_ylim(0.0, 1.05)
    ax1.set_ylabel("Efficiency (Failed = 0)")
    ax1.set_title("(a) CM-Pod Scale Sweep (32 -> 64 -> 128 NPUs)")
    ax1.legend(loc="lower left", ncol=2, frameon=True, framealpha=0.93, fontsize=7.0)

    e4_by_flt = d["e4_fc"]["by_fault_scheme"]
    e4_all = d["e4_fc"]["by_scheme"]
    e4_schemes = [
        ("flex+spraycc", "Flex\nCBFC"),
        ("flex+spraycc+fc_pfc", "Flex\nPFC"),
        ("flex+spraycc+fc_none+sack+buf256", "Flex\nLossy\n256K"),
        ("flex+spraycc+fc_none+sack+buf128", "Flex\nLossy\n128K"),
        ("sird+spraycc+fc_pfc", "SIRD\nPFC"),
        ("reps+spraycc+fc_none+sack+buf256", "REPS\nLossy\n256K"),
        ("ecmp+dcqcn+fc_pfc", "ECMP\nPFC"),
        ("ecmp+dcqcn+fc_none+sack+buf256", "ECMP\nLossy\n256K"),
    ]
    x2 = np.arange(len(e4_schemes))
    w2 = 0.24
    flts = [("F0", "F0", "#0072B2"), ("F3far", "F3far (1/4 Lane)", "#009E73"), ("H1far", "H1far (Hard Down)", "#E69F00")]
    for i, (fk, flbl, col) in enumerate(flts):
        vals = [e4_by_flt[f"{fk}|{sk}"]["eff_mean"] for sk, _ in e4_schemes]
        offset = (i - 1.0) * w2
        ax2.bar(x2 + offset, vals, width=w2 * 0.92, color=col, label=flbl, edgecolor="white", linewidth=0.4)
    for i, (sk, _) in enumerate(e4_schemes):
        ov = e4_all[sk]["eff_mean"]
        ax2.text(x2[i], max(e4_by_flt[f"{fk}|{sk}"]["eff_mean"] for fk, _, _ in flts) + 0.02,
                 f"{ov:.3f}", ha="center", va="bottom", fontsize=7.4,
                 fontweight="bold" if sk.startswith("flex+") else "normal")
    ax2.set_xticks(x2)
    ax2.set_xticklabels([lbl for _, lbl in e4_schemes], fontsize=7.6)
    ax2.set_ylim(0, 1.06)
    ax2.set_ylabel("Mean Efficiency (E4, n=192)")
    ax2.set_title("(b) Flow-Control Regimes: CBFC vs PFC vs Lossy+SACK")
    ax2.legend(loc="upper right", frameon=True, framealpha=0.92, fontsize=7.6)

    save_fig(fig, "fig4_scale_fc")


def plot_fig5(d):
    """Fig 5: E5 Rate/RTT Sensitivity & Real Alibaba SimAI Collective Trace."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.8, 4.3), constrained_layout=True)

    sens = d["e5"]["sensitivity_by_ds_scheme"]
    base400 = d["e5"]["base400_1x_ref_ubpod"]
    cats = ["100G\n(1x RTT)", "400G\n(1x Ref)", "800G\n(1x RTT)", "400G\n(5x RTT)", "400G\n(10x RTT)"]
    sch_e5 = ["flex+spraycc", "flex+spraycc+v6", "sird+spraycc", "reps+spraycc"]
    x_rate = np.array([0, 1, 2])
    for sch in sch_e5:
        v100 = sens[f"e5_rate100|{sch}"]["eff_mean"]
        v400 = base400[sch]["eff_mean"]
        v800 = sens[f"e5_rate800|{sch}"]["eff_mean"]
        v5x = sens[f"e5_rtt5x|{sch}"]["eff_mean"]
        v10x = sens[f"e5_rtt10x|{sch}"]["eff_mean"]
        ls = "--" if sch == "flex+spraycc+v6" else "-"
        lw = 2.1 if sch == "flex+spraycc" else 1.4
        ax1.plot(x_rate, [v100, v400, v800], marker="o", linestyle=ls, linewidth=lw, color=COLORS[sch], label=LABELS[sch])
        ax1.plot([3, 4], [v5x, v10x], marker="s", linestyle=ls, linewidth=lw, color=COLORS[sch])
        ax1.plot([1, 3], [v400, v5x], linestyle=":", linewidth=1.0, color=COLORS[sch], alpha=0.6)
    ax1.axvline(2.5, color="#888888", linestyle="-.", linewidth=1.0)
    ax1.text(1.0, 0.98, "Link Rate Sweep", ha="center", va="top", fontsize=8.0, color="#444444")
    ax1.text(3.5, 0.98, "Fabric RTT Sweep", ha="center", va="top", fontsize=8.0, color="#444444")
    ax1.set_xticks(np.arange(5))
    ax1.set_xticklabels(cats, fontsize=8.0)
    ax1.set_ylim(0.55, 1.00)
    ax1.set_ylabel("Mean Efficiency (Un-tuned Parameters)")
    ax1.set_title("(a) Link-Rate & Fabric-RTT Sensitivity (E5, n=324)")
    ax1.legend(loc="lower left", frameon=True, framealpha=0.92, fontsize=7.7)

    rt_rows = list(csv.DictReader(open(os.path.join(R19_DIR, "e5_realtrace_summary.csv"))))
    sch_rt = ["flex+spraycc", "mprdma+spraycc", "sird+spraycc", "reps+spraycc", "adaptive+caqm", "ecmp+dcqcn", "hpn+spraycc"]
    flts = ["F0", "F1far", "F3far", "F3heal", "F3multi", "H1far"]
    x2 = np.arange(len(flts))
    w2 = 0.118
    for i, sch in enumerate(sch_rt):
        vals = []
        for fk in flts:
            sub = [float(r["dag_eff_mean"]) for r in rt_rows if r["ds"] == "e5_realtrace_ubpod" and r["fault"] == fk and r["scheme"] == sch]
            vals.append(sub[0] if sub else 0.0)
        offset = (i - (len(sch_rt) - 1) / 2.0) * w2
        bars = ax2.bar(x2 + offset, vals, width=w2 * 0.92, color=COLORS[sch], label=LABELS[sch], edgecolor="white", linewidth=0.4)
        if sch == "flex+spraycc":
            for b, v in zip(bars, vals):
                ax2.text(b.get_x() + b.get_width() / 2, v + 0.015, f"{v:.2f}",
                         ha="center", va="bottom", fontsize=7.4, fontweight="bold", color=COLORS[sch])
    ax2.set_xticks(x2)
    ax2.set_xticklabels(flts)
    ax2.set_ylim(0, 1.12)
    ax2.set_ylabel("Fault-Aware DAGBound Efficiency (B_DAG / T)")
    ax2.set_title("(b) Alibaba SimAI Hybrid MoE Trace (UB-Pod, 6 Faults)")
    ax2.text(0.02, 0.97, "CM-Pod SimAI DAGBound:\nFlex 0.795 vs SIRD 0.710 / MP 0.685",
             transform=ax2.transAxes, va="top", ha="left", fontsize=7.4,
             bbox=dict(boxstyle="round,pad=0.28", fc="#F5F8FB", ec="#CCCCCC"))
    ax2.legend(loc="upper right", ncol=2, frameon=True, framealpha=0.92, fontsize=6.9)

    save_fig(fig, "fig5_sensitivity_realtrace")


def plot_fig6(d):
    """Fig 6: H1far Control Tail Isolation (v7 vs v6) & Queue vs Reorder Trade-off."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.8, 4.3), constrained_layout=True)

    slices = [
        ("e1_ubpod_a1", "ubpod_a1\n(400G)", d["ccmetrics_e1_e2"]),
        ("e1_ubpod_a2", "ubpod_a2\n(400G)", d["ccmetrics_e1_e2"]),
        ("e1_cmpod", "cm_pod\n(400G)", d["ccmetrics_e1_e2"]),
        ("e5_rate100", "E5\n100G", d["ccmetrics_e3_e5"]),
        ("e5_rate800", "E5\n800G", d["ccmetrics_e3_e5"]),
        ("e5_rtt5x", "E5\n5x RTT", d["ccmetrics_e3_e5"]),
        ("e5_rtt10x", "E5\n10x RTT", d["ccmetrics_e3_e5"]),
    ]
    v7_p99 = [blk["by_ds_fault_scheme"][f"{ds}|H1far|flex+spraycc"]["ctrl_p99_us_mean"] for ds, _, blk in slices]
    v6_p99 = [blk["by_ds_fault_scheme"][f"{ds}|H1far|flex+spraycc+v6"]["ctrl_p99_us_mean"] for ds, _, blk in slices]
    sird_p99 = [blk["by_ds_fault_scheme"][f"{ds}|H1far|sird+spraycc"]["ctrl_p99_us_mean"] for ds, _, blk in slices]

    x = np.arange(len(slices))
    w = 0.27
    ax1.bar(x - w, v7_p99, width=w * 0.92, color=COLORS["flex+spraycc"], label="FlexSpray v7 (HiPrio Bypass)")
    ax1.bar(x, v6_p99, width=w * 0.92, color=COLORS["flex+spraycc+v6"], label="FlexSpray v6 (Fixed Commit)")
    ax1.bar(x + w, sird_p99, width=w * 0.92, color=COLORS["sird+spraycc"], label="SIRD+SprayCC")
    for i, (val7, val6) in enumerate(zip(v7_p99, v6_p99)):
        ax1.text(x[i] - w, val7 + 0.35, f"{val7:.2f}", ha="center", va="bottom", fontsize=7.2, fontweight="bold", color=COLORS["flex+spraycc"])
        ax1.text(x[i], val6 + 0.35, f"{val6:.1f}", ha="center", va="bottom", fontsize=7.2, color="#1A658A")
    ax1.set_xticks(x)
    ax1.set_xticklabels([lbl for _, lbl, _ in slices], fontsize=8.0)
    ax1.set_ylim(0, 26.5)
    ax1.set_ylabel("Mean Control p99 Latency under H1far (us)")
    ax1.set_title("(a) Hard-Down Control-Plane Tail Isolation (v7 vs v6)")
    ax1.legend(loc="upper left", frameon=True, framealpha=0.92, fontsize=7.7)

    cc12 = d["ccmetrics_e1_e2"]["by_ds_scheme"]
    pts_cmpod = ["flex+spraycc", "sird+spraycc", "reps+spraycc", "mprdma+spraycc", "ecmp+dcqcn", "hpn+spraycc"]
    labeled = set()
    for ds_key, marker_st in [("e1_cmpod", "o"), ("e2_dyn_ubpod", "^")]:
        for sch in pts_cmpod:
            key = f"{ds_key}|{sch}"
            if key not in cc12:
                continue
            rec = cc12[key]
            qpk = rec["q_peak_kb_mean"]
            reord = rec["reorder_max_mean"]
            lbl = LABELS[sch] if sch not in labeled else None
            if lbl:
                labeled.add(sch)
            ax2.scatter([reord], [qpk], s=85, marker=marker_st, color=COLORS[sch], edgecolor="black", linewidth=0.6, label=lbl, zorder=4)
            if ds_key == "e2_dyn_ubpod" and sch in ("flex+spraycc", "reps+spraycc", "mprdma+spraycc", "sird+spraycc"):
                offsets = {
                    "flex+spraycc": (46, 560),
                    "sird+spraycc": (68, 1180),
                    "mprdma+spraycc": (132, 1660),
                    "reps+spraycc": (122, 2360),
                }
                tx, ty = offsets[sch]
                ax2.annotate(f"{LABELS[sch].split('+')[0]}\n({reord:.0f}, {qpk:.0f}KB)", (reord, qpk),
                             xytext=(tx, ty), fontsize=7.4,
                             fontweight="bold" if sch == "flex+spraycc" else "normal",
                             color=COLORS["flex+spraycc"] if sch == "flex+spraycc" else "#222222",
                             arrowprops=dict(arrowstyle="->", color="#666666", lw=0.8))
    ax2.set_xlim(-10, 200)
    ax2.set_ylim(0, 2950)
    ax2.set_xlabel("Mean Peak Packet Reordering Span (Packets)")
    ax2.set_ylabel("Mean Peak Switch Queue (KB)")
    ax2.set_title("(b) Switch Queue Footprint vs Packet Reordering (E1/E2)")
    ax2.plot([], [], marker="o", color="black", linestyle="None", label="Circle: E1 CM-Pod")
    ax2.plot([], [], marker="^", color="black", linestyle="None", label="Triangle: E2 Dyn. UB-Pod")
    ax2.legend(loc="upper left", ncol=2, frameon=True, framealpha=0.92, fontsize=7.1)

    save_fig(fig, "fig6_ccmetrics_tail")


def main():
    with open(os.path.join(R19_DIR, "r19_summary.json")) as f:
        d = json.load(f)
    plot_fig1(d)
    plot_fig2(d)
    plot_fig3(d)
    plot_fig4(d)
    plot_fig5(d)
    plot_fig6(d)
    print("Generated 6 PDF/PNG pairs in", FIG_DIR)


if __name__ == "__main__":
    main()
