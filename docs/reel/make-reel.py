#!/usr/bin/env python3
"""Build the Instagram reel for the ad blocker, phase by phase.
usage: python3 docs/reel/make-reel.py images|clips|vo|assemble|all
  images   Gemini (key in ../../.env) draws one 9:16 start image per scene       -> scene_NN.png/jpg   (free)
  clips    Higgsfield CLI animates each image with Veo 3.1 Lite, no sound         -> clips/scene_NN.mp4 (credits!)
  vo       Gemini TTS speaks the voice-over line per scene                          -> vo/scene_NN.wav    (free)
  assemble ffmpeg: crop 1080x1920, burn captions, lay the voice-over, concatenate   -> reel.mp4
Nothing is regenerated if its output already exists; delete a file to redo that scene."""
import base64, json, pathlib, re, shutil, subprocess, sys, urllib.request, wave

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
ENV = {k.strip(): v.strip().strip('"') for k, v in (l.split("=", 1) for l in (ROOT / ".env").read_text().splitlines() if "=" in l and not l.lstrip().startswith("#"))}
KEY = ENV.get("GEMINI_API_KEY") or sys.exit("GEMINI_API_KEY missing in .env")
API = "https://generativelanguage.googleapis.com/v1beta"
FONT = HERE / "font.ttf"
if not FONT.exists(): shutil.copy("/System/Library/Fonts/Supplemental/Arial Bold.ttf", FONT)

STYLE_IMG = ("Photorealistic, cinematic, vertical 9:16 portrait framing, soft natural light, shallow depth of field, "
             "no text, no letters, no logos, no watermark, no brand names. ")
FLAT_IMG = ("Flat 2D vector illustration, clean minimal style, soft off-white background, muted blue and grey palette "
            "with one red accent, vertical 9:16, no text, no letters, no logos. ")
VERT = " Vertical 9:16, subject centred and fully inside the frame."

SCENES = [
  dict(id=1, dur=6, voice="Phones, tablets, TVs, laptops... ads on every single screen in your house. Sound familiar?",
       caption=["Ads on every screen", "in your house?"],
       image=STYLE_IMG + "A family living room at night: a phone, a tablet, a laptop and a TV all glowing, each screen crowded with colourful pop-up banners and adverts (abstract shapes and blocks, no readable words), slightly overwhelming, warm indoor lamp light." + VERT,
       video="Slow handheld push-in across the living room, the screens flicker as new colourful banner shapes pop up on them, warm lamp light, cinematic, no text."),
  dict(id=2, dur=6, voice="Well, here's the fix. One tiny chip, the size of a postage stamp, plugged into your Wi-Fi.",
       caption=["Here's the fix.", "One tiny chip. About five pounds."],
       image=STYLE_IMG + "Extreme close-up of a hand plugging a USB cable into a tiny green circuit board the size of a postage stamp, a small LED on the board, on a wooden desk next to a phone charger, calm and clean." + VERT,
       video="The hand pushes the cable in, the tiny LED on the board blinks blue then settles to steady green, gentle rack focus onto the LED, calm, no text."),
  dict(id=3, dur=8, freeze=4.6, voice="Here's the trick. Before any app can show you an ad, it has to ask where to fetch it from. Now this little chip answers that question. And for ads, the answer is... nowhere!",
       caption=["Every app first asks:", "\"where is this ad?\"", "", "The chip answers: nowhere.", "", "The ad never arrives."],
       image=FLAT_IMG + "A smartphone icon on the left with an empty speech bubble containing a question mark, a small green circuit-board icon in the centre, and on the right a grey cloud with a banner-shaped advert icon inside it; a dotted line from the chip toward the cloud." + VERT,
       video="Flat 2D animation: the question-mark bubble slides from the phone to the chip, the chip flashes a red cross, the dotted line and the advert cloud fade out and dissolve, smooth minimal motion, no text."),
  dict(id=4, dur=6, voice="Every phone, every TV, even your guests. Nothing to install. The moment you join the Wi-Fi, you're covered.",
       caption=["Works for everyone on the Wi-Fi.", "Nothing to install."],
       image=STYLE_IMG + "The same family living room in soft daylight, now calm: a child smiling at a tablet showing a clean colourful game with no banners, a TV showing a clean nature scene, a laptop with a clean page, relaxed atmosphere." + VERT,
       video="Slow gentle push-in, the child laughs softly at the tablet, screens stay clean and calm, soft daylight, no text."),
  dict(id=5, dur=6, voice="Now, YouTube is sneaky. It hides its ads inside the video itself. So I built a small browser add-on for that too.",
       caption=["Except YouTube.", "That one needed a browser add-on."],
       image=STYLE_IMG + "Close-up of a laptop screen showing a generic video player with a play button, and in the browser toolbar a small glowing puzzle-piece icon; hands on the keyboard, evening desk light." + VERT,
       video="The puzzle-piece icon glows brighter, the video plays smoothly with no interruption, subtle camera drift, no text."),
  dict(id=6, dur=6, voice="And the best part? It's completely free and open source. Link in bio. Go build one!",
       caption=["Free and open source.", "Link in bio."],
       image=STYLE_IMG + "A hand holding a phone showing a dark dashboard with a bright green status pill and a rising counter (abstract bars, no readable words), the tiny green circuit board lying on the desk beside a coffee cup, morning light." + VERT,
       video="The counter bars on the phone rise, the tiny LED on the board blinks green, gentle handheld movement, no text."),
]

