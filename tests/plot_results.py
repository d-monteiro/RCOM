import csv
import os
import matplotlib.pyplot as plt

# ler csv
results = []
with open("results.csv", "r") as f:
    reader = csv.DictReader(f)
    for row in reader:
        row["fer"] = float(row["fer"])
        row["tprop"] = int(row["tprop"])
        row["frame_size"] = int(row["frame_size"])
        row["S_tx"] = float(row["S_tx"])
        row["time"] = float(row["time"])
        results.append(row)

os.makedirs("plots", exist_ok=True)

# constantes
C = 38400  # baudrate bit/s
FILE_SIZE = 10240  # 10KB

# ---- PLOT 1: S vs FER ----
fer_results = [r for r in results if r["test_name"] == "vary_fer"]

# agrupar por FER e fazer media
fer_values = sorted(set(r["fer"] for r in fer_results))
s_means = []
for fer in fer_values:
    vals = [r["S_tx"] for r in fer_results if r["fer"] == fer and r["S_tx"] > 0]
    if vals:
        s_means.append(sum(vals) / len(vals))
    else:
        s_means.append(0)

# curva teorica: S = (1-FER) / (1+2a)
# a = T_prop / T_frame, com tprop=0, a e muito pequeno
# na pratica com tprop=0 o a vem do processing delay
# vamos calcular a a partir do S medido sem erros
s_no_error = s_means[0] if s_means[0] > 0 else 0.5
fer_teorico = []
s_teorico = []
for i in range(0, 51):
    f = i / 100.0
    fer_teorico.append(f)
    s_teorico.append(s_no_error * (1 - f))

plt.figure(figsize=(10, 6))
plt.plot(fer_values, s_means, "bo-", label="S medido", markersize=8)
plt.plot(fer_teorico, s_teorico, "r--", label="S teorico = S0 * (1-FER)")
plt.xlabel("FER (Frame Error Rate)")
plt.ylabel("S (Eficiencia)")
plt.title("Eficiencia vs FER (tprop=0, frame=256)")
plt.legend()
plt.grid(True)
plt.ylim(0, 1)
plt.savefig("plots/s_vs_fer.png", dpi=150)
plt.close()
print("Plot guardado: plots/s_vs_fer.png")

# ---- PLOT 2: S vs T_prop (que corresponde a variar 'a') ----
tprop_results = [r for r in results if r["test_name"] == "vary_tprop"]

tprop_values = sorted(set(r["tprop"] for r in tprop_results))
s_means_tprop = []
for tp in tprop_values:
    vals = [r["S_tx"] for r in tprop_results if r["tprop"] == tp and r["S_tx"] > 0]
    if vals:
        s_means_tprop.append(sum(vals) / len(vals))
    else:
        s_means_tprop.append(0)

# calcular 'a' para cada tprop
# T_frame = frame_size * 8 / C = 256 * 8 / 38400
T_frame = 256 * 8 / C
a_values = [(tp / 1000.0) / T_frame for tp in tprop_values]

# curva teorica: S = 1 / (1 + 2a)
a_teorico = []
s_teorico_a = []
for i in range(0, 200):
    a = i / 10.0
    a_teorico.append(a)
    s_teorico_a.append(1.0 / (1.0 + 2.0 * a))

plt.figure(figsize=(10, 6))
plt.plot(a_values, s_means_tprop, "bo-", label="S medido", markersize=8)
plt.plot(a_teorico, s_teorico_a, "r--", label="S teorico = 1/(1+2a)")
plt.xlabel("a = T_prop / T_frame")
plt.ylabel("S (Eficiencia)")
plt.title("Eficiencia vs a (FER=0, frame=256)")
plt.legend()
plt.grid(True)
plt.ylim(0, 1)
plt.savefig("plots/s_vs_a.png", dpi=150)
plt.close()
print("Plot guardado: plots/s_vs_a.png")

# ---- PLOT 3: S vs Frame Size ----
fsize_results = [r for r in results if r["test_name"] == "vary_framesize"]

fsize_values = sorted(set(r["frame_size"] for r in fsize_results))
s_means_fsize = []
for fs in fsize_values:
    vals = [r["S_tx"] for r in fsize_results if r["frame_size"] == fs and r["S_tx"] > 0]
    if vals:
        s_means_fsize.append(sum(vals) / len(vals))
    else:
        s_means_fsize.append(0)

plt.figure(figsize=(10, 6))
plt.plot(fsize_values, s_means_fsize, "bo-", label="S medido", markersize=8)
plt.xlabel("Frame Size (bytes)")
plt.ylabel("S (Eficiencia)")
plt.title("Eficiencia vs Frame Size (FER=0, tprop=0)")
plt.legend()
plt.grid(True)
plt.ylim(0, 1)
plt.savefig("plots/s_vs_framesize.png", dpi=150)
plt.close()
print("Plot guardado: plots/s_vs_framesize.png")

# ---- PLOT 4: S vs FER para diferentes T_prop ----
combo_results = [r for r in results if r["test_name"] == "fer_x_tprop"]

if combo_results:
    plt.figure(figsize=(10, 6))

    tprops_combo = sorted(set(r["tprop"] for r in combo_results))
    for tp in tprops_combo:
        fers = sorted(set(r["fer"] for r in combo_results if r["tprop"] == tp))
        s_vals = []
        for f in fers:
            vals = [r["S_tx"] for r in combo_results if r["tprop"] == tp and r["fer"] == f and r["S_tx"] > 0]
            if vals:
                s_vals.append(sum(vals) / len(vals))
            else:
                s_vals.append(0)
        plt.plot(fers, s_vals, "o-", label=f"tprop={tp}ms", markersize=8)

    plt.xlabel("FER")
    plt.ylabel("S (Eficiencia)")
    plt.title("Eficiencia vs FER para diferentes T_prop (frame=256)")
    plt.legend()
    plt.grid(True)
    plt.ylim(0, 1)
    plt.savefig("plots/s_vs_fer_tprop.png", dpi=150)
    plt.close()
    print("Plot guardado: plots/s_vs_fer_tprop.png")

print("\nTodos os plots gerados em plots/")
