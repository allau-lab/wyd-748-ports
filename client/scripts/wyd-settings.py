#!/usr/bin/env python3
"""WYD 7.48 — Configurador de vídeo, áudio e qualidade (config.txt)."""

from __future__ import annotations

import os
import subprocess
import sys
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk

CLIENT_DIR = Path(__file__).resolve().parent.parent / "client748"
CONFIG_PATH = CLIENT_DIR / "config.txt"

# Índice [RES] 1-based → resolução (NewApp::stResList)
RESOLUTIONS = [
    (1, "640 × 480"),
    (2, "800 × 600"),
    (3, "1024 × 768"),
    (4, "1280 × 720"),
    (5, "1280 × 800"),
    (6, "1280 × 960"),
    (7, "1280 × 1024"),
    (8, "1440 × 900"),
    (9, "1600 × 900"),
    (10, "1600 × 1024"),
    (11, "1600 × 1080 (máx.)"),
]

KEYS_ORDER = [
    "VERSION",
    "RES",
    "ANIMATION",
    "SOUND",
    "MUSIC",
    "SERVER",
    "BRIGHT",
    "CURSOR",
    "DEMO",
    "WINDOW",
    "CLASSIC",
    "CAMERAROTATE",
    "DXT",
    "KEYTYPE",
    "CAMERAVIEW",
    "LOL_CONTROLS",
    "TOUCH_UI",
    "VSYNC",
    "ANISOTROPIC",
    "MIPMAP",
    "ANTIALIAS",
    "REFLECTION",
    "DEBUG_STATS",
]

DEFAULTS = {
    "VERSION": 10000,
    "RES": 11,
    "ANIMATION": 2,
    "SOUND": 100,
    "MUSIC": 100,
    "SERVER": -1,
    "BRIGHT": 55,
    "CURSOR": 1,
    "DEMO": 0,
    "WINDOW": 1,
    "CLASSIC": 1,
    "CAMERAROTATE": 1,
    "DXT": 1,
    "KEYTYPE": 0,
    "CAMERAVIEW": 0,
    "LOL_CONTROLS": 1,
    "TOUCH_UI": 1,
    "VSYNC": 1,
    "ANISOTROPIC": 8,
    "MIPMAP": 50,
    "ANTIALIAS": 4,
    "REFLECTION": 1,
    "DEBUG_STATS": 0,
}

PRESETS = {
    "LOW": {
        "RES": 4, "ANISOTROPIC": 2, "MIPMAP": 20,
        "ANTIALIAS": 0, "REFLECTION": 0, "VSYNC": 1,
    },
    "MEDIUM": {
        "RES": 5, "ANISOTROPIC": 4, "MIPMAP": 30,
        "ANTIALIAS": 2, "REFLECTION": 0, "VSYNC": 1,
    },
    "HIGH": {
        "RES": 9, "ANISOTROPIC": 8, "MIPMAP": 50,
        "ANTIALIAS": 4, "REFLECTION": 1, "VSYNC": 1,
    },
    "ULTRA": {
        "RES": 11, "ANISOTROPIC": 16, "MIPMAP": 50,
        "ANTIALIAS": 8, "REFLECTION": 1, "VSYNC": 1,
    },
}

MAX_QUALITY = {**DEFAULTS, **PRESETS["ULTRA"]}


def load_config(path: Path) -> dict[str, int]:
    cfg = dict(DEFAULTS)
    if not path.is_file():
        return cfg
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line.startswith("["):
            continue
        try:
            key, val = line[1:].split("]", 1)
            cfg[key.strip()] = int(val.strip())
        except (ValueError, IndexError):
            continue
    return cfg


def save_config(path: Path, cfg: dict[str, int]) -> None:
    lines = [f"[{k}] {cfg.get(k, DEFAULTS[k])}" for k in KEYS_ORDER]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


class SettingsApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("WYD 7.48 — Configurações")
        self.minsize(480, 560)
        self.configure(bg="#1a1c22")
        self.cfg = load_config(CONFIG_PATH)

        style = ttk.Style(self)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        style.configure("TFrame", background="#1a1c22")
        style.configure("TLabelframe", background="#1a1c22", foreground="#e8e6e1")
        style.configure("TLabelframe.Label", background="#1a1c22", foreground="#c9a66b", font=("DejaVu Sans", 11, "bold"))
        style.configure("TLabel", background="#1a1c22", foreground="#e8e6e1", font=("DejaVu Sans", 10))
        style.configure("TCheckbutton", background="#1a1c22", foreground="#e8e6e1", font=("DejaVu Sans", 10))
        style.configure("TButton", font=("DejaVu Sans", 10))
        style.configure("Accent.TButton", font=("DejaVu Sans", 10, "bold"))

        header = ttk.Label(self, text="Configurações do Cliente", font=("DejaVu Sans", 16, "bold"))
        header.pack(pady=(18, 4))
        sub = ttk.Label(self, text=str(CONFIG_PATH), foreground="#8a8790")
        sub.pack(pady=(0, 12))

        body = ttk.Frame(self, padding=16)
        body.pack(fill=tk.BOTH, expand=True)

        preset_bar = ttk.Frame(body)
        preset_bar.pack(fill=tk.X, pady=(0, 6))
        ttk.Label(preset_bar, text="Preset gráfico:").pack(side=tk.LEFT)
        self.preset_var = tk.StringVar(value="HIGH")
        self.preset_combo = ttk.Combobox(
            preset_bar, textvariable=self.preset_var,
            values=["LOW", "MEDIUM", "HIGH", "ULTRA", "CUSTOM"],
            state="readonly", width=12,
        )
        self.preset_combo.pack(side=tk.LEFT, padx=8)
        ttk.Button(preset_bar, text="Aplicar preset", command=self.apply_preset).pack(side=tk.LEFT)

        # Vídeo
        video = ttk.LabelFrame(body, text=" Vídeo ", padding=12)
        video.pack(fill=tk.X, pady=6)

        ttk.Label(video, text="Resolução").grid(row=0, column=0, sticky="w", pady=4)
        self.res_var = tk.StringVar()
        res_labels = [f"{i} — {label}" for i, label in RESOLUTIONS]
        self.res_combo = ttk.Combobox(video, textvariable=self.res_var, values=res_labels, state="readonly", width=28)
        self.res_combo.grid(row=0, column=1, sticky="ew", padx=8, pady=4)

        ttk.Label(video, text="Animação (suavidade)").grid(row=1, column=0, sticky="w", pady=4)
        self.anim_var = tk.IntVar(value=int(self.cfg.get("ANIMATION", 2)))
        ttk.Scale(video, from_=0, to=2, orient=tk.HORIZONTAL, variable=self.anim_var).grid(
            row=1, column=1, sticky="ew", padx=8, pady=4
        )

        ttk.Label(video, text="Brilho").grid(row=2, column=0, sticky="w", pady=4)
        self.bright_var = tk.IntVar(value=int(self.cfg.get("BRIGHT", 55)))
        ttk.Scale(video, from_=10, to=100, orient=tk.HORIZONTAL, variable=self.bright_var).grid(
            row=2, column=1, sticky="ew", padx=8, pady=4
        )

        self.window_var = tk.IntVar(value=1 if self.cfg.get("WINDOW", 1) else 0)
        ttk.Checkbutton(video, text="Modo janela", variable=self.window_var).grid(
            row=3, column=0, columnspan=2, sticky="w", pady=4
        )

        self.dxt_var = tk.IntVar(value=1 if self.cfg.get("DXT", 1) else 0)
        ttk.Checkbutton(
            video,
            text="Texturas sem compressão DXT (melhor qualidade)",
            variable=self.dxt_var,
        ).grid(row=4, column=0, columnspan=2, sticky="w", pady=4)

        self.classic_var = tk.IntVar(value=1 if self.cfg.get("CLASSIC", 1) else 0)
        ttk.Checkbutton(video, text="Interface clássica (7.48)", variable=self.classic_var).grid(
            row=5, column=0, columnspan=2, sticky="w", pady=4
        )

        video.columnconfigure(1, weight=1)

        # Áudio
        audio = ttk.LabelFrame(body, text=" Áudio ", padding=12)
        audio.pack(fill=tk.X, pady=6)

        ttk.Label(audio, text="Efeitos (SFX)").grid(row=0, column=0, sticky="w", pady=4)
        self.sound_var = tk.IntVar(value=int(self.cfg.get("SOUND", 100)))
        ttk.Scale(audio, from_=0, to=100, orient=tk.HORIZONTAL, variable=self.sound_var).grid(
            row=0, column=1, sticky="ew", padx=8, pady=4
        )
        self.sound_lbl = ttk.Label(audio, text="")
        self.sound_lbl.grid(row=0, column=2, sticky="e")
        self.sound_var.trace_add("write", lambda *_: self._sync_labels())

        ttk.Label(audio, text="Música (BGM)").grid(row=1, column=0, sticky="w", pady=4)
        self.music_var = tk.IntVar(value=int(self.cfg.get("MUSIC", 100)))
        ttk.Scale(audio, from_=0, to=100, orient=tk.HORIZONTAL, variable=self.music_var).grid(
            row=1, column=1, sticky="ew", padx=8, pady=4
        )
        self.music_lbl = ttk.Label(audio, text="")
        self.music_lbl.grid(row=1, column=2, sticky="e")
        self.music_var.trace_add("write", lambda *_: self._sync_labels())

        audio.columnconfigure(1, weight=1)

        # Câmera
        cam = ttk.LabelFrame(body, text=" Câmera / Controles ", padding=12)
        cam.pack(fill=tk.X, pady=6)
        self.camrot_var = tk.IntVar(value=1 if self.cfg.get("CAMERAROTATE", 1) else 0)
        ttk.Checkbutton(cam, text="Rotação de câmera", variable=self.camrot_var).pack(anchor="w")

        # Botões
        btns = ttk.Frame(body)
        btns.pack(fill=tk.X, pady=16)
        ttk.Button(btns, text="Máxima qualidade", command=self.apply_max_quality).pack(side=tk.LEFT, padx=4)
        ttk.Button(btns, text="Salvar", command=self.save, style="Accent.TButton").pack(side=tk.LEFT, padx=4)
        ttk.Button(btns, text="Salvar e iniciar", command=self.save_and_launch).pack(side=tk.LEFT, padx=4)
        ttk.Button(btns, text="Fechar", command=self.destroy).pack(side=tk.RIGHT, padx=4)

        note = ttk.Label(
            body,
            text="MSAA / aniso / VSync são aplicados pelo cliente Linux automaticamente.\n"
            "SOUND/MUSIC = 0 desliga o áudio. Pasta sound/ e music/ são necessárias.",
            foreground="#8a8790",
            justify=tk.LEFT,
        )
        note.pack(anchor="w", pady=(4, 0))

        self._select_res(int(self.cfg.get("RES", 11)))
        self._sync_labels()

    def _select_res(self, res_index: int) -> None:
        for i, (idx, label) in enumerate(RESOLUTIONS):
            if idx == res_index:
                self.res_combo.current(i)
                return
        self.res_combo.current(len(RESOLUTIONS) - 1)

    def _sync_labels(self) -> None:
        self.sound_lbl.configure(text=f"{int(self.sound_var.get())}%")
        self.music_lbl.configure(text=f"{int(self.music_var.get())}%")

    def _collect(self) -> dict[str, int]:
        sel = self.res_combo.current()
        res = RESOLUTIONS[sel][0] if sel >= 0 else 11
        return {
            "VERSION": int(self.cfg.get("VERSION", 10000)),
            "RES": res,
            "ANIMATION": int(self.anim_var.get()),
            "SOUND": int(self.sound_var.get()),
            "MUSIC": int(self.music_var.get()),
            "SERVER": int(self.cfg.get("SERVER", -1)),
            "BRIGHT": int(self.bright_var.get()),
            "CURSOR": int(self.cfg.get("CURSOR", 1)),
            "DEMO": int(self.cfg.get("DEMO", 0)),
            "WINDOW": 1 if self.window_var.get() else 0,
            "CLASSIC": 1 if self.classic_var.get() else 0,
            "CAMERAROTATE": 1 if self.camrot_var.get() else 0,
            "DXT": 1 if self.dxt_var.get() else 0,
            "KEYTYPE": int(self.cfg.get("KEYTYPE", 0)),
            "CAMERAVIEW": int(self.cfg.get("CAMERAVIEW", 0)),
            "LOL_CONTROLS": int(self.cfg.get("LOL_CONTROLS", 1)),
            "TOUCH_UI": int(self.cfg.get("TOUCH_UI", 1)),
            "VSYNC": int(self.cfg.get("VSYNC", 1)),
            "ANISOTROPIC": int(self.cfg.get("ANISOTROPIC", 8)),
            "MIPMAP": int(self.cfg.get("MIPMAP", 50)),
            "ANTIALIAS": int(self.cfg.get("ANTIALIAS", 4)),
            "REFLECTION": int(self.cfg.get("REFLECTION", 1)),
            "DEBUG_STATS": int(self.cfg.get("DEBUG_STATS", 0)),
        }

    def apply_preset(self) -> None:
        name = self.preset_var.get()
        if name == "CUSTOM":
            return
        self.cfg.update(PRESETS[name])
        preset = self.cfg
        self._select_res(int(preset["RES"]))
        self.dxt_var.set(1)
        self.window_var.set(1)
        self.classic_var.set(1)
        self.preset_var.set(name)
        messagebox.showinfo("Preset gráfico", f"Preset {name} aplicado. Clique em Salvar para gravar.")

    def apply_max_quality(self) -> None:
        self.cfg.update(MAX_QUALITY)
        self._select_res(11)
        self.anim_var.set(2)
        self.bright_var.set(55)
        self.sound_var.set(100)
        self.music_var.set(100)
        self.window_var.set(1)
        self.dxt_var.set(1)
        self.classic_var.set(1)
        self.camrot_var.set(1)
        self.preset_var.set("ULTRA")
        self._sync_labels()
        messagebox.showinfo("Qualidade", "Preset de máxima qualidade aplicado.\nClique em Salvar para gravar.")

    def save(self) -> None:
        cfg = self._collect()
        try:
            save_config(CONFIG_PATH, cfg)
            self.cfg = cfg
            # Garante links de áudio
            self._ensure_audio_links()
            messagebox.showinfo("Salvo", f"Configuração gravada em:\n{CONFIG_PATH}")
        except OSError as exc:
            messagebox.showerror("Erro", str(exc))

    def _ensure_audio_links(self) -> None:
        sound = CLIENT_DIR / "sound"
        music = CLIENT_DIR / "music"
        if sound.exists() and music.exists():
            return
        # Só avisa — o setup inicial já criou os symlinks quando possível.
        missing = []
        if not sound.exists():
            missing.append("sound/")
        if not music.exists():
            missing.append("music/")
        if missing:
            messagebox.showwarning(
                "Áudio",
                "Pastas ausentes: " + ", ".join(missing) + "\n"
                "O configurador espera sound/ e music/ em client748.",
            )

    def save_and_launch(self) -> None:
        self.save()
        project = CLIENT_DIR / "project"
        if not project.is_file():
            messagebox.showerror("Erro", f"Binário não encontrado:\n{project}")
            return
        env = os.environ.copy()
        env.setdefault("DXVK_WSI_DRIVER", "SDL2")
        env.setdefault("SDL_VIDEODRIVER", "wayland")
        dxvk = CLIENT_DIR.parent / "third_party" / "dxvk-native" / "usr" / "lib"
        if dxvk.is_dir():
            env["LD_LIBRARY_PATH"] = f"{dxvk}:{env.get('LD_LIBRARY_PATH', '')}"
        try:
            subprocess.Popen([str(project)], cwd=str(CLIENT_DIR), env=env)
        except OSError as exc:
            messagebox.showerror("Erro ao iniciar", str(exc))


def main() -> int:
    if not CLIENT_DIR.is_dir():
        print(f"Pasta do cliente não encontrada: {CLIENT_DIR}", file=sys.stderr)
        return 1
    app = SettingsApp()
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
