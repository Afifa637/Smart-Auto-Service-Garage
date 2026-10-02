"""Record the scripted demo reel and encode it to MP4.

How it works
------------
The application, run with `--reel <dir> --reelstart A --reelframes B`, renders
frames A..B-1 of the reel with v-sync off and a fixed 1/24 s timestep, and
dumps one BMP per frame.  Frames before A are simulated but not drawn, so a
run can resume part-way through the reel with the service sequence in exactly
the state continuous playback would have produced.

Two minutes of 1280x720 BMPs is about 8 GB, so this script does not wait for a
run to finish: it consumes each frame as soon as the NEXT one appears (which
proves the file is complete), writes it to the video and deletes it.  Peak
disk use stays under a gigabyte.

The reel is recorded in short chunks and relaunched automatically if a run
ends early, so a renderer window that is closed part-way through costs one
chunk rather than the whole take.

Two files come out of the single pass: the full-resolution master and a
half-size copy for anywhere with an upload limit.

H.264 is not available on this machine (the OpenH264 runtime DLL is absent and
OpenCV has no other H.264 backend), so the container is MPEG-4 Part 2, which
every desktop player and PowerPoint handle natively.

Usage:
    python make_video.py                 # the full two minutes
    python make_video.py --frames 480    # a short test run
"""
import argparse
import os
import subprocess
import sys
import time

import cv2

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)

ap = argparse.ArgumentParser()
ap.add_argument("--frames", type=int, default=2880, help="frame count (2880 = 120 s at 24 fps)")
ap.add_argument("--fps", type=float, default=24.0)
ap.add_argument("--width", type=int, default=1280)
ap.add_argument("--height", type=int, default=720)
ap.add_argument("--chunk", type=int, default=360, help="frames per renderer run")
ap.add_argument("--out", default=os.path.join(HERE, "SmartGarage_Demo_2min.mp4"))
ap.add_argument("--small", default=os.path.join(HERE, "SmartGarage_Demo_2min_540p.mp4"))
ap.add_argument("--frame-dir", default=os.path.join(HERE, "_frames"))
ap.add_argument("--exe", default=os.path.join(PROJ, "x64", "Debug", "YoutubeOpenGL.exe"))
args = ap.parse_args()

if not os.path.isfile(args.exe):
    sys.exit("renderer not found: %s" % args.exe)


def path_for(i):
    return os.path.join(args.frame_dir, "f%05d.bmp" % i)


def clear_frames():
    for f in os.listdir(args.frame_dir):
        try:
            os.remove(os.path.join(args.frame_dir, f))
        except OSError:
            pass


def launch(start, end):
    cmd = [args.exe,
           "--reel", args.frame_dir,
           "--reelstart", str(start),
           "--reelframes", str(end),
           "--size", str(args.width), str(args.height)]
    # CREATE_NEW_CONSOLE: the renderer must own its console and window, as it
    # would if started from Explorer.  Sharing this console, its GLFW window
    # is torn down after a few seconds.
    return subprocess.Popen(cmd, cwd=os.path.dirname(args.exe),
                            stdout=log, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NEW_CONSOLE)


os.makedirs(args.frame_dir, exist_ok=True)
clear_frames()
for p in (args.out, args.small):
    if os.path.exists(p):
        os.remove(p)

log = open(os.path.join(HERE, "reel.log"), "w")
fourcc = cv2.VideoWriter_fourcc(*"mp4v")
big = small = None
sw = sh = 0
next_idx = 0
started = time.time()
attempts = 0

try:
    while next_idx < args.frames:
        attempts += 1
        if attempts > 40:
            print("giving up after %d renderer launches" % attempts, flush=True)
            break

        chunk_start = next_idx
        chunk_end = min(next_idx + args.chunk, args.frames)
        clear_frames()          # a dead run may have left a truncated BMP
        app = launch(chunk_start, chunk_end)
        print("run %d: frames %d..%d" % (attempts, chunk_start, chunk_end - 1), flush=True)
        last_progress = time.time()

        while next_idx < chunk_end:
            cur, nxt = path_for(next_idx), path_for(next_idx + 1)
            done = app.poll() is not None
            ready = os.path.exists(cur) and (
                os.path.exists(nxt) or done or (time.time() - last_progress) > 6.0)
            if not ready:
                if done and not os.path.exists(cur):
                    break                      # the run ended early; relaunch
                if time.time() - last_progress > 180.0:
                    print("  stalled at frame %d" % next_idx, flush=True)
                    break
                time.sleep(0.15)
                continue

            img = cv2.imread(cur, cv2.IMREAD_COLOR)
            if img is None:
                time.sleep(0.1)
                continue

            if big is None:
                h, w = img.shape[:2]
                sw, sh = (w // 2) // 2 * 2, (h // 2) // 2 * 2
                big = cv2.VideoWriter(args.out, fourcc, args.fps, (w, h))
                small = cv2.VideoWriter(args.small, fourcc, args.fps, (sw, sh))
                if not big.isOpened() or not small.isOpened():
                    sys.exit("could not open the video writers")
                print("writing %dx%d and %dx%d" % (w, h, sw, sh), flush=True)

            big.write(img)
            small.write(cv2.resize(img, (sw, sh), interpolation=cv2.INTER_AREA))
            try:
                os.remove(cur)
            except OSError:
                pass
            next_idx += 1
            last_progress = time.time()

        if app.poll() is None:
            app.terminate()
        print("  -> %d / %d frames (%.1fs of footage, %.0fs elapsed)"
              % (next_idx, args.frames, next_idx / args.fps, time.time() - started), flush=True)
finally:
    # Whatever happened above, the files must be closed properly: an MP4 whose
    # writer was never released has no index and will not play.
    for w_ in (big, small):
        if w_ is not None:
            w_.release()
    log.close()

print("", flush=True)
for p in (args.out, args.small):
    if os.path.exists(p):
        print("%s  %.1f MB" % (p, os.path.getsize(p) / (1024.0 * 1024.0)), flush=True)
print("encoded %d frames = %.1f s in %d renderer launches"
      % (next_idx, next_idx / args.fps, attempts), flush=True)

# ---- verify the files actually open --------------------------------------
for p in (args.out, args.small):
    cap = cv2.VideoCapture(p)
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    f = cap.get(cv2.CAP_PROP_FPS)
    ok, _ = cap.read()
    cap.release()
    print("verify %s: %d frames, %.0f fps, %.1f s, readable=%s"
          % (os.path.basename(p), n, f, n / f if f else 0, ok), flush=True)

clear_frames()
try:
    os.rmdir(args.frame_dir)
except OSError:
    pass
