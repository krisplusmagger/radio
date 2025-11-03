import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import RangeSlider, Button

# --- load magnitude CSV you saved earlier ---
mag = np.loadtxt("csi_1khz_mag_denoised_10_28_num_0_4.csv", delimiter=",")  # shape: [T, Nsub]
# mag = np.loadtxt("csi_butt_9_23_C_num_0.csv", delimiter=",")  # shape: [T, Nsub]
# mag = np.loadtxt("csi_mag_9_23_num_0.csv", delimiter=",")  # shape: [T, Nsub]

# mag = np.loadtxt("csi_1khz_mag_smoothed_9_23_num_0.csv", delimiter=",")  # shape: [T, Nsub]
T, N = mag.shape

# limits
KMAX_CAP = min(60, N)         # max subcarriers we pre-create (for speed)
INIT_K   = min(10, KMAX_CAP)  # initial number shown

# optional: set your CSI sampling rate (Hz). If unknown, it falls back to samples
fs = None  # e.g., fs = 1000.0

# x-axis (time or sample index)
if fs is not None:
    x_full = np.arange(T) / fs
    xlabel = "Time (s)"
else:
    x_full = np.arange(T)
    xlabel = "Sample index"

# ---- figure & main axes ----
plt.rcParams["figure.dpi"] = 150
fig, ax = plt.subplots()
plt.subplots_adjust(bottom=0.28)  # leave room for two sliders + buttons

# Pre-create line objects for up to KMAX_CAP subcarriers
lines = []
for k in range(KMAX_CAP):
    (ln,) = ax.plot([], [], lw=1.0, label=f"SC {k}")
    ln.set_visible(False)  # hide initially; we'll enable as needed
    lines.append(ln)

# Initial windows
win_end = T           # initial time window end
init_klo, init_khi = 0, INIT_K - 1       # initial SC window [inclusive]

def set_initial_data():
    lo_t, hi_t = 0, win_end - 1
    xs = x_full[lo_t:hi_t + 1]
    for i, ln in enumerate(lines):
        if init_klo <= i <= init_khi:
            ln.set_visible(True)
            ln.set_data(xs, mag[lo_t:hi_t + 1, i])
        else:
            ln.set_visible(False)

set_initial_data()

ax.set_xlabel(xlabel)
ax.set_ylabel(r"|H[k]|")
ax.set_title("Magnitude of subcarriers (interactive)")
ax.legend(ncol=2, fontsize=8, frameon=False)
ax.relim(); ax.autoscale()

# ---- widgets: two RangeSliders + buttons ----
# Time window slider (two handles; drag bar to pan)
ax_range = plt.axes([0.10, 0.16, 0.80, 0.04])
range_slider = RangeSlider(
    ax=ax_range,
    label="Window",
    valmin=0,
    valmax=T - 1,
    valinit=(0, win_end - 1),
    valstep=1,
    dragging=True,   # drag the filled bar to pan
)

# Subcarrier window slider (two handles; drag bar to pan)
ax_scr = plt.axes([0.10, 0.10, 0.80, 0.04])
sc_range = RangeSlider(
    ax=ax_scr,
    label="Subcarriers",
    valmin=0,
    valmax=KMAX_CAP,
    valinit=(0, 53),
    valstep=1,
    dragging=True,
)

# Buttons
ax_btn_auto = plt.axes([0.10, 0.055, 0.12, 0.035])
btn_rescale = Button(ax_btn_auto, "Autoscale Y")

ax_btn_reset = plt.axes([0.24, 0.055, 0.12, 0.035])
btn_reset = Button(ax_btn_reset, "Reset")

def update_plot(_=None):
    lo_t, hi_t = map(int, range_slider.val)
    klo,  khi  = map(int, sc_range.val)
    if hi_t <= lo_t:
        hi_t = min(lo_t + 1, T - 1)
    if khi <= klo:
        khi = min(klo + 1, KMAX_CAP - 1)

    xs = x_full[lo_t:hi_t + 1]

    for i, ln in enumerate(lines):
        if klo <= i <= khi:
            ln.set_visible(True)
            ln.set_data(xs, mag[lo_t:hi_t + 1, i])
        else:
            ln.set_visible(False)

    # refresh legend to only include visible lines
    vis = [ln for ln in lines if ln.get_visible()]
    if vis:
        ax.legend(handles=vis, labels=[h.get_label() for h in vis],
                  ncol=2, fontsize=8, frameon=False)
    ax.figure.canvas.draw_idle()

def do_autoscale(event=None):
    ax.relim()
    ax.autoscale()
    ax.figure.canvas.draw_idle()

def do_reset(event=None):
    range_slider.set_val((0, win_end - 1))
    sc_range.set_val((init_klo, init_khi))
    do_autoscale()

range_slider.on_changed(update_plot)
sc_range.on_changed(update_plot)
btn_rescale.on_clicked(do_autoscale)
btn_reset.on_clicked(do_reset)

# Optional: keyboard shortcuts
def on_key(event):
    lo_t, hi_t = map(int, range_slider.val)
    klo,  khi  = map(int, sc_range.val)

    if event.key == "left":   # pan time window left
        shift = max(1, (hi_t - lo_t) // 20)
        lo_t = max(0, lo_t - shift)
        hi_t = max(lo_t + 1, hi_t - shift)
        range_slider.set_val((lo_t, hi_t))

    elif event.key == "right":  # pan time window right
        shift = max(1, (hi_t - lo_t) // 20)
        hi_t = min(T - 1, hi_t + shift)
        lo_t = min(hi_t - 1, lo_t + shift)
        range_slider.set_val((lo_t, hi_t))

    elif event.key == "up":    # pan SC window up (higher indices)
        shift = max(1, (khi - klo + 1) // 5)
        khi = min(KMAX_CAP - 1, khi + shift)
        klo = min(khi - 1, klo + shift)
        sc_range.set_val((klo, khi))

    elif event.key == "down":  # pan SC window down (lower indices)
        shift = max(1, (khi - klo + 1) // 5)
        klo = max(0, klo - shift)
        khi = max(klo + 1, khi - shift)
        sc_range.set_val((klo, khi))

    elif event.key == "a":     # autoscale
        do_autoscale()
    elif event.key == "r":     # reset
        do_reset()

fig.canvas.mpl_connect("key_press_event", on_key)

# Mouse-wheel zoom around cursor (x-direction, affects time window)
def on_scroll(event):
    if event.inaxes != ax or event.xdata is None:
        return
    lo_t, hi_t = map(int, range_slider.val)
    width = max(hi_t - lo_t, 1)
    factor = 0.9 if event.button == "up" else 1.1
    new_width = max(1, int(width * factor))

    # Map xdata to sample index (if fs is set, xdata is time)
    if fs is not None:
        idx = int(round(event.xdata * fs))
    else:
        idx = int(round(event.xdata))

    # Keep proportion left/right around cursor
    left_frac = (idx - lo_t) / width if width > 0 else 0.5
    left = int(round(idx - left_frac * new_width))
    right = left + new_width
    left = max(0, left)
    right = min(T - 1, right)
    if right <= left:
        right = min(left + 1, T - 1)

    range_slider.set_val((left, right))

fig.canvas.mpl_connect("scroll_event", on_scroll)

update_plot()
plt.show()
