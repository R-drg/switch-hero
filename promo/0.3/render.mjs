// Renders the motions in motion.html to 1920x1080 30 fps MP4s, one per scene
// plus the whole reel, by stepping renderFrame() frame by frame in headless
// Chromium and piping the frames into ffmpeg.
//
//   node promo/0.3/render.mjs                 every scene and the reel
//   node promo/0.3/render.mjs 01-multijogador one scene
//   node promo/0.3/render.mjs --stills DIR    a few PNG stills per scene
//
// Needs Playwright (with Chromium) and an ffmpeg with libx264; set FFMPEG to
// point at one if it is not on PATH.
import { spawn } from 'node:child_process';
import { createReadStream, existsSync, mkdirSync, writeFileSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const outDir = join(here, 'out');
const FFMPEG = process.env.FFMPEG || 'ffmpeg';
const FPS = 30;

let playwright;
try { playwright = await import('playwright'); } catch {
  const req = createRequire(join(process.execPath, '../../lib/node_modules/'));
  playwright = req('playwright');
}

const types = { '.html': 'text/html', '.ttf': 'font/ttf', '.js': 'text/javascript', '.mjs': 'text/javascript' };
const server = createServer((req, res) => {
  const p = join(root, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  if (!p.startsWith(root) || !existsSync(p) || statSync(p).isDirectory()) { res.writeHead(404).end(); return; }
  res.writeHead(200, { 'content-type': types[extname(p)] || 'application/octet-stream' });
  createReadStream(p).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/promo/0.3/motion.html`;

const browser = await playwright.chromium.launch({ args: ['--disable-gpu-vsync'] });
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1 });
page.on('pageerror', e => console.error('page error:', e.message));
await page.goto(url);
await page.evaluate(() => window.ready);
const scenes = await page.evaluate(() => window.SCENES);

const grab = (id, t) => page.evaluate(([id, t]) => {
  window.renderFrame(id, t);
  return document.getElementById('c').toDataURL('image/jpeg', 0.95);
}, [id, t]).then(d => Buffer.from(d.slice(d.indexOf(',') + 1), 'base64'));

function ffmpeg(args) {
  const p = spawn(FFMPEG, ['-hide_banner', '-loglevel', 'error', '-y', ...args], { stdio: ['pipe', 'inherit', 'inherit'] });
  const done = new Promise((ok, fail) => p.on('close', c => (c ? fail(new Error('ffmpeg exited ' + c)) : ok())));
  return { stdin: p.stdin, done };
}
const encode = ['-c:v', 'libx264', '-preset', 'slow', '-crf', '20', '-maxrate', '12M', '-bufsize', '24M', '-pix_fmt', 'yuv420p', '-movflags', '+faststart'];

const args = process.argv.slice(2);
mkdirSync(outDir, { recursive: true });
if (args[0] === '--stills') {
  const dir = resolve(args[1] || join(outDir, 'stills'));
  mkdirSync(dir, { recursive: true });
  for (const s of scenes) for (const f of [.3, .5, .75]) {
    const t = +(s.dur * f).toFixed(2);
    writeFileSync(join(dir, `${s.id}@${t}.jpg`), await grab(s.id, t));
  }
  console.log('stills in', dir);
} else {
  const pick = args.length ? scenes.filter(s => args.includes(s.id)) : scenes;
  const reel = args.length ? null : ffmpeg(['-f', 'image2pipe', '-c:v', 'mjpeg', '-framerate', String(FPS), '-i', '-', ...encode, join(outDir, 'switch-hero-0.3.mp4')]);
  for (const s of pick) {
    const clip = ffmpeg(['-f', 'image2pipe', '-c:v', 'mjpeg', '-framerate', String(FPS), '-i', '-', ...encode, join(outDir, `${s.id}.mp4`)]);
    const n = Math.round(s.dur * FPS), t0 = Date.now();
    for (let i = 0; i < n; i++) {
      const jpg = await grab(s.id, i / FPS);
      if (!clip.stdin.write(jpg)) await new Promise(r => clip.stdin.once('drain', r));
      if (reel && !reel.stdin.write(jpg)) await new Promise(r => reel.stdin.once('drain', r));
    }
    clip.stdin.end(); await clip.done;
    console.log(`${s.id}: ${n} frames in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
  }
  if (reel) { reel.stdin.end(); await reel.done; console.log('reel: switch-hero-0.3.mp4'); }
}
await browser.close();
server.close();
