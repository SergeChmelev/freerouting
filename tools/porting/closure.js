// Computes the transitive closure of Java classes reachable from root classes.
// usage: node closure.js <srcRoot> <root fqcn>... [--exclude prefix,...] [--list]
const fs = require('fs');
const path = require('path');
const args = process.argv.slice(2);
const srcRoot = args.shift();
let excludes = [];
let list = false;
const roots = [];
while (args.length) {
  const a = args.shift();
  if (a === '--exclude') excludes = args.shift().split(',');
  else if (a === '--list') list = true;
  else roots.push(a);
}
const files = [];
(function walk(d) {
  for (const e of fs.readdirSync(d, { withFileTypes: true })) {
    const p = path.join(d, e.name);
    if (e.isDirectory()) walk(p);
    else if (e.name.endsWith('.java')) files.push(p);
  }
})(srcRoot);
const byFq = new Map();
const byPkg = new Map();
for (const f of files) {
  const src = fs.readFileSync(f, 'utf8');
  const pkg = (src.match(/^package\s+([\w.]+);/m) || [])[1] || '';
  const name = path.basename(f, '.java');
  const fq = pkg + '.' + name;
  const code = src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '').replace(/"(?:\\.|[^"\\])*"/g, '""');
  const imports = [...src.matchAll(/^import\s+(static\s+)?([\w.]+)(\.\*)?;/gm)].map(m => ({ s: !!m[1], n: m[2], w: !!m[3] }));
  const tokens = new Set(code.match(/\b[A-Z]\w*\b/g) || []);
  const fqRefs = new Set((code.match(/\bapp\.freerouting(?:\.\w+)+/g) || []));
  const lines = src.split('\n').length;
  const info = { f, pkg, name, fq, imports, tokens, fqRefs, lines };
  byFq.set(fq, info);
  if (!byPkg.has(pkg)) byPkg.set(pkg, []);
  byPkg.get(pkg).push(info);
}
function resolveFq(n) {
  // n may be pkg.Class or pkg.Class.Inner or pkg.Class.member (static import)
  const parts = n.split('.');
  for (let i = parts.length; i > 1; i--) {
    const c = parts.slice(0, i).join('.');
    if (byFq.has(c)) return c;
  }
  return null;
}
const seen = new Set();
const queue = [];
function add(fq) {
  if (!fq || seen.has(fq)) return;
  if (excludes.some(x => fq.startsWith(x))) return;
  seen.add(fq);
  queue.push(fq);
}
roots.forEach(add);
while (queue.length) {
  const info = byFq.get(queue.shift());
  if (!info) continue;
  for (const im of info.imports) {
    if (!im.n.startsWith('app.freerouting')) continue;
    if (im.w) {
      const pk = im.s ? null : im.n;
      for (const o of byPkg.get(pk) || []) if (info.tokens.has(o.name)) add(o.fq);
      if (im.s) add(resolveFq(im.n));
    } else add(resolveFq(im.n));
  }
  for (const r of info.fqRefs) add(resolveFq(r));
  for (const o of byPkg.get(info.pkg) || []) if (o !== info && info.tokens.has(o.name)) add(o.fq);
}
const out = [...seen].filter(x => byFq.has(x)).sort();
const perPkg = new Map();
let total = 0;
for (const fq of out) {
  const i = byFq.get(fq);
  total += i.lines;
  const p = perPkg.get(i.pkg) || { n: 0, l: 0 };
  p.n++; p.l += i.lines;
  perPkg.set(i.pkg, p);
}
for (const [p, v] of [...perPkg].sort()) console.log(String(v.l).padStart(7), String(v.n).padStart(4), p);
console.log(String(total).padStart(7), String(out.length).padStart(4), 'TOTAL');
if (list) for (const fq of out) console.log(fq, byFq.get(fq).lines);
