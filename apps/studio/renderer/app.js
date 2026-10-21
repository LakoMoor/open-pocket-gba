// Open Pocket Studio, renderer.
const $ = s => document.querySelector(s);
const S = window.studio;
const kb = n => n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.round(n / 1024) + ' KB';
const ago = t => { const s = (Date.now() - t) / 1000; return s < 3600 ? Math.max(1, Math.round(s / 60)) + ' min ago' : s < 86400 ? Math.round(s / 3600) + ' h ago' : Math.round(s / 86400) + ' d ago'; };

// ---------------------------------------------------------------- tabs
document.querySelectorAll('.tab').forEach(t => t.onclick = () => {
  document.querySelectorAll('.tab').forEach(x => x.classList.toggle('on', x === t));
  document.querySelectorAll('.page').forEach(p => p.classList.toggle('on', p.id === 'p-' + t.dataset.tab));
});
function seg(el, fn) {
  el.querySelectorAll('button').forEach(b => b.onclick = () => {
    el.querySelectorAll('button').forEach(x => x.classList.toggle('on', x === b));
    fn(b.dataset.src);
  });
}

// ---------------------------------------------------------------- status
let st = { build: {}, drives: [] }, release = null, src = 'release', sdsrc = 'release';
const picked = {};
async function status() {
  st = await S.status();
  $('#dot').classList.toggle('on', !!st.port);
  $('#status').textContent = st.port ? `${st.connected ? 'connected' : 'board'} on ${st.port}` : 'board not found';
  renderInstall();
  renderDrives();
}
$('.badge').onclick = () => S.connect().then(status);

// ---------------------------------------------------------------- install
function item(label, value, onclick, primary, disabled) {
  const b = document.createElement('button');
  b.className = 'item' + (primary ? ' primary' : '');
  b.innerHTML = `<span></span><span class="v"></span>`;
  b.firstChild.textContent = label;
  b.lastChild.textContent = value || '';
  b.disabled = !!disabled;
  b.onclick = onclick;
  return b;
}
function renderInstall() {
  const l = $('#ilist');
  l.innerHTML = '';
  if (src === 'release') {
    const r = release;
    const v = !r ? 'checking…' : r.error ? (r.error.includes('404') ? 'no release published yet' : r.error) : `${r.tag} · ${new Date(r.published).toLocaleDateString()}`;
    l.append(item('Install the latest release', v, () => start(() => S.installRelease()), true, !r || r.error || !(r.fw || r.fpga)));
    if (r && !r.error) {
      l.append(item('Firmware', r.fw ? kb(r.fw.size) : 'not in the release', null, false, true));
      l.append(item('FPGA bitstream', r.fpga ? kb(r.fpga.size) : 'not in the release', null, false, true));
    }
  } else if (src === 'build') {
    const b = st.build, v = k => b[k] ? `${kb(b[k].size)} · ${ago(b[k].mtime)}` : 'not built';
    l.append(item('Update everything', 'bitstream + firmware', () => start(() => S.installBuild('all')), true, !b.bitstream || !b.firmware));
    l.append(item('Firmware', v('firmware'), () => start(() => S.installBuild('firmware')), false, !b.firmware));
    l.append(item('FPGA bitstream', v('bitstream'), () => start(() => S.installBuild('bitstream')), false, !b.bitstream));
    l.append(item('Game image', v('games'), () => start(() => S.installBuild('games')), false, !b.games));
    l.append(item('Try the bitstream until power off', 'SRAM', () => start(() => S.installBuild('sram')), false, !b.bitstream));
  } else {
    for (const [k, label] of [['bitstream', 'FPGA bitstream (.fs or .bin)'], ['firmware', 'Firmware (fw.bin)'], ['games', 'Game image (.bin)']]) {
      l.append(item(label, picked[k] ? picked[k].split(/[\\/]/).pop() : 'choose…', async () => {
        const f = await S.pick(k);
        if (f) { picked[k] = f; renderInstall(); }
      }));
    }
    const items = ['bitstream', 'firmware', 'games'].filter(k => picked[k]).map(k => ({ kind: k, file: picked[k] }));
    l.append(item('Install the chosen files', items.length ? items.length + ' file(s)' : '', () => start(() => S.install(items)), true, !items.length));
  }
}
seg($('#src'), s => { src = s; renderInstall(); });

let steps = [];
function start(fn) {
  steps = [];
  $('#steps').innerHTML = '';
  $('#bar').style.width = '0';
  $('#msg').className = 'msg';
  $('#prog').classList.add('show');
  document.querySelectorAll('#ilist .item').forEach(b => b.disabled = true);
  fn();
}
S.on('job', e => {
  if (e.state === 'step') {
    if (!steps.includes(e.label)) {
      steps.push(e.label);
      $('#steps').innerHTML = steps.map(s => `<span class="s">${s}</span>`).join('');
    }
    $('#steps').querySelectorAll('.s').forEach((x, i) => { x.className = 's' + (i < steps.indexOf(e.label) ? ' done' : i === steps.indexOf(e.label) ? ' on' : ''); });
    const total = e.steps > 1 ? ((e.step + e.pct / 100) / e.steps) * 100 : e.pct;
    $('#bar').style.width = total.toFixed(1) + '%';
    $('#phase').textContent = `${e.label}: ${e.phase}`;
    $('#pct').textContent = Math.round(total) + '%';
  } else if (e.state === 'done') {
    $('#bar').style.width = '100%';
    $('#pct').textContent = '100%';
    $('#steps').querySelectorAll('.s').forEach(x => x.className = 's done');
    const m = $('#msg'); m.className = 'msg ok'; m.textContent = `${e.msg}. The board restarts on its own.`;
    renderInstall();
  } else if (e.state === 'error') {
    const m = $('#msg'); m.className = 'msg err'; m.textContent = e.msg;
    renderInstall();
  }
});