def gemini(model, body):
    req = urllib.request.Request(f"{API}/models/{model}:generateContent", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json", "x-goog-api-key": KEY})
    with urllib.request.urlopen(req, timeout=300) as r: return json.load(r)

def image_path(s):
    for ext in ("png", "jpg"):
        p = HERE / f"scene_{s['id']:02d}.{ext}"
        if p.exists(): return p
    return None

def phase_images():
    for s in SCENES:
        if image_path(s): print(f"scene {s['id']}: image exists"); continue
        resp = gemini("gemini-3-pro-image", {"contents": [{"parts": [{"text": s["image"]}]}],
                      "generationConfig": {"responseModalities": ["IMAGE", "TEXT"], "imageConfig": {"aspectRatio": "9:16"}}})
        parts = resp.get("candidates", [{}])[0].get("content", {}).get("parts", [])
        img = next((p["inlineData"] for p in parts if "inlineData" in p), None)
        if not img: print(f"scene {s['id']}: no image", json.dumps(resp)[:200]); continue
        ext = "png" if "png" in img.get("mimeType", "") else "jpg"
        (HERE / f"scene_{s['id']:02d}.{ext}").write_bytes(base64.b64decode(img["data"]))
        print(f"scene {s['id']}: image {ext} ok")

def phase_clips():
    (HERE / "clips").mkdir(exist_ok=True)
    cli = shutil.which("higgsfield") or sys.exit("higgsfield CLI not found")
    procs = []
    for s in SCENES:
        out = HERE / "clips" / f"scene_{s['id']:02d}.mp4"
        if out.exists(): print(f"scene {s['id']}: clip exists"); continue
        img = image_path(s) or sys.exit(f"scene {s['id']}: no start image, run 'images' first")
        cmd = [cli, "generate", "create", "veo3_1_lite", "--prompt", s["video"], "--duration", str(s["dur"]),
               "--generate_audio", "false", "--aspect_ratio", "9:16", "--start-image", str(img),
               "--wait", "--wait-timeout", "25m", "--json"]
        procs.append((s, out, subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)))
        print(f"scene {s['id']}: job started ({s['dur']} s)")
    for s, out, p in procs:
        text, _ = p.communicate(timeout=1800)
        urls = re.findall(r"https://[^\s\"']+\.mp4[^\s\"']*", text)
        if p.returncode != 0 or not urls: print(f"scene {s['id']}: FAILED\n{text[-500:]}"); continue
        with urllib.request.urlopen(urls[-1], timeout=600) as r: out.write_bytes(r.read())
        print(f"scene {s['id']}: clip saved ({out.stat().st_size // 1024} KB)")

def phase_vo():
    (HERE / "vo").mkdir(exist_ok=True)
    direction = ("You are ONE single character for the whole video: a charismatic, confident TV-shopping-channel presenter "
                 "pitching a gadget they built themselves. Upbeat, persuasive and warm, a smile in the voice, punchy pacing "
                 "with a short dramatic pause before each payoff, never shouting, never robotic, never a news reader. "
                 "Keep exactly the same persona, pitch and energy in every line. Speak ONLY the following words, nothing else: ")
    for s in SCENES:
        out = HERE / "vo" / f"scene_{s['id']:02d}.wav"
        if out.exists(): print(f"scene {s['id']}: voice exists"); continue
        body = {"contents": [{"parts": [{"text": direction + s["voice"]}]}],
                "generationConfig": {"responseModalities": ["AUDIO"],
                                     "speechConfig": {"voiceConfig": {"prebuiltVoiceConfig": {"voiceName": "Puck"}}}}}
        resp = None
        for model in ("gemini-2.5-pro-preview-tts", "gemini-2.5-flash-preview-tts"):
            try: resp = gemini(model, body); break
            except Exception as e: print(f"  {model}: {str(e)[:120]}")
        blob = resp["candidates"][0]["content"]["parts"][0]["inlineData"]
        pcm = base64.b64decode(blob["data"]); rate = int(re.search(r"rate=(\d+)", blob.get("mimeType", "rate=24000")).group(1))
        with wave.open(str(out), "wb") as w: w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm)
        print(f"scene {s['id']}: voice ok ({len(pcm) / rate / 2:.1f} s)")

def ffprobe_len(path):
    return float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(path)],
                                capture_output=True, text=True).stdout.strip() or 0)

