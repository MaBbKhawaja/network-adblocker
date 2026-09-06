#!/usr/bin/env python3
"""Generate the README pictures with Gemini's image model. Reads GEMINI_API_KEY from ../.env (gitignored).
usage: python3 docs/gen-images.py [list]        -> 'list' just prints the image-capable models for this key
Writes docs/flowchart.png and docs/diagram.png."""
import base64, json, os, pathlib, sys, urllib.error, urllib.request

root = pathlib.Path(__file__).resolve().parent.parent
env = {}
for line in (root / ".env").read_text().splitlines():
    if "=" in line and not line.lstrip().startswith("#"):
        k, v = line.split("=", 1); env[k.strip()] = v.strip().strip('"').strip("'")
key = env.get("GEMINI_API_KEY") or os.environ.get("GEMINI_API_KEY")
if not key:
    sys.exit("GEMINI_API_KEY not found in .env")
API = "https://generativelanguage.googleapis.com/v1beta"

def call(path, body=None):
    req = urllib.request.Request(f"{API}/{path}", data=json.dumps(body).encode() if body else None,
                                 headers={"Content-Type": "application/json", "x-goog-api-key": key})
    with urllib.request.urlopen(req, timeout=180) as r:
        return json.load(r)

def image_models():
    models = call("models?pageSize=200").get("models", [])
    return [m["name"].split("/", 1)[1] for m in models
            if "image" in m["name"].lower() and "generateContent" in m.get("supportedGenerationMethods", [])]

if len(sys.argv) > 1 and sys.argv[1] == "list":
    print("\n".join(image_models())); sys.exit(0)

# best text rendering first; fall back to whatever the key can see
candidates = image_models()
PREFERRED = ["gemini-3-pro-image", "gemini-3.1-flash-image", "gemini-2.5-flash-image"]
model = next((m for m in PREFERRED if m in candidates), None) or (sorted(candidates)[-1] if candidates else None)
if not model:
    sys.exit("this key has no image-generation model available")
if len(sys.argv) > 1 and sys.argv[1].startswith("--model="): model = sys.argv[1].split("=", 1)[1]
print("model:", model)

STYLE = ("Clean technical illustration for a software README, flat vector style, white background, "
         "muted blue/grey palette with one red accent for blocked items, generous whitespace, crisp legible sans-serif "
         "labels, no gradients, no 3D, no people, no watermark. Spell every label exactly as given.")

PROMPTS = {
    "flowchart.png": STYLE + " A left-to-right FLOW CHART of one DNS lookup in a home network. Boxes with arrows: "
        "'Phone asks: where is ads.tracker.net?' -> 'ESP32 board (192.168.1.53)' -> diamond 'On the block list?' -> "
        "YES branch (red) -> 'Answer 0.0.0.0' -> 'Ad never loads'; NO branch -> 'Ask Cloudflare 1.1.1.1' -> "
        "'Real address' -> 'Page loads'. Include a small note box: 'Router (DHCP) tells every device to ask the board first'. "
        "Landscape, 16:9.",
    "diagram.png": STYLE + " A SYSTEM DIAGRAM of a home network ad blocker. Centre: a small ESP32-S3 circuit board labelled "
        "'adblocker  192.168.1.53' with a green LED. Left: a Wi-Fi router labelled 'Router (DHCP: DNS 1 = board, DNS 2 = 1.1.1.1)'. "
        "Bottom: devices labelled 'Phones', 'Laptops', 'TV', 'Guests' with arrows to the board captioned 'DNS questions'. "
        "Right: a cloud labelled 'Cloudflare 1.1.1.1' with an arrow from the board captioned 'allowed lookups'. "
        "Top right: a browser window labelled 'Dashboard  adblocker.local' and a small puzzle-piece icon labelled "
        "'YouTube extension (laptops)' with a dotted line to the board captioned 'rules + counts'. A red crossed-out box "
        "labelled 'Ad servers' with 'blocked' beside the board. Landscape, 16:9.",
}

out_dir = root / "docs"; out_dir.mkdir(exist_ok=True)
for name, prompt in PROMPTS.items():
    body = {"contents": [{"parts": [{"text": prompt}]}],
            "generationConfig": {"responseModalities": ["IMAGE", "TEXT"], "imageConfig": {"aspectRatio": "16:9"}}}
    try:
        resp = call(f"models/{model}:generateContent", body)
    except urllib.error.HTTPError as e:   # older models reject imageConfig; retry without it
        if e.code != 400: raise
        del body["generationConfig"]["imageConfig"]
        resp = call(f"models/{model}:generateContent", body)
    parts = resp.get("candidates", [{}])[0].get("content", {}).get("parts", [])
    img = next((p["inlineData"] for p in parts if "inlineData" in p), None)
    if not img:
        print(name, "-> no image returned:", json.dumps(resp)[:300]); continue
    data = base64.b64decode(img["data"])
    (out_dir / name).write_bytes(data)
    print(f"{name}: {len(data)//1024} KB, {img.get('mimeType')}")