// ---------------------------------------------------------------- SD card
function renderDrives() {
  const l = $('#drives');
  l.innerHTML = '';
  if (!st.drives.length) { l.append(item('No SD card found', 'insert a card reader', null, false, true)); return; }
  for (const d of st.drives) {
    const [id, name, size] = d.split('|');
    l.append(item(`Copy to ${id} ${name || ''}`, size ? kb(+size) : '', async () => {
      const m = $('#sdmsg'); m.className = 'msg info'; m.textContent = 'Copying…';
      const r = await S.sd(d, sdsrc);
      m.className = 'msg ' + (r.ok ? 'ok' : 'err');
      m.textContent = r.ok ? `Copied ${r.copied.join(' and ')} to ${id}. Put the card in the board: Menu › System › System Update.` : r.msg;
    }, true));
  }
}
seg($('#sdsrc'), s => { sdsrc = s; });

// ---------------------------------------------------------------- remote: menu pictures
let font = [];
const PAL = [0x101010, 0x000000, 0x0F380F, 0x242424, 0x3C3C3C, 0x7A7A7A, 0xBEBEBE, 0xFFFFFF,
             0x3A6FF0, 0x9BBC0F, 0xF5C518, 0xE5483C, 0x306230, 0x0E1A33, 0x8BAC0F, 0x000000];
function draw(canvas, rows, cols, top) {
  const g = canvas.getContext('2d'), img = g.createImageData(canvas.width, canvas.height);
  for (let cy = 0; cy < rows.length; cy++) for (let cx = 0; cx < cols; cx++) {
    const v = parseInt(rows[cy].substr(cx * 4, 4), 16), attr = v >> 8, ch = v & 0xFF;
    const fg = PAL[attr & 15], bg = PAL[attr >> 4], code = ch & 0x7F, ext = ch & 0x80;
    for (let py = 0; py < 10; py++) {
      const fy = py - 1;
      const bits = fy < 0 || fy > 7 ? (ext ? font[code * 8 + (fy < 0 ? 0 : 7)] : 0) : font[code * 8 + fy];
      for (let px = 0; px < 8; px++) {
        const c = (bits >> (7 - px)) & 1 ? fg : bg, o = ((top + cy * 10 + py) * canvas.width + cx * 8 + px) * 4;
        img.data[o] = c >> 16; img.data[o + 1] = (c >> 8) & 255; img.data[o + 2] = c & 255; img.data[o + 3] = 255;
      }
    }
  }
  g.putImageData(img, 0, 0);
}
async function shot() {
  const r = await S.shot();
  if (!r) return log('(no menu open: press MENU first)\n');
  draw($('#lcd'), r.lcd, 16, 4);
  draw($('#osd'), r.osd, 42, 0);
}
$('#shot').onclick = shot;
setInterval(() => { if ($('#auto').checked && $('#p-remote').classList.contains('on')) shot(); }, 2000);

// ---------------------------------------------------------------- remote: buttons
document.querySelectorAll('.k').forEach(b => {
  const k = b.dataset.k;
  b.onpointerdown = () => { b.classList.add('down'); S.key(k, true); };
  const up = () => { if (b.classList.contains('down')) { b.classList.remove('down'); S.key(k, false); } };
  b.onpointerup = up; b.onpointerleave = up;
});
const keymap = { ArrowUp: 'U', ArrowDown: 'D', ArrowLeft: 'L', ArrowRight: 'R', x: 'A', z: 'B', Enter: 'T', Backspace: 'S', m: 'M' };
const held = {};
addEventListener('keydown', e => {
  if (!$('#p-remote').classList.contains('on')) return;
  const k = keymap[e.key]; if (!k || held[k]) return;
  held[k] = 1; e.preventDefault(); S.key(k, true);
  const b = document.querySelector(`.k[data-k="${k}"]`); if (b) b.classList.add('down');
});
addEventListener('keyup', e => {
  const k = keymap[e.key]; if (!k || !held[k]) return;
  held[k] = 0; S.key(k, false);
  const b = document.querySelector(`.k[data-k="${k}"]`); if (b) b.classList.remove('down');
});

// ---------------------------------------------------------------- log
const logEl = $('#log');
function log(t) {
  logEl.textContent += t;
  if (logEl.textContent.length > 80000) logEl.textContent = logEl.textContent.slice(-60000);
  logEl.scrollTop = logEl.scrollHeight;
}
S.on('log', log);
S.on('status', status);

(async () => {
  font = (await S.font()).split(/\r?\n/).filter(Boolean).map(h => parseInt(h, 16));
  status();
  setInterval(status, 5000);
  release = await S.releaseInfo();
  renderInstall();
})();