def caption_png(lines, path):
    """Render caption lines to a transparent PNG with a dark rounded box (this ffmpeg has no drawtext)."""
    from PIL import Image, ImageDraw, ImageFont
    W = 1080; font = ImageFont.truetype(str(FONT), 62); pad, gap = 34, 14
    sizes = [font.getbbox(l) for l in lines]
    tw = max(b[2] - b[0] for b in sizes); th = sum(b[3] - b[1] for b in sizes) + gap * (len(lines) - 1)
    img = Image.new("RGBA", (W, th + 2 * pad), (0, 0, 0, 0)); d = ImageDraw.Draw(img)
    x0 = (W - tw) // 2 - pad; d.rounded_rectangle([x0, 0, x0 + tw + 2 * pad, th + 2 * pad], radius=28, fill=(0, 0, 0, 150))
    y = pad
    for l, b in zip(lines, sizes):
        d.text(((W - (b[2] - b[0])) // 2 - b[0], y - b[1]), l, font=font, fill=(255, 255, 255, 255)); y += (b[3] - b[1]) + gap
    img.save(path); return img.size[1]

def phase_assemble():
    (HERE / "out").mkdir(exist_ok=True)
    parts = []
    for s in SCENES:
        clip = HERE / "clips" / f"scene_{s['id']:02d}.mp4"; vo = HERE / "vo" / f"scene_{s['id']:02d}.wav"
        if not clip.exists(): print(f"scene {s['id']}: no clip, skipped"); continue
        # trim the silence Gemini pads around the line, then size the scene to the narration;
        # a clip shorter than its narration holds its last frame (tpad) instead of cutting the voice
        if vo.exists():
            trimmed = HERE / "out" / f"vo_{s['id']:02d}.wav"
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(vo), "-af",
                            # leading silence, then (via reverse) trailing silence only — never the pauses between sentences
                            "silenceremove=start_periods=1:start_threshold=-42dB:start_silence=0.15,"
                            "areverse,silenceremove=start_periods=1:start_threshold=-42dB:start_silence=0.25,areverse,"
                            "loudnorm=I=-16:TP=-1.5:LRA=11", str(trimmed)], check=True)
            vo = trimmed
        vo_len = ffprobe_len(vo) if vo.exists() else 0
        clip_len = ffprobe_len(clip) or s["dur"]
        # optional freeze: stop the clip at this second and hold that frame (for clips whose ending dissolves to nothing)
        frz = s.get("freeze"); clip_len = min(clip_len, frz) if frz else clip_len
        dur = max(vo_len + 0.9, 4.0)
        hold = max(0.0, dur - clip_len)
        groups = [[]]
        for l in s["caption"]: (groups.append([]) if l == "" else groups[-1].append(l))
        n = len(groups); seg = dur / n
        inputs = ["-i", str(clip)]
        chain = "[0:v]" + (f"trim=end={frz},setpts=PTS-STARTPTS," if frz else "") + "scale=1080:1920:force_original_aspect_ratio=increase,crop=1080:1920" + (f",tpad=stop_mode=clone:stop_duration={hold:.2f}" if hold > 0.05 else "") + "[v0]"; last = "v0"
        for gi, g in enumerate(groups):
            png = HERE / "out" / f"cap_{s['id']:02d}_{gi}.png"; h = caption_png(g, png)
            inputs += ["-i", str(png)]
            enable = f"between(t,{gi*seg:.2f},{dur if gi == n-1 else (gi+1)*seg:.2f})"
            chain += f";[{last}][{gi+1}:v]overlay=0:{int(1920*0.70 - h/2)}:enable='{enable}'[v{gi+1}]"; last = f"v{gi+1}"
        chain += f";[{last}]fps=30,format=yuv420p[vout]"
        out = HERE / "out" / f"scene_{s['id']:02d}.mp4"
        cmd = ["ffmpeg", "-y", "-loglevel", "error"] + inputs
        if vo.exists(): cmd += ["-itsoffset", "0.45", "-i", str(vo)]
        cmd += ["-filter_complex", chain, "-map", "[vout]", "-t", f"{dur:.2f}", "-c:v", "libx264", "-preset", "medium", "-crf", "20"]
        cmd += (["-map", f"{n+1}:a", "-af", "apad", "-c:a", "aac", "-b:a", "160k"] if vo.exists() else ["-an"])
        cmd += [str(out)]
        subprocess.run(cmd, check=True)
        parts.append(out); print(f"scene {s['id']}: {dur:.1f} s")
    lst = HERE / "out" / "list.txt"; lst.write_text("".join(f"file '{p}'\n" for p in parts))
    final = HERE / "reel.mp4"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(lst), "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p",
                    "-c:a", "aac", "-b:a", "160k", "-movflags", "+faststart", str(final)], check=True)  # one clean stream, streamable
    print(f"reel.mp4: {ffprobe_len(final):.1f} s, {final.stat().st_size // 1024} KB")

if __name__ == "__main__":
    phase = sys.argv[1] if len(sys.argv) > 1 else "all"
    for name in (["images", "clips", "vo", "assemble"] if phase == "all" else [phase]):
        print(f"--- {name} ---"); globals()[f"phase_{name}"]()
