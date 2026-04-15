#!/usr/bin/env python3
"""
Arm telemetry recorder
Records 50 Hz telemetry (tick, pos, setpoint, err, pwm) to CSV.
After stopping, opens a matplotlib plot of the session.

Telemetry format from firmware: tick,pos,setpoint,err,pwm
  tick    — firmware PID tick counter (1 tick = 20ms)
  pos     — encoder count
  setpoint— current target count
  err     — setpoint - pos
  pwm     — motor output (-100 to +100)
"""

import tkinter as tk
import serial
import threading
import time
import csv
import os
from datetime import datetime

PORT = '/dev/ttyUSB0'
BAUD = 115200

# ── Serial setup ──────────────────────────────────────────────────────────────
arm = serial.Serial(PORT, BAUD, timeout=0.1)
time.sleep(2)

def send(cmd):
    arm.write((cmd + '\n').encode())

# ── Recording state ───────────────────────────────────────────────────────────
_recording    = False
_record_start = 0.0
_record_file  = None
_record_writer= None
_record_path  = ""
_row_count    = 0

# ── Background reader ─────────────────────────────────────────────────────────
latest_telem = None  # tk.StringVar, filled after root is created

def reader():
    global _recording, _record_writer, _record_file, _record_start, _row_count
    while True:
        line = arm.readline().decode(errors='ignore').strip()
        if line.count(',') != 4:
            continue
        parts = line.split(',')
        try:
            tick, pos, sp, err, pwm = [int(p) for p in parts]
        except ValueError:
            continue

        if latest_telem is not None:
            latest_telem.set(f"{pos}, {sp}, {err}, {pwm}")

        if _recording and _record_writer is not None:
            t_ms = int((time.time() - _record_start) * 1000)
            _record_writer.writerow([t_ms, tick, pos, sp, err, pwm])
            _record_file.flush()
            _row_count += 1

reader_thread = threading.Thread(target=reader, daemon=True)
reader_thread.start()

# ── Plot ──────────────────────────────────────────────────────────────────────
def plot_csv(path):
    """Open a matplotlib figure for the given CSV. Runs in its own thread."""
    try:
        import matplotlib.pyplot as plt
        import matplotlib.gridspec as gridspec
    except ImportError:
        print("matplotlib not installed — run: pip install matplotlib")
        return

    t_ms_col, tick_col, pos_col, sp_col, err_col, pwm_col = [], [], [], [], [], []
    with open(path, newline='') as f:
        reader_csv = csv.DictReader(f)
        for row in reader_csv:
            t_ms_col.append(int(row['t_ms']))
            tick_col.append(int(row['tick']))
            pos_col.append(int(row['pos']))
            sp_col.append(int(row['setpoint']))
            err_col.append(int(row['err']))
            pwm_col.append(int(row['pwm']))

    t_sec = [t / 1000.0 for t in t_ms_col]

    fig = plt.figure(figsize=(12, 7))
    fig.suptitle(os.path.basename(path), fontsize=11, fontweight='bold')
    gs = gridspec.GridSpec(2, 1, hspace=0.45)

    # ── Top: position vs setpoint ─────────────────────────────────────────────
    ax1 = fig.add_subplot(gs[0])
    ax1.plot(t_sec, pos_col,  color='#2277cc', linewidth=1.2, label='pos')
    ax1.plot(t_sec, sp_col,   color='#ff8800', linewidth=1.0,
             linestyle='--', label='setpoint')
    ax1.set_ylabel('Encoder counts')
    ax1.set_title('Position vs Setpoint')
    ax1.legend(loc='upper right', fontsize=9)
    ax1.grid(True, alpha=0.35)

    # ── Bottom: error and PWM ─────────────────────────────────────────────────
    ax2 = fig.add_subplot(gs[1], sharex=ax1)
    ax2.axhline(0, color='#888888', linewidth=0.7, linestyle=':')
    ax2.plot(t_sec, err_col,  color='#cc2222', linewidth=1.0, label='error')
    ax2.set_ylabel('Error (counts)', color='#cc2222')
    ax2.tick_params(axis='y', labelcolor='#cc2222')

    ax3 = ax2.twinx()
    ax3.plot(t_sec, pwm_col,  color='#228833', linewidth=1.0,
             alpha=0.8, label='pwm')
    ax3.set_ylabel('PWM output', color='#228833')
    ax3.tick_params(axis='y', labelcolor='#228833')
    ax3.set_ylim(-110, 110)

    ax2.set_title('Error and PWM')
    ax2.set_xlabel('Time (s)')
    ax2.grid(True, alpha=0.35)

    # Combined legend for ax2 + ax3
    lines2, labels2 = ax2.get_legend_handles_labels()
    lines3, labels3 = ax3.get_legend_handles_labels()
    ax2.legend(lines2 + lines3, labels2 + labels3, loc='upper right', fontsize=9)

    plt.show()

def launch_plot(path):
    t = threading.Thread(target=plot_csv, args=(path,), daemon=True)
    t.start()

# ── GUI ───────────────────────────────────────────────────────────────────────
root = tk.Tk()
root.title("Arm Recorder")
root.resizable(False, False)

latest_telem   = tk.StringVar(value="---")
status_var     = tk.StringVar(value="Ready")
rows_var        = tk.StringVar(value="")

FONT_LABEL = ("Courier", 11)
FONT_BTN   = ("Courier", 13, "bold")
FONT_SMALL = ("Courier", 10)
PAD        = dict(padx=10, pady=5)

# ── Header ────────────────────────────────────────────────────────────────────
tk.Label(root, text="═══ ARM RECORDER ══════════════════════",
         font=("Courier", 12, "bold"), anchor="w").grid(
    row=0, column=0, columnspan=4, sticky="ew", padx=10, pady=(10, 2))

# ── Live telemetry ────────────────────────────────────────────────────────────
tk.Label(root, text="Telem (pos, sp, err, pwm):",
         font=FONT_LABEL, anchor="w").grid(
    row=1, column=0, columnspan=2, sticky="w", padx=10)
tk.Label(root, textvariable=latest_telem,
         font=FONT_LABEL, fg="#00aa00", anchor="w").grid(
    row=1, column=2, columnspan=2, sticky="w", padx=4)

tk.Label(root, text="────────────────────────────────────────",
         font=FONT_SMALL).grid(row=2, column=0, columnspan=4, pady=(4, 6))

# ── Status / timer ────────────────────────────────────────────────────────────
status_label = tk.Label(root, textvariable=status_var,
                        font=("Courier", 12, "bold"), fg="#666666")
status_label.grid(row=3, column=0, columnspan=3, sticky="w", padx=10)

tk.Label(root, textvariable=rows_var,
         font=FONT_SMALL, fg="#888888", anchor="e").grid(
    row=3, column=3, sticky="e", padx=10)

# ── Record button ─────────────────────────────────────────────────────────────
def update_timer():
    if _recording:
        elapsed = time.time() - _record_start
        status_var.set(f"RECORDING  {elapsed:.1f}s")
        rows_var.set(f"{_row_count} rows")
        root.after(100, update_timer)

def toggle_record():
    global _recording, _record_start, _record_file, _record_writer
    global _record_path, _row_count

    if not _recording:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        _record_path   = f"arm_log_{ts}.csv"
        _record_file   = open(_record_path, 'w', newline='')
        _record_writer = csv.writer(_record_file)
        _record_writer.writerow(['t_ms', 'tick', 'pos', 'setpoint', 'err', 'pwm'])
        _row_count     = 0
        _record_start  = time.time()
        _recording     = True

        record_btn.config(text="STOP  RECORDING", bg="#aa3333")
        status_label.config(fg="#cc0000")
        plot_btn.grid_remove()
        update_timer()

    else:
        _recording = False
        if _record_file:
            _record_file.close()
            _record_file   = None
            _record_writer = None

        record_btn.config(text="START  RECORDING", bg="#228822")
        status_label.config(fg="#006600")
        status_var.set(f"Saved: {_record_path}")
        rows_var.set(f"{_row_count} rows")
        plot_btn.grid()   # show plot button now that we have data

record_btn = tk.Button(root, text="START  RECORDING", font=FONT_BTN,
                       width=32, bg="#228822", fg="white",
                       activebackground="#116611",
                       command=toggle_record)
record_btn.grid(row=4, column=0, columnspan=4, padx=10, pady=6)

# ── Plot button (hidden until first recording saved) ──────────────────────────
plot_btn = tk.Button(root, text="PLOT LAST RECORDING", font=FONT_BTN,
                     width=32, bg="#335588", fg="white",
                     activebackground="#224477",
                     command=lambda: launch_plot(_record_path))
plot_btn.grid(row=5, column=0, columnspan=4, padx=10, pady=(0, 6))
plot_btn.grid_remove()

tk.Label(root, text="────────────────────────────────────────",
         font=FONT_SMALL).grid(row=6, column=0, columnspan=4, pady=(2, 6))

# ── Arm control ───────────────────────────────────────────────────────────────
tk.Button(root, text="Bump −\n(−10)", font=FONT_BTN, width=14,
          bg="#4a6fa5", fg="white", activebackground="#3a5f95",
          command=lambda: send('NUDGE:-10')).grid(
    row=7, column=0, columnspan=2, **PAD)

tk.Button(root, text="Bump +\n(+10)", font=FONT_BTN, width=14,
          bg="#4a6fa5", fg="white", activebackground="#3a5f95",
          command=lambda: send('NUDGE:10')).grid(
    row=7, column=2, columnspan=2, **PAD)

tk.Button(root, text="Goto  −150", font=FONT_BTN, width=14,
          bg="#5a8a5a", fg="white", activebackground="#4a7a4a",
          command=lambda: send('GOTO:-150')).grid(
    row=8, column=0, columnspan=2, **PAD)

tk.Button(root, text="Goto  +150", font=FONT_BTN, width=14,
          bg="#5a8a5a", fg="white", activebackground="#4a7a4a",
          command=lambda: send('GOTO:150')).grid(
    row=8, column=2, columnspan=2, **PAD)

tk.Button(root, text="HOME", font=FONT_BTN, width=32,
          bg="#c8a020", fg="white", activebackground="#b89010",
          command=lambda: send('HOME')).grid(
    row=9, column=0, columnspan=4, **PAD)

# ── Quit ──────────────────────────────────────────────────────────────────────
def quit_app():
    global _recording
    if _recording:
        toggle_record()
    send('STREAM:OFF')
    time.sleep(0.1)
    arm.close()
    root.destroy()

tk.Button(root, text="QUIT", font=FONT_BTN, width=32,
          bg="#aa3333", fg="white", activebackground="#882222",
          command=quit_app).grid(
    row=10, column=0, columnspan=4, **PAD)

tk.Label(root, text="════════════════════════════════════════",
         font=FONT_SMALL).grid(row=11, column=0, columnspan=4, pady=(4, 10))

# ── Start ─────────────────────────────────────────────────────────────────────
# Firmware auto-starts streaming at 50 Hz — no STREAM:ON needed,
# but send it anyway in case the ESP32 was already running.
send('STREAM:ON')

root.protocol("WM_DELETE_WINDOW", quit_app)
root.mainloop()
