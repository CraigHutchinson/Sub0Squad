#include "sub0squad/portal.hpp"

namespace sub0squad {

const char* portalPage()
{
    return R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sub0Squad portal</title>
<style>
:root{--bg:#f7f7f5;--panel:#fff;--ink:#1c1c1a;--mute:#6b6b66;--line:#d9d9d3;--pending:#9a9a94;--running:#2f6fd6;--accepted:#2a8a4a;--failed:#c23a30;--edge:#a9a9a2;--accent:#7a4fd0;--warn:#b7791f}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--panel:#20201e;--ink:#ecece8;--mute:#9a9a94;--line:#3a3a36;--pending:#77776f;--running:#5b9bff;--accepted:#4cc274;--failed:#ff6b5e;--edge:#5c5c56;--accent:#a98bf0;--warn:#e0a23a}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 system-ui,sans-serif}
header{padding:12px 16px;border-bottom:1px solid var(--line);display:flex;gap:16px;align-items:baseline;flex-wrap:wrap}
h1{font-size:16px;margin:0}.mute{color:var(--mute)}main{padding:12px 16px;display:grid;gap:12px;grid-template-columns:1fr;max-width:1500px;margin:0 auto}
.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px}
.tiles{display:flex;gap:10px;flex-wrap:wrap;margin-top:6px}
.tile{border:1px solid var(--line);border-radius:8px;padding:6px 10px;min-width:150px;cursor:pointer;background:var(--panel)}
.tile:hover{border-color:var(--accent)}.tile .v{font-size:19px;font-weight:600}.tile .l{font-size:12px;color:var(--mute)}.tile svg{display:block;margin-top:2px}
.kpis{display:flex;gap:18px;flex-wrap:wrap}.kpi b{display:block;font-size:20px}
svg{max-width:100%}.graph svg{width:100%;height:auto;display:block;overflow:visible}
.node{cursor:pointer}.node rect.box{fill:var(--panel);stroke-width:2}.node text{fill:var(--ink);font-size:12px}.node .sub{fill:var(--mute);font-size:10.5px}.node .live{fill:var(--running);font-size:10.5px}
.s-pending rect.box{stroke:var(--pending)}.s-running rect.box{stroke:var(--running);animation:pulse 1.4s ease-in-out infinite}.s-accepted rect.box{stroke:var(--accepted)}.s-failed rect.box{stroke:var(--failed)}
.node.sel rect.box{stroke-width:4}
.node.cont rect.box{fill:color-mix(in srgb,var(--ink) 4%,var(--panel));stroke-dasharray:none}.node.cont.s-pending rect.box{stroke-dasharray:6 4}
.node.act-divide rect.box,.node.act-architect rect.box,.node.act-integrate rect.box{stroke:var(--accent);fill:color-mix(in srgb,var(--accent) 14%,var(--panel))}.node.act-integrate rect.box{stroke:#1a9a8a}
.actdot{fill:var(--accent);stroke:var(--panel);stroke-width:2}.actdot.int{fill:#1a9a8a}.acttext{fill:#fff;font-size:13px;font-weight:700;text-anchor:middle}.badge{display:inline-block;padding:0 7px;border-radius:9px;font-size:11px;font-weight:600;border:1px solid var(--accent);color:var(--accent);vertical-align:1px;cursor:help}.badge.fr{border-color:var(--warn);color:var(--warn)}.badge.mock{border-color:var(--mute);color:var(--mute)}.bpill{fill:none;stroke:var(--accent);stroke-width:1.2}.bpill.fr{stroke:var(--warn)}.btext{fill:var(--accent);font-size:10px;font-weight:600}.btext.fr{fill:var(--warn)}@keyframes pulse{50%{stroke-opacity:.35}}
.edge{fill:none;stroke:var(--edge);stroke-width:1.5}.edge.parent{stroke-dasharray:4 4;stroke:var(--accent)}
.lane{fill:none;stroke:var(--line);stroke-dasharray:2 5}.lanelabel{fill:var(--mute);font-size:11px}
.spark{fill:none;stroke:var(--running);stroke-width:1.5}.spark.b{stroke:var(--accepted)}.spark.c{stroke:var(--warn)}.sparkfill{fill:var(--running);opacity:.12}
table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:4px 8px;border-bottom:1px solid var(--line);font-size:13px;vertical-align:middle}
tr.click{cursor:pointer}tr.click:hover,tr.selrow{background:var(--bg)}
.pill{display:inline-block;padding:0 8px;border-radius:10px;font-size:12px;border:1px solid var(--line)}.pill.warn{color:var(--warn);border-color:var(--warn)}.pill.bad{color:var(--failed);border-color:var(--failed)}
.legend span{margin-right:14px}.dot{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:4px;border:2px solid}
.bar{height:8px;background:var(--line);border-radius:4px;overflow:hidden;min-width:90px}.bar i{display:block;height:100%;background:var(--running)}
#drawer{position:fixed;top:0;right:0;width:min(420px,100vw);height:100vh;overflow:auto;background:var(--panel);border-left:1px solid var(--line);padding:14px;transform:translateX(105%);transition:transform .18s;z-index:5;box-shadow:-6px 0 20px rgba(0,0,0,.15)}
#drawer.open{transform:none}#drawer h2{margin:0 0 6px;font-size:16px}#drawer .x{position:absolute;top:8px;right:12px;cursor:pointer;font-size:20px;color:var(--mute);background:none;border:0}
.kv{display:grid;grid-template-columns:130px 1fr;gap:2px 8px;font-size:13px;margin:8px 0}.kv span:nth-child(odd){color:var(--mute)}
pre.viewer{white-space:pre-wrap;word-break:break-word;font-size:12px;max-height:78vh;overflow:auto;border:1px solid var(--line);padding:8px;border-radius:6px}
details>summary{cursor:pointer}.fl{padding-left:12px;font-size:13px}
code{font-size:12px;word-break:break-all}
.rq{border:1px solid var(--line);border-radius:8px;padding:8px 10px;margin:6px 0}.rq.block{border-color:var(--warn)}.rq textarea,.addform textarea{width:100%;font:inherit;background:var(--bg);color:var(--ink);border:1px solid var(--line);border-radius:6px;padding:6px;margin:6px 0}
.btn{font:inherit;cursor:pointer;border:1px solid var(--accent);color:var(--accent);background:none;border-radius:6px;padding:2px 10px;margin:2px 6px 2px 0}.btn.warn{border-color:var(--failed);color:var(--failed)}.btn.opt{border-color:var(--line);color:var(--mute)}
.addform label{display:inline-block;margin-right:12px;font-size:13px}.node.blocked rect.box{stroke-dasharray:6 4}
.reqline{margin:2px 0}.triage{border-left:3px solid var(--warn);padding:2px 8px;margin:6px 0;font-size:13px}.triage.fatal{border-left-color:var(--failed)}.triage.guidance{border-left-color:var(--accepted)}.offline .btn{opacity:.4;pointer-events:none}.warnbar{border:1px solid var(--warn);color:var(--warn);border-radius:6px;padding:4px 8px;margin:6px 0;font-size:13px}
@media(min-width:900px){main{grid-template-columns:3fr 2fr}.wide{grid-column:1/-1}}
</style></head><body>
<header><h1>Sub0Squad portal</h1><span class="mute" title="Small work: be the expert and do it best. Larger work: divide it well and make sure the separation and the interactions hold. The squad is part of your team.">Many small minds, one whole solution.</span><span id="asof" class="mute">connecting…</span><span class="mute">read-only · loopback · live 1 s · click anything for detail</span></header>
<main>
<section class="card wide"><div class="kpis" id="kpis"></div></section>
<section class="card wide" id="intent"><b>Intent</b> <span class="mute">what was asked, what was added or decided since, and what is waiting for an answer</span>
<div id="in-req"></div>
<b>Inbox</b> <span class="mute" id="in-count"></span><div id="offlinebar"></div><div id="inbox"></div>
<details id="addbox"><summary>Add a requirement, clarification or question to the running project</summary>
<div class="addform"><div><label><input type="radio" name="addkind" value="requirement" checked> requirement</label><label><input type="radio" name="addkind" value="clarification"> clarification</label><label><input type="radio" name="addkind" value="question"> question for the architect</label></div>
<div class="mute" style="margin-top:4px">Scope (the nodes it concerns; none ticked = all):</div><div id="add-scope"></div>
<textarea id="add-text" rows="3" placeholder="e.g. The result should keep a history of the last five calculations."></textarea>
<button class="btn" id="add-go">Add to the project</button> <span class="mute" id="in-msg"></span></div></details>
<b>Decisions</b><div id="in-log"></div></section>
<section class="card wide"><b>System</b> <span class="mute" id="sysnote"></span><div class="tiles" id="sys"></div></section>
<section class="card wide"><b>Engines</b> <span class="mute">(live from each llama-server's /slots: prefill progress, tokens decoded, rates)</span><div id="engines"></div></section>
<section class="card wide"><div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:8px"><b id="wfname">Work graph</b>
<span class="legend mute"><span><i class="dot" style="border-color:var(--pending)"></i>pending</span><span><i class="dot" style="border-color:var(--running)"></i>running</span><span><i class="dot" style="border-color:var(--accepted)"></i>accepted</span><span><i class="dot" style="border-color:var(--failed)"></i>failed</span><span>line: depends on · dashed box: divided into parts it integrates</span></span></div>
<div id="graph" class="graph"></div><div class="mute" id="note" style="margin-top:6px"></div></section>
<section class="card wide"><b>Workflow package</b> <span class="mute">the request, the plan, the deliverables and every artifact of every node</span>
<div id="pkg"><div id="pk-info"></div><b>Deliverables</b><div id="pk-del"></div><b>Search the history</b> <span class="mute">conversations, replies, gate outputs, briefings: what a worker saw and said</span>
<div><input id="q" type="search" placeholder="e.g. appended equals, ENOENT, style.css" style="width:min(520px,100%);font:inherit;background:var(--bg);color:var(--ink);border:1px solid var(--line);border-radius:6px;padding:4px 8px"> <span class="mute" id="q-note"></span></div><div id="q-out"></div>
<b>Files</b> <span class="mute" id="pk-count"></span><div id="pk-top"></div><div id="pk-tree"></div></div></section>
<section class="card"><b>Nodes</b><table><thead><tr><th>node</th><th>model</th><th>status</th><th>att.</th><th>in</th><th>out</th><th>time</th></tr></thead><tbody id="nodes"></tbody></table></section>
<section class="card"><b>Decisions</b><table><tbody id="decisions"></tbody></table></section>
<section class="card wide"><b>Daemon jobs</b><table><thead><tr><th>job</th><th>state</th><th>tool / role</th><th>node</th><th>progress</th><th>elapsed</th></tr></thead><tbody id="jobs"></tbody></table></section>
</main>
<aside id="drawer" aria-live="off"><button class="x" id="dx" aria-label="close">×</button><div id="dA"></div><div id="dB"></div><div id="dC"></div></aside>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const fmt=n=>n==null||isNaN(n)?'–':n>=10000?(n/1000).toFixed(1)+'k':n>=1000?(n/1000).toFixed(2)+'k':Math.round(n);
const f1=n=>n==null||isNaN(n)?'–':(+n).toFixed(1);
const last=a=>a&&a.length?a[a.length-1]:null;
const TOKEN='__PORTAL_TOKEN__';   // per run; only this page has it, so a generated page cannot post to the inbox
let state=null,live=null,sel=null,big=null,bigAt=0;   // sel: {type:'node'|'tile'|'engine'|'engtile'|'slot'|'file', key}

// ================= retained-mode DOM updates =================
// A refresh never rebuilds a region whose markup is unchanged, and rebuilds a list only row by row, keyed: elements that did not change keep
// their scroll position, selection, open <details>, hover and running CSS animations. Only the parts that really changed are touched.
const setInner=(el,html)=>{if(el.__html===html)return false;el.__html=html;el.innerHTML=html;return true};
const setText=(el,t)=>{if(el.__text===t)return;el.__text=t;el.textContent=t};
// entries: [{k, html | apply(el), cls, data:{...}}]; children keep their identity by key and are only reordered, patched or removed.
function reconcile(parent,entries,o={}){
  const make=o.ns?(t=>document.createElementNS(o.ns,t)):(t=>document.createElement(t));
  const have=new Map();
  [...parent.children].forEach(c=>{const k=c.getAttribute('data-k');if(k==null)c.remove();else have.set(k,c)});
  let cursor=parent.firstElementChild;
  for(const e of entries){
    const k=String(e.k);let c=have.get(k);
    if(c)have.delete(k);else{c=make(o.tag||'div');c.setAttribute('data-k',k)}
    if(c===cursor)cursor=cursor.nextElementSibling;else parent.insertBefore(c,cursor);
    if(e.cls!==undefined&&c.getAttribute('class')!==e.cls)c.setAttribute('class',e.cls);
    if(e.data)for(const [a,b] of Object.entries(e.data))if(c.getAttribute('data-'+a)!==String(b))c.setAttribute('data-'+a,b);
    if(e.apply)e.apply(c);else setInner(c,e.html);
  }
  have.forEach(c=>c.remove());
}
// While text is selected the page stops repainting, so it can be highlighted and copied; it catches up as soon as the selection is cleared.
const frozen=()=>{const g=window.getSelection();return !!g&&!g.isCollapsed&&g.toString().length>0};

function spark(vals,w,h,cls,max){
  if(!vals||vals.length<2)return `<svg width="${w}" height="${h}"></svg>`;
  const m=Math.max(max||0,...vals,1e-9),n=vals.length,pts=vals.map((v,i)=>[i*(w-2)/(n-1)+1,h-2-(v/m)*(h-4)]);
  const d=pts.map((p,i)=>(i?'L':'M')+p[0].toFixed(1)+','+p[1].toFixed(1)).join(' ');
  return `<svg width="${w}" height="${h}" viewBox="0 0 ${w} ${h}"><path class="sparkfill" d="${d} L${w-1},${h} L1,${h}Z"/><path class="spark ${cls||''}" d="${d}"/></svg>`;
}
// ================= data helpers =================
function sysTiles(){
  const s=live&&live.system;if(!s)return[];
  const gib=m=>(m/1024).toFixed(1)+' GiB';const t=[];
  t.push({k:'cpu',l:'CPU busy',v:f1(last(s.cpu))+' %',a:s.cpu,max:100});
  t.push({k:'mem',l:'Memory used',v:f1(last(s.mem))+' %',sub:gib(s.memTotalMiB*(last(s.mem)||0)/100)+' of '+gib(s.memTotalMiB),a:s.mem,max:100,warn:(last(s.mem)||0)>92});
  t.push({k:'swapMiB',l:'Swap used',v:gib(last(s.swapMiB)||0),sub:'of '+gib(s.swapTotalMiB),a:s.swapMiB,max:Math.max(s.swapTotalMiB,1),warn:(last(s.swapMiB)||0)>512,c:'c'});
  t.push({k:'load',l:'Load average',v:f1(last(s.load)),a:s.load,c:'b'});
  if(s.hasGpu){t.push({k:'gpu',l:'GPU busy',v:f1(last(s.gpu))+' %',a:s.gpu,max:100});
    t.push({k:'vramMiB',l:'GPU VRAM',v:Math.round(last(s.vramMiB)||0)+' MiB',sub:'of '+Math.round(s.vramTotalMiB)+' MiB',a:s.vramMiB,max:s.vramTotalMiB,c:'b'});
    t.push({k:'gttMiB',l:'GPU GTT',v:Math.round(last(s.gttMiB)||0)+' MiB',sub:'of '+Math.round(s.gttTotalMiB)+' MiB',a:s.gttMiB,max:s.gttTotalMiB,c:'b'});}
  return t;
}
const engineKey=base=>base.replace(/\/v1$/,'');
function currentWf(){const w=((state&&state.workflows&&state.workflows.workflows)||[]);return w.length?w[w.length-1]:null}
function profiles(){const m={};const wf=currentWf();((wf&&wf.engines)||[]).forEach(e=>{const head=e.split(',')[0],eq=head.indexOf('=');if(eq<1)return;const name=head.slice(0,eq),url=head.slice(eq+1);
  const g=k=>{const r=e.match(new RegExp('(?:^|,)'+k+'=([^,]*)'));return r?r[1]:''};m[name]={name,base:engineKey(url),model:g('model'),ctx:g('ctx'),slots:g('slots'),seed:g('seed'),cache:g('cache'),thinking:g('thinking'),raw:e}});return m}
function modelInfo(base){const e=((live&&live.engines)||[]).find(x=>x.base===base);return e&&e.model&&(e.model.file||e.model.alias)?e.model:null}
function badgeOf(m,fallback){
  if(!m)return fallback?{short:fallback,quant:'',cls:''}:null;
  const f=m.file||m.alias||'',r=f.match(/(\d+(?:\.\d+)?)B(?:[-_]A(\d+(?:\.\d+)?)B)?/i);
  const size=r?r[1]+'B'+(r[2]?'-A'+r[2]+'B':''):(m.nParams?(m.nParams/1e9).toFixed(m.nParams>=1e10?0:1)+'B':(m.alias||'?'));
  const q=(f.match(/(IQ\d[_A-Z0-9]*|Q\d[_A-Z0-9]*|BF16|F16)/i)||[])[1]||'';
  return {short:size,quant:q,cls:''};
}
function modelTitle(m,prof,tier){
  if(tier==='frontier-stub')return 'frontier tier (stub): nothing is sent anywhere; stands in for a stronger model';
  const lines=[];if(m){lines.push((m.alias||'')+' · '+(m.file||''));if(m.ftype)lines.push('quantisation '+m.ftype);if(m.nParams)lines.push((m.nParams/1e9).toFixed(1)+' B parameters, '+(m.sizeBytes/1073741824).toFixed(1)+' GiB');if(m.nCtxTrain)lines.push('trained context '+m.nCtxTrain);if(m.build)lines.push('llama.cpp '+m.build+', '+m.totalSlots+' slots')}
  if(prof){lines.push('profile '+prof.name+': ctx '+(prof.ctx||'?')+', slots '+(prof.slots||'?')+(prof.seed?', seed '+prof.seed:'')+(prof.cache==='0'?', cache off':''))}
  return lines.join('\n')||'no model information yet';
}
// the badge of a node: from the engine profile that served its attempts (ledger name) or the engine of its running job
function nodeModel(i,job){
  if(i.tier==='frontier-stub')return {b:{short:'frontier',quant:'stub',cls:'fr'},title:modelTitle(null,null,'frontier-stub')};
  const prof=profiles();let p=i.engine?prof[i.engine]:null;
  if(!p&&job&&job.lease)p=Object.values(prof).find(x=>x.base===engineKey(job.lease));
  if(!p){const wf=currentWf();return (wf&&(wf.engines||[]).length===0&&i.attempts>0)?{b:{short:'mock',quant:'',cls:'mock'},title:'mock agent: deterministic templates, no model'}:null}
  const m=modelInfo(p.base);return {b:badgeOf(m,p.model||p.name),title:modelTitle(m,p),prof:p,m};
}
const badgeHtml=(b,title)=>`<span class="badge ${b.cls}" title="${esc(title)}">${esc(b.short)}${b.quant?' '+esc(b.quant):''}</span>`;
function slotOwners(){
  const m={};((state&&state.jobs&&state.jobs.jobs)||[]).forEach(j=>{
    if(!['running','starting'].includes(j.state)||!j.lease||!j.metrics||j.metrics['engine.slot']==null)return;
    m[engineKey(j.lease)+'#'+j.metrics['engine.slot']]={item:j.item,job:j.id,attempt:j.attempt,elapsed:j.elapsedSec};});
  return m;
}
function slotText(s){
  if(!s.processing)return 'idle';
  if(s.promptProcessed<s.promptTokens&&s.decoded===0)return `prefill ${fmt(s.promptProcessed)} / ${fmt(s.promptTokens)}`;
  return `decoding ${fmt(s.decoded)} tok`+(s.remain>=0?` (${fmt(s.remain)} left)`:'')+` · ${f1(s.decodeTps)} t/s`;
}
// ================= system and engines =================
function renderLive(){
  if(frozen()||!live)return;
  reconcile($('sys'),sysTiles().map(t=>({k:t.k,cls:'tile',html:`<div class="l">${t.l}</div><div class="v">${t.v}${t.warn?' <span class="pill warn">high</span>':''}</div><div class="l">${t.sub||'&nbsp;'}</div>${spark(t.a,150,28,t.c,t.max)}`})));
  const owners=slotOwners(),engines=live.engines||[];
  const entries=engines.map(e=>({k:e.base,cls:'eng',apply:c=>{
    if(!c.__sk){c.innerHTML='<div class="enghead" style="margin-top:8px;cursor:pointer"></div><div class="tiles et"></div><table style="margin-top:6px"><thead><tr><th>slot</th><th>node</th><th>activity</th><th>decode t/s (60 s)</th></tr></thead><tbody class="er"></tbody></table>';c.__sk=1}
    const eb=badgeOf(e.model&&(e.model.file||e.model.alias)?e.model:null,null);
    setInner(c.querySelector('.enghead'),`<b>${esc(e.base)}</b> ${eb?`${badgeHtml(eb,modelTitle(e.model,null))} <span class="mute">${esc(e.model.file||e.model.alias)}</span>`:''} <span class="pill ${e.ok?'':'bad'}">${e.ok?'up':'unreachable'}</span> ${e.ok?'':`<span class="mute">${esc(e.error)}</span>`}`);
    reconcile(c.querySelector('.et'),[['decode','Decode now',f1(last(e.decodeTps))+' tok/s',e.decodeTps,''],['prefill','Prefill now',f1(last(e.prefillTps))+' tok/s',e.prefillTps,'b'],['active','Slots busy',(last(e.active)||0)+' of '+e.slots.length,e.active,'c']]
      .map(x=>({k:x[0],cls:'tile',html:`<div class="l">${x[1]}</div><div class="v">${x[2]}</div>${spark(x[3],150,28,x[4])}`})));
    reconcile(c.querySelector('.er'),e.slots.map(s=>{const o=owners[e.base+'#'+s.id];
      const pre=s.processing&&s.promptProcessed<s.promptTokens&&s.decoded===0,pct=s.promptTokens?Math.min(100,100*s.promptProcessed/s.promptTokens):0;
      return {k:s.id,cls:'click',data:{s:s.id},html:`<td>slot ${s.id}</td><td>${o?esc(o.item)+' <span class="mute">#'+o.attempt+'</span>':'<span class="mute">—</span>'}</td><td>${esc(slotText(s))}${pre?`<div class="bar"><i style="width:${pct}%"></i></div>`:''}</td><td>${spark(s.decodeSeries.slice(-60),90,20,'',null)}</td>`}}),{tag:'tr'});
  }}));
  if(!engines.length)entries.push({k:'none',html:'<p class="mute">No llama-server found yet: engines are discovered from the workflow profiles (or start the portal with --engine-url http://127.0.0.1:PORT).</p>'});
  reconcile($('engines'),entries);
  setText($('sysnote'),live.system&&live.system.memTotalMiB?`${Math.round(live.system.memTotalMiB/1024)} GiB RAM`:'');
}
// ================= work graph =================
// ---- layered, compound layout: a divided parent is a container holding its parts; units are layered by dependency, ordered by barycentre to cut
// ---- crossings, and placed deterministically from the plan alone (status never moves a node). Coordinates are absolute, computed in one pass.
const GW=230,GH=76,GX=46,GY=22,CPAD=14,CHEAD=44,CFOOT=26;
function buildTree(items){
  const byId=Object.fromEntries(items.map(i=>[i.id,i]));
  const parentOf=i=>{let id=i.id;while(id.includes('.')){id=id.slice(0,id.lastIndexOf('.'));const p=byId[id];if(p&&p.persona==='integrator')return id}return null};
  const kids={},roots=[];
  items.forEach(i=>{const p=parentOf(i);if(p)(kids[p]=kids[p]||[]).push(i.id);else roots.push(i.id)});
  return {byId,kids,roots,order:Object.fromEntries(items.map((i,n)=>[i.id,n]))};
}
function layoutScope(T,unitIds){
  // unit of an item inside this scope = the member whose subtree contains it, else null (outside this scope)
  const subtree=id=>{const out=[id];(T.kids[id]||[]).forEach(k=>out.push(...subtree(k)));return out};
  const unitOf={};unitIds.forEach(u=>subtree(u).forEach(x=>unitOf[x]=u));
  const preds={},succ={};unitIds.forEach(u=>{preds[u]=new Set();succ[u]=new Set()});
  unitIds.forEach(u=>subtree(u).forEach(x=>(T.byId[x].deps||[]).forEach(d=>{const du=unitOf[d];if(du&&du!==u){preds[u].add(du);succ[du].add(u)}})));
  const layer={};const lay=u=>layer[u]!=null?layer[u]:(layer[u]=0,layer[u]=[...preds[u]].reduce((m,p)=>Math.max(m,lay(p)+1),0));
  unitIds.forEach(lay);
  const nLayers=Math.max(0,...Object.values(layer))+1,cols=Array.from({length:nLayers},()=>[]);
  [...unitIds].sort((a,b)=>T.order[a]-T.order[b]).forEach(u=>cols[layer[u]].push(u));
  const idx=u=>cols[layer[u]].indexOf(u);
  const bary=(u,set)=>{const a=[...set];return a.length?a.reduce((s,p)=>s+idx(p)/Math.max(1,cols[layer[p]].length-1||1),0)/a.length:null};
  for(let sweep=0;sweep<4;sweep++){   // down then up: order each column by the mean position of its neighbours, keeping the previous order on ties
    for(let c=1;c<nLayers;c++){const prev=Object.fromEntries(cols[c].map((u,n)=>[u,n]));cols[c].sort((a,b)=>{const ba=bary(a,preds[a]),bb=bary(b,preds[b]);return (ba==null||bb==null)?prev[a]-prev[b]:(ba-bb)||(prev[a]-prev[b])})}
    for(let c=nLayers-2;c>=0;c--){const prev=Object.fromEntries(cols[c].map((u,n)=>[u,n]));cols[c].sort((a,b)=>{const ba=bary(a,succ[a]),bb=bary(b,succ[b]);return (ba==null||bb==null)?prev[a]-prev[b]:(ba-bb)||(prev[a]-prev[b])})}
  }
  // size every unit (a container is as big as its own layout plus a header and a footer), then place the columns
  const size={},inner={};
  unitIds.forEach(u=>{const k=T.kids[u];if(k&&k.length){const l=layoutScope(T,k);inner[u]=l;size[u]={w:l.w+2*CPAD,h:l.h+CHEAD+CFOOT+CPAD}}else size[u]={w:GW,h:GH}});
  const colW=cols.map(c=>Math.max(1,...c.map(u=>size[u].w))),colH=cols.map(c=>c.reduce((s,u)=>s+size[u].h,0)+GY*Math.max(0,c.length-1));
  const H=Math.max(1,...colH);let x=0;const pos={},nodes=[],edges=[];
  cols.forEach((c,ci)=>{let y=(H-colH[ci])/2;c.forEach(u=>{pos[u]={x,y,w:size[u].w,h:size[u].h};y+=size[u].h+GY});x+=colW[ci]+GX});
  const W=Math.max(1,x-GX);
  unitIds.forEach(u=>{const p=pos[u],l=inner[u];
    if(l){nodes.push({id:u,kind:'container',x:p.x,y:p.y,w:p.w,h:p.h});
      l.nodes.forEach(n=>nodes.push({...n,x:n.x+p.x+CPAD,y:n.y+p.y+CHEAD}));
      l.edges.forEach(e=>edges.push({a:[e.a[0]+p.x+CPAD,e.a[1]+p.y+CHEAD],b:[e.b[0]+p.x+CPAD,e.b[1]+p.y+CHEAD]}))}
    else nodes.push({id:u,kind:'leaf',x:p.x,y:p.y,w:p.w,h:p.h})});
  unitIds.forEach(v=>preds[v].forEach(u=>edges.push({a:[pos[u].x+pos[u].w,pos[u].y+pos[u].h/2],b:[pos[v].x,pos[v].y+pos[v].h/2]})));
  return {w:W,h:H,nodes,edges};
}
function nodeLive(i,jobsByItem,engines){
  const j=jobsByItem[i.id];if(!j)return null;
  const slotNo=j.metrics&&j.metrics['engine.slot'];const base=j.lease?engineKey(j.lease):null;
  const eng=engines.find(e=>e.base===base);const s=eng&&slotNo!=null?eng.slots.find(x=>x.id===slotNo):null;
  return {job:j,slot:s,slotNo,base};
}
const clipText=(s,n)=>{s=String(s);return s.length>n?s.slice(0,n-1)+'…':s};
let graphSig=null;
function drawGraph(items,wfId){
  const host=$('graph');
  items=items.map(i=>({persona:'',deps:[],parent:'',tokensIn:0,tokensOut:0,durationSec:0,engine:'',...i}));
  if(!items.length){if(graphSig!=='none'){graphSig='none';host.innerHTML='<p class="mute">No work items yet.</p>'}return}
  // the live job behind each node; a division or a card rewrite raised by the node shows on the node itself (highest rank wins)
  const rank={decompose:4,rearchitect:3,agent:2,gate:1},jobsByItem={};
  ((state.jobs&&state.jobs.jobs)||[]).forEach(j=>{if(j.workflow===wfId&&j.item&&['running','starting'].includes(j.state)&&rank[j.role]&&(!jobsByItem[j.item]||rank[j.role]>rank[jobsByItem[j.item].role]))jobsByItem[j.item]=j});
  const actOf=(i,j)=>!j?null:j.role==='decompose'?{cls:'act-divide',label:'dividing into parts',icon:'D',tip:'The model is dividing this item into smaller parts ('+j.id+'). When it is done the parts appear inside it and it integrates them.'}
    :j.role==='rearchitect'?{cls:'act-architect',label:'rewriting its card',icon:'A',tip:'An architect job is rewriting this item\'s work card, shorter and focused on what failed ('+j.id+').'}
    :(i.persona==='integrator'&&j.role==='gate')?{cls:'act-integrate',label:'integrating its parts',icon:'I',int:true,tip:'All parts are accepted: this node runs its own tests over them together ('+j.id+').'}:null;
  const engines=(live&&live.engines)||[];
  const wfNow=currentWf(),openReq=((wfNow&&wfNow.requests)||[]).filter(r=>r.status==='open'&&r.blocking);
  const blockedBy=id=>openReq.find(r=>!(r.scope||[]).length||r.scope.includes(id));
  const T=buildTree(items),L=layoutScope(T,T.roots),PX=14,PY=14,width=L.w+2*PX,height=L.h+2*PY;
  // edges: one cubic per dependency, anchored mid-height on the facing sides; the shape of the plan decides when the frame is rebuilt
  let shell='';
  L.edges.forEach(e=>{const x1=e.a[0]+PX,y1=e.a[1]+PY,x2=e.b[0]+PX,y2=e.b[1]+PY,mx=(x1+x2)/2;shell+=`<path class="edge" d="M${x1},${y1} C${mx},${y1} ${mx},${y2} ${x2},${y2}"/>`});
  const sig=width+'x'+height+'|'+shell;
  if(sig!==graphSig){graphSig=sig;host.innerHTML=`<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="work graph"><g class="lc"></g>${shell}<g class="ll"></g></svg>`}   // containers underneath, then the edges (so those inside a container show), then the nodes
  const nodes=L.nodes.slice().sort((a,b)=>(a.kind==='container'?0:1)-(b.kind==='container'?0:1)).map(n=>{
    const i=T.byId[n.id],x=n.x+PX,yy=n.y+PY,W=n.w,H=n.h,job=jobsByItem[i.id],Lv=i.status=='running'?nodeLive(i,jobsByItem,engines):null,act=i.status==='running'?actOf(i,job):null;
    const NM=i.attempts>0||Lv?nodeModel(i,job):null,bw=NM?(NM.b.short.length+(NM.b.quant?NM.b.quant.length+1:0))*6.3+10:0;
    const badge=NM?`<g><title>${esc(NM.title)}</title><rect class="bpill ${NM.b.cls}" x="${x+W-8-bw}" y="${yy+6}" width="${bw}" height="16" rx="8"/><text class="btext ${NM.b.cls}" x="${x+W-3-bw}" y="${yy+17}">${esc(NM.b.short)}${NM.b.quant?' '+esc(NM.b.quant):''}</text></g>`:'';
    const actIcon=act?`<g><title>${esc(act.tip)}</title><circle class="actdot${act.int?' int':''}" cx="${x}" cy="${yy}" r="11"/><text class="acttext" x="${x}" y="${yy+4.5}">${act.icon}</text></g>`:'';
    const bq=i.status!=='accepted'?blockedBy(i.id):null;
    const short=i.id.includes('.')?i.id.slice(i.id.lastIndexOf('.')+1):i.id,maxId=Math.max(6,Math.floor((W-26-bw)/7));
    const cls=`node ${n.kind==='container'?'cont ':''}s-${i.status}${act?' '+act.cls:''}${bq?' blocked':''}${sel&&sel.type=='node'&&sel.key==i.id?' sel':''}`;
    if(n.kind==='container'){
      const rounds=i.attempts?`round${i.attempts==1?'':'s'} ${i.attempts}`:'waiting for its parts';
      return {k:i.id,cls,html:`<title>${esc(i.id)} · ${esc(i.title||'')}</title><rect class="box" x="${x}" y="${yy}" width="${W}" height="${H}" rx="10"/>
      <text x="${x+12}" y="${yy+19}"><tspan font-weight="600">${esc(clipText(short,maxId))}</tspan> <tspan class="sub">integrates its parts</tspan></text>
      <text class="sub" x="${x+12}" y="${yy+35}">${esc(act?act.label:i.status)} · ${esc(rounds)}${bq?' · waiting for an answer ('+esc(bq.id)+')':''}</text>
      <text class="sub" x="${x+12}" y="${yy+H-9}">tokens in ${fmt(i.tokensIn)} · out ${fmt(i.tokensOut)}</text>${badge}${actIcon}`}}
    let liveLine='';
    if(Lv){const j=Lv.job,pr=j.progress;let t=act?`${act.label} · ${Math.round(j.elapsedSec||0)} s`:`${j.role} running ${Math.round(j.elapsedSec||0)} s`;
      if(Lv.slot)t=Lv.slot.processing?(act?act.label+' · ':'')+slotText(Lv.slot):t;else if(pr&&pr.total)t+=` · ${pr.phase||''} ${pr.done}/${pr.total}`;
      liveLine=`<text class="live" x="${x+10}" y="${yy+66}">${esc(clipText((Lv.slotNo!=null?'slot '+Lv.slotNo+' · ':'')+t,Math.floor((W-20)/5.6)))}</text>`;
      if(Lv.slot)liveLine+=`<g transform="translate(${x+W-82},${yy+34})">${spark(Lv.slot.decodeSeries.slice(-40),72,22,'',null)}</g>`}
    else if(bq)liveLine=`<text class="live" x="${x+10}" y="${yy+66}" style="fill:var(--warn)">waiting for an answer (${esc(bq.id)})</text>`;
    return {k:i.id,cls,html:`<title>${esc(i.id)} · ${esc(i.title||'')}</title><rect class="box" x="${x}" y="${yy}" width="${W}" height="${H}" rx="8"/>
    <text x="${x+10}" y="${yy+18}"><tspan font-weight="600">${esc(clipText(short,maxId))}</tspan> <tspan class="sub">${esc(i.persona)}</tspan></text>
    <text class="sub" x="${x+10}" y="${yy+34}">${esc(act?act.label:i.status)} · attempt${i.attempts==1?'':'s'} ${i.attempts}${i.engine?' · '+esc(i.engine):''}${(i.healed||[]).length?' · healed: '+esc(i.healed.join(', ')):(i.mends||[]).length?' · mending: '+esc(i.mends.join(', ')):''}</text>
    <text class="sub" x="${x+10}" y="${yy+50}">tokens in ${fmt(i.tokensIn)} · out ${fmt(i.tokensOut)}</text>${liveLine}${badge}${actIcon}`}});
  const NS='http://www.w3.org/2000/svg';
  reconcile(host.querySelector('.lc'),nodes.filter(n=>n.cls.includes(' cont ')||n.cls.startsWith('node cont')),{tag:'g',ns:NS});
  reconcile(host.querySelector('.ll'),nodes.filter(n=>!(n.cls.includes(' cont ')||n.cls.startsWith('node cont'))),{tag:'g',ns:NS});
}
// ================= workflow package =================
let pkg=null,pkgWf=null,pkgAt=0,fileView=null;
// generated sites are served from the next port: a different origin from this page, with no network access of their own
const siteBase=()=>location.protocol+'//'+location.hostname+':'+(+location.port+1);
async function loadPkg(force){const wf=currentWf();if(!wf||document.hidden)return;if(!force&&pkgWf===wf.id&&Date.now()-pkgAt<5000)return;pkgAt=Date.now();pkgWf=wf.id;
  try{pkg=await (await fetch('/api/files?wf='+encodeURIComponent(wf.id),{cache:'no-store'})).json();renderPkg();if(sel&&sel.type=='node')drawDrawer()}catch(e){}}
const fl=(p,label)=>`<a href="#" class="flink" data-p="${esc(p)}">${esc(label||p)}</a>`;
function groupFiles(files){const g={};files.forEach(f=>{const parts=f.p.split('/');const k=parts[0]==='items'&&parts.length>2?'items/'+parts[1]:parts[0]==='project'?'project':'(workflow)';(g[k]=g[k]||[]).push(f)});return g}
function renderPkg(){
  const wf=currentWf();if(!wf||!pkg||frozen())return;
  const d=pkg.deliverables&&pkg.deliverables.items||[];const files=pkg.files||[];const g=groupFiles(files);
  const kind={'web-page':'page','report':'report','source-tree':'sources'};
  setInner($('pk-info'),`<div class="kv"><span>request</span><span>${esc(wf.taskText||wf.task)}</span><span>workflow</span><span>${esc(wf.id)} · ${esc(wf.state)} · <code>${esc(wf.sandbox)}</code></span><span>engines</span><span>${(wf.engines||[]).map(e=>esc(e)).join('<br>')||'mock agents'}</span></div>`);
  setInner($('pk-del'),d.length?'<table><tbody>'+d.map(x=>`<tr><td><span class="pill">${esc(kind[x.kind]||x.kind)}</span></td><td>${esc(x.title)}${x.run?`<br><span class="mute"><code>${esc(x.run)}</code></span>`:''}</td><td>${x.open==='site'?`<a href="${siteBase()}/site/${encodeURIComponent(wf.id)}/${esc((x.path||'').replace(/^project\//,'')||'index.html')}" target="_blank" rel="noopener">open ↗</a>`:x.open==='file'?fl(x.path,'view'):''}</td></tr>`).join('')+'</tbody></table>'+(pkg.deliverables.summary?`<div class="mute">${esc(pkg.deliverables.summary)}</div>`:'')
    :`<div class="mute">${wf.state==='running'?'The deliverables appear when the integrator has passed.':'No deliverables manifest in this workflow.'}</div>`);
  setText($('pk-count'),`${files.length} files, per-attempt workspaces (ws/) left out`);
  setInner($('pk-top'),(g['(workflow)']||[]).map(f=>f.p).sort().map(p=>fl(p)).join(' · '));
  // one <details> per group, kept across refreshes (so it stays open); only its list is rewritten, and only when it changed
  reconcile($('pk-tree'),Object.keys(g).filter(k=>k!=='(workflow)').sort().map(k=>({k,apply:c=>{
    if(!c.__sk){c.innerHTML='<summary></summary><div class="fl"></div>';c.__sk=1}
    setInner(c.querySelector('summary'),`${esc(k)} <span class="mute">(${g[k].length})</span>`);
    setInner(c.querySelector('.fl'),g[k].sort((a,b)=>a.p<b.p?-1:1).map(f=>`<div>${fl(f.p,f.p.replace(k+'/',''))} <span class="mute">${fmt(f.s)} B</span></div>`).join(''));}})),{tag:'details'});
}
let searchTimer=null;
$('q').addEventListener('input',()=>{clearTimeout(searchTimer);searchTimer=setTimeout(runSearch,350)});
async function runSearch(){
  const wf=currentWf(),text=$('q').value.trim();if(!wf)return;
  if(text.length<3){setText($('q-note'),'');setInner($('q-out'),'');return}
  try{const j=await (await fetch('/api/search?wf='+encodeURIComponent(wf.id)+'&q='+encodeURIComponent(text),{cache:'no-store'})).json();
    setText($('q-note'),j.hits.length+' line'+(j.hits.length==1?'':'s')+(j.hits.length>=40?' (best 40)':''));
    setInner($('q-out'),j.hits.map(h=>`<div class="reqline"><span class="pill">${esc(h.kind)}</span> ${fl(h.p,h.p.replace(/^items\//,''))}<span class="mute">:${h.line}</span> <span class="mute">${esc(h.text)}</span></div>`).join('')||'<span class="mute">nothing matched</span>')}catch(e){setText($('q-note'),'search failed: '+e)}}
// A transcript (conversation.md) or a gate output is followed live: re-read every 1.5 s, the end of a long file, the view kept at the bottom while it grows.
const followable=p=>/(^|\/)(conversation(-skeleton)?\.md|gate\.txt|stdout\.log|stderr\.log)$/.test(p);
function fileUrl(fv){return '/api/file?wf='+encodeURIComponent(fv.wf)+'&p='+encodeURIComponent(fv.p)+(fv.follow?'&tail=200000':'')}
async function loadFile(){const fv=fileView;if(!fv)return;try{const r=await fetch(fileUrl(fv),{cache:'no-store'});const t=r.ok?await r.text():'(not available: '+r.status+')';if(fileView===fv&&t!==fv.text){fv.text=t;drawDrawer()}}catch(e){if(fileView===fv&&fv.text=='loading…'){fv.text='(error: '+e+')';drawDrawer()}}}
async function openFile(p){const wf=currentWf();if(!wf)return;sel={type:'file',key:p};fileView={p,text:'loading…',wf:wf.id,follow:/conversation(-skeleton)?\.md$/.test(p)};openDrawerShell();drawDrawer();await loadFile()}
function setFollow(on){if(fileView){fileView.follow=!!on;loadFile()}}
async function tickFile(){if(document.hidden||busy.file||!fileView||!fileView.follow||!sel||sel.type!='file')return;busy.file=true;try{await loadFile()}finally{busy.file=false}}
// the link that opens the latest transcript of a node, followed live
function watchLink(i,run){const att=run&&run.role=='agent'?run.attempt:Math.max(1,i.attempts||1);return `<div style="margin:6px 0">${fl('items/'+i.id+'/attempt-'+att+'/conversation.md','▶ watch transcript (attempt '+att+')')}${run?' <span class="mute">live</span>':''}</div>`}
// ================= intent: the request, requirements, the inbox and the decisions =================
const kindLabel={question:'question',clarification:'clarification',requirement:'requirement',blocker:'blocker',proposal:'proposal',escalation:'escalation'};
function renderIntent(){
  const wf=currentWf();if(!wf){return}
  const rq=wf.requests||[];
  const reqs=[{n:'REQ-1',t:wf.taskText||wf.task,by:'owner',when:wf.submittedAt}].concat(rq.filter(r=>r.kind==='requirement'&&r.status==='answered').map((r,i)=>({n:'REQ-'+(i+2),t:r.answer||r.detail||r.title,by:r.answeredBy||r.from,when:r.answeredAt,scope:r.scope})));
  setInner($('in-req'),reqs.map(r=>`<div class="reqline"><span class="pill">${esc(r.n)}</span> ${esc(r.t)} <span class="mute">· ${esc(r.by)}${r.scope&&r.scope.length?' · applies to '+esc(r.scope.join(', ')):''}</span></div>`).join('')+((wf.reasons||[]).length?`<div class="mute" style="margin-top:4px">Assumptions made when planning: ${(wf.reasons||[]).map(esc).join(' · ')}</div>`:''));
  // an answer can only be delivered to a running daemon: say so, and do not offer buttons that cannot work
  const daemonUp=(state.workflows&&state.workflows.source)==='live';
  setInner($('offlinebar'),daemonUp?'':'<div class="warnbar">The daemon is not running, so this is the last recorded state: answers cannot be delivered until it is started again (<code>sub0squad serve</code>); the workflow resumes from its journal.</div>');
  $('inbox').classList.toggle('offline',!daemonUp);
  const open=rq.filter(r=>r.status==='open'),openBlocking=open.filter(r=>r.blocking).length;
  setText($('in-count'),open.length?`${open.length} open${openBlocking?`, ${openBlocking} holding work`:''}`:'nothing waiting');
  reconcile($('inbox'),open.length?open.map(r=>({k:r.id,cls:'rq'+(r.blocking?' block':''),data:{rid:r.id,kind:r.kind},html:
    `<span class="pill">${esc(kindLabel[r.kind]||r.kind)}</span> <b>${esc(r.title)}</b> <span class="mute">${esc(r.id)} · from ${esc(r.from)}${(r.scope||[]).length?' · scope '+esc(r.scope.join(', ')):''}${r.blocking?' · holding the work':''}</span>`+
    (r.detail?`<div style="margin-top:4px;white-space:pre-wrap">${esc(r.detail)}</div>`:'')+
    (r.kind==='escalation'&&(r.scope||[]).length===1?`<div class="mute">${fl('items/'+r.scope[0]+'/briefing-'+r.id+'.md','Briefing for a decider')} <span class="mute">(the situation, the harness facts, the evidence and what was tried, ready to hand to an architect or a frontier model)</span></div>`:'')+
    (r.triage?`<div class="triage ${esc(r.triage)}"><b>Overseer: ${r.triage==='fatal'?'a fault no worker can fix':r.triage==='owner'?'the manager decides how to proceed':'handled automatically'}.</b> ${esc(r.analysis)}</div>`:'')+
    ((r.options||[]).length?`<div>${r.options.map(o=>`<button class="btn opt" data-opt="${esc(o)}">${esc(o)}</button>`).join('')}</div>`:'')+
    `<textarea rows="2" placeholder="Your answer or guidance for the nodes concerned…"></textarea>`+
    (r.kind==='escalation'?`<button class="btn" data-act="retry">Retry with this guidance</button><button class="btn warn" data-act="stop">Stop the workflow</button>`:`<button class="btn" data-act="answer">Send answer</button>`)})):[{k:'none',html:'<span class="mute">No open requests.</span>'}]);
  const done=rq.filter(r=>r.status==='answered').slice().reverse();
  setInner($('in-log'),done.length?done.map(r=>`<div class="reqline"><span class="pill">${esc(kindLabel[r.kind]||r.kind)}</span> <b>${esc(r.title)}</b> <span class="mute">${esc(r.id)} · ${esc(r.from)} → ${esc(r.answeredBy||'')}${r.resolution?' · '+esc(r.resolution):''}</span>${r.analysis&&r.answeredBy==='overseer'?`<div class="mute">Overseer's analysis: ${esc(r.analysis)}</div>`:''}${r.answer?`<div class="mute" style="white-space:pre-wrap">${esc(r.answer)}</div>`:''}</div>`).join(''):'<span class="mute">None yet.</span>');
  reconcile($('add-scope'),wf.items.map(i=>({k:i.id,cls:'',html:`<label><input type="checkbox" value="${esc(i.id)}"> ${esc(i.id)}</label>`})),{tag:'span'});
}
async function postJson(path,body){try{const r=await fetch(path,{method:'POST',headers:{'content-type':'application/json','x-portal-token':TOKEN},body:JSON.stringify(body)});try{return await r.json()}catch(e){return {ok:false,error:'HTTP '+r.status}}}catch(e){return {ok:false,error:'the portal is not reachable (was it restarted? reload this page)'}}}
$('inbox').addEventListener('click',async ev=>{
  const card=ev.target.closest('.rq');if(!card)return;const wf=currentWf();if(!wf)return;
  const opt=ev.target.closest('[data-opt]');if(opt){const ta=card.querySelector('textarea');ta.value=opt.getAttribute('data-opt');ta.focus();return}
  const btn=ev.target.closest('[data-act]');if(!btn)return;
  const res=btn.getAttribute('data-act'),answer=card.querySelector('textarea').value.trim();
  const out=await postJson('/api/answer',{wf:wf.id,spec:{rid:card.getAttribute('data-rid'),answer,resolution:res==='retry'||res==='stop'?res:'',by:'owner'}});
  setText($('in-msg'),out.ok?`answered ${card.getAttribute('data-rid')}`:('not sent: '+(out.error||'?')));tickState()});
$('add-go').addEventListener('click',async()=>{
  const wf=currentWf();if(!wf)return;const text=$('add-text').value.trim();if(!text){setText($('in-msg'),'write something first');return}
  const kind=(document.querySelector('input[name=addkind]:checked')||{}).value||'requirement';
  const scope=[...document.querySelectorAll('#add-scope input:checked')].map(x=>x.value);
  const out=await postJson('/api/raise',{wf:wf.id,spec:{kind,from:'user',title:text.slice(0,80),detail:text,scope,blocking:kind==='question',options:[]}});
  if(out.ok){$('add-text').value='';document.querySelectorAll('#add-scope input').forEach(x=>x.checked=false)}
  setText($('in-msg'),out.ok?`added as ${out.rid}`:('not sent: '+(out.error||'?')));tickState()});
// ================= nodes, decisions, jobs =================
function render(){
  if(!state)return;
  if(frozen()){if(!$('asof').textContent.includes('paused'))$('asof').textContent+=' · paused while text is selected';return}
  const d=state,wfs=(d.workflows&&d.workflows.workflows)||[];
  setText($('asof'),'as of '+new Date((d.workflows.asOf||Date.now()/1000)*1000).toLocaleTimeString()+' · '+(d.workflows.source||''));
  const wf=wfs.length?wfs[wfs.length-1]:null;
  if(!wf){reconcile($('kpis'),[{k:'none',html:'<span class="mute">No workflows yet. Start one with <code>sub0squad do "…"</code>.</span>'}]);drawGraph([],'');reconcile($('nodes'),[],{tag:'tr'});reconcile($('decisions'),[],{tag:'tr'})}
  else{
    const L=wf.ledger||{},eng=Object.entries(L.byEngine||{}).map(([n,e])=>{const pr=profiles()[n],b=pr?badgeOf(modelInfo(pr.base),pr.model):null;return `${esc(n)}${b?' '+badgeHtml(b,modelTitle(modelInfo(pr.base),pr)):''} ${e.decodeTps.toFixed(1)} tok/s (finished)`}).join(' · ')||'—';
    const done=wf.items.filter(i=>i.status=='accepted').length;
    reconcile($('kpis'),[['workflow',esc(wf.id)+' <span class="pill">'+esc(wf.state)+'</span>'],['items accepted',done+' / '+wf.items.length],['jobs active',wf.jobsActive+' of '+wf.jobs],['open requests',String((wf.requests||[]).filter(r=>r.status==='open').length)],
      ['tokens (finished)',fmt(L.totalTokens||0)],['work ratio',((L.workRatio||0)*100).toFixed(1)+' %'],['rework tokens',fmt(L.reworkTokens||0)],['engine',eng]].map(([k,v])=>({k,cls:'kpi',html:`<span class="mute">${k}</span><b>${v}</b>`})));
    setText($('wfname'),'Work graph · '+wf.id+' · '+wf.task);setText($('note'),wf.note||'');
    drawGraph(wf.items,wf.id);
    renderIntent();
    reconcile($('nodes'),wf.items.map(i=>({tokensIn:0,tokensOut:0,durationSec:0,...i})).map(i=>{const nm=i.attempts>0?nodeModel(i,null):null;
      return {k:i.id,cls:'click'+(sel&&sel.type=='node'&&sel.key==i.id?' selrow':''),html:`<td>${esc(i.id)}</td><td>${nm?badgeHtml(nm.b,nm.title):''}</td><td>${esc(i.status)}${(()=>{const j=((state.jobs&&state.jobs.jobs)||[]).find(x=>x.workflow===wf.id&&x.item===i.id&&['running','starting'].includes(x.state)&&(x.role==='decompose'||x.role==='rearchitect'));return j?` <span class="pill">${j.role==='decompose'?'dividing':'rewriting card'}</span>`:''})()}</td><td>${i.attempts}</td><td>${fmt(i.tokensIn)}</td><td>${fmt(i.tokensOut)}</td><td>${Math.round(i.durationSec)} s</td>`}}),{tag:'tr'});
    const ds=(wf.decisions||[]).slice(-12).reverse();
    reconcile($('decisions'),ds.length?ds.map((x,n)=>({k:x.action+'|'+((x.inputs&&x.inputs.item)||'')+'|'+((x.inputs&&x.inputs.attempt)||'')+'|'+(ds.length-n),html:`<td><b>${esc(x.action)}</b> ${esc((x.inputs&&x.inputs.item)||'')} #${esc((x.inputs&&x.inputs.attempt)||'')}<br><span class="mute">${esc((x.reasons||[]).slice(-1)[0]||'')}</span></td>`})):[{k:'none',html:'<td class="mute">none yet</td>'}],{tag:'tr'});
  }
  const logLinks=j=>fl('@jobs/'+j.id+'/run-'+(j.run||1)+'/stdout.log','out')+' '+fl('@jobs/'+j.id+'/run-'+(j.run||1)+'/stderr.log','err')+' '+fl('@jobs/'+j.id+'/run-'+(j.run||1)+'/exit.json','exit');
  reconcile($('jobs'),((d.jobs&&d.jobs.jobs)||[]).slice(-16).reverse().map(j=>({k:j.id,html:`<td>${esc(j.id)}<br><span class="mute">${logLinks(j)}</span></td><td>${esc(j.state)}${j.note&&!['succeeded','running'].includes(j.state)?`<br><span class="mute">${esc(j.note)}</span>`:''}</td><td>${esc(j.tool||'')} ${esc(j.role||'')}${j.service?' <span class="pill">service</span>':''}</td><td>${esc(j.item||'')}</td><td>${j.progress?esc(j.progress.done+'/'+j.progress.total+' '+(j.progress.phase||'')):''}</td><td>${j.elapsedSec!=null?Math.round(j.elapsedSec)+' s':''}</td>`})),{tag:'tr'});
  if($('drawer').classList.contains('open'))drawDrawer();
}
// ================= drawer (detail): three stacked regions, each rewritten only when its own markup changes =================
function openDrawerShell(){const dr=$('drawer');if(!dr.classList.contains('open')){dr.classList.add('open')}dr.scrollTop=0;['dA','dB','dC'].forEach(id=>{$(id).__html=null})}
function openDrawer(s){sel=s;big=null;bigAt=0;openDrawerShell();drawDrawer();render();pollBig(true)}
$('dx').onclick=()=>{$('drawer').classList.remove('open');sel=null;fileView=null;render()};
document.addEventListener('keydown',e=>{if(e.key=='Escape')$('dx').onclick()});
function chart(vals,unit,cls,max){
  if(!vals||vals.length<2)return '<p class="mute">collecting…</p>';
  const mn=Math.min(...vals),mx=Math.max(...vals),av=vals.reduce((a,b)=>a+b,0)/vals.length;
  return `<div>${spark(vals,380,90,cls,max)}</div><div class="mute" style="font-size:12px">last ${vals.length} s · min ${f1(mn)} · avg ${f1(av)} · max ${f1(mx)} ${unit}</div>`;
}
function drawDrawer(){
  if(!sel||frozen())return;const L=big||live||{};let a='',b='',c='';
  if(sel.type=='file'){
    if(!fileView){return}
    const wf=fileView.wf,raw='/api/file?wf='+encodeURIComponent(wf)+'&p='+encodeURIComponent(fileView.p),isSite=fileView.p.startsWith('project/')&&/\.html?$/.test(fileView.p);
    a=`<h2 style="word-break:break-all">${esc(fileView.p)}</h2><div class="mute"><a href="${raw}" target="_blank" rel="noopener">raw</a>${isSite?` · <a href="${siteBase()}/site/${encodeURIComponent(wf)}/${esc(fileView.p.replace(/^project\//,''))}" target="_blank" rel="noopener">open as a page ↗</a>`:''}${followable(fileView.p)?` · <label><input type="checkbox" ${fileView.follow?'checked':''} onchange="setFollow(this.checked)"> follow live</label>`:''}</div><pre class="viewer">${esc(fileView.text)}</pre>`;}
  else if(sel.type=='tile'){const s=L.system||{};const t=sysTiles().find(x=>x.k==sel.key);const m={cpu:['CPU busy','%'],mem:['Memory used','%'],swapMiB:['Swap used','MiB'],load:['Load average',''],gpu:['GPU busy','%'],vramMiB:['GPU VRAM','MiB'],gttMiB:['GPU GTT','MiB']}[sel.key]||[sel.key,''];
    a=`<h2>${m[0]}</h2><div class="mute">now ${t?t.v:''}</div>`;b=chart(s[sel.key],m[1],'',null);}
  else if(sel.type=='engine'){const e=((L.engines)||[]).find(x=>x.base===sel.key);const m=e&&e.model||{};const pr=Object.values(profiles()).filter(x=>x.base===sel.key);const bd=badgeOf(m.file||m.alias?m:null,null);
    a=`<h2>${esc(sel.key)} ${bd?`<span class="badge">${esc(bd.short)}${bd.quant?' '+esc(bd.quant):''}</span>`:''}</h2><div class="kv"><span>model</span><span>${esc(m.alias||'–')}</span><span>file</span><span><code>${esc(m.path||'–')}</code></span><span>quantisation</span><span>${esc(m.ftype||'–')}</span><span>parameters</span><span>${m.nParams?(m.nParams/1e9).toFixed(2)+' B':'–'}</span><span>size</span><span>${m.sizeBytes?(m.sizeBytes/1073741824).toFixed(1)+' GiB':'–'}</span><span>trained context</span><span>${m.nCtxTrain||'–'}</span><span>server</span><span>llama.cpp ${esc(m.build||'–')}, ${m.totalSlots||'?'} slots</span><span>profiles</span><span>${pr.map(x=>esc(x.name)+' (ctx '+esc(x.ctx||'?')+', slots '+esc(x.slots||'?')+(x.seed?', seed '+esc(x.seed):'')+')').join('<br>')||'–'}</span></div>`;
    b=e?`<b>Decode</b>${chart(e.decodeTps,'tok/s','',null)}<b>Prefill</b>${chart(e.prefillTps,'tok/s','b',null)}`:'';}
  else if(sel.type=='engtile'||sel.type=='slot'){const [base,k]=sel.key.split('|');const e=((L.engines)||[]).find(x=>x.base===base);
    if(!e)a='<p class="mute">engine not sampled</p>';
    else if(sel.type=='engtile'){a=`<h2>${esc(base)}</h2>`;b=chart(e[{decode:'decodeTps',prefill:'prefillTps',active:'active'}[k]],k=='active'?'slots':'tok/s',k=='prefill'?'b':'',null);}
    else{const s=e.slots.find(x=>x.id==+k)||{};const o=slotOwners()[base+'#'+k];
      a=`<h2>${esc(base)} · slot ${k}</h2><div class="kv"><span>node</span><span>${o?esc(o.item)+' attempt '+o.attempt+' ('+esc(o.job)+')':'—'}</span><span>state</span><span>${esc(slotText(s))}</span><span>prompt</span><span>${fmt(s.promptProcessed)} of ${fmt(s.promptTokens)} tokens processed</span><span>decoded</span><span>${fmt(s.decoded)} tokens</span></div>${o?`<div style="margin:6px 0">${fl('items/'+o.item+'/attempt-'+o.attempt+'/conversation.md','▶ watch transcript')} <span class="mute">live</span></div>`:''}`;
      b=`<b>Decode</b>${chart(s.decodeSeries,'tok/s','',null)}<b>Prefill</b>${chart(s.prefillSeries,'tok/s','b',null)}`;}}
  else if(sel.type=='node'){const wf=currentWf();const i=wf&&wf.items.find(x=>x.id==sel.key);
    if(!i)a='<p class="mute">node not found</p>';else{
      const j=((state.jobs&&state.jobs.jobs)||[]).filter(x=>x.workflow==wf.id&&x.item==i.id).slice(-4).reverse();const run=j.find(x=>['running','starting'].includes(x.state));
      const sl=run&&run.metrics&&run.metrics['engine.slot'],base=run&&run.lease?engineKey(run.lease):null;const e=((L.engines)||[]).find(x=>x.base===base);const s=e&&sl!=null?e.slots.find(x=>x.id===sl):null;
      const decs=(wf.decisions||[]).filter(x=>x.inputs&&x.inputs.item==i.id).slice(-5).reverse();
      // region A: what changes when the node finishes an attempt; B: what is live; C: lists that change when files, jobs or decisions do
      a=`<h2>${esc(i.id)} <span class="pill">${esc(i.status)}</span></h2><div class="mute">${esc(i.title||'')}</div><div class="kv"><span>persona</span><span>${esc(i.persona||'')}</span>${(i.mends||[]).length?`<span>${(i.healed||[]).length?'healed':'mends applied'}</span><span>${esc(i.mends.join(', '))} (repaired by the harness, outside the ladder)</span>`:''}<span>attempts</span><span>${i.attempts}</span><span>tier / engine</span><span>${esc(i.tier||'–')} / ${esc(i.engine||'–')}</span><span>model</span><span>${(()=>{const nm=nodeModel(i,run);return nm?`${badgeHtml(nm.b,'')}<br><span class="mute" style="white-space:pre-line">${esc(nm.title)}</span>`:'–'})()}</span><span>tokens</span><span>in ${fmt(i.tokensIn||0)} · out ${fmt(i.tokensOut||0)}</span><span>time (finished)</span><span>${Math.round(i.durationSec||0)} s</span><span>depends on</span><span>${esc((i.deps||[]).join(', ')||'—')}</span><span>parent</span><span>${esc(i.parent||'—')}</span><span>files</span><span><code>${esc(wf.sandbox)}/items/${esc(i.id)}/attempt-${i.attempts}/</code></span>${i.attempts>1||i.status==='failed'?`<span>iterate</span><span>in isolation, minutes instead of the whole run: <code>sub0squad replay ${esc(wf.id)} ${esc(i.id)} --seeds 1,2,3</code></span>`:''}</div>`;
      a+=watchLink(i,run);
      if(run)b=`<b>Running now</b><div class="kv"><span>job</span><span>${esc(run.id)} (${esc(run.role)}, attempt ${run.attempt})</span><span>elapsed</span><span>${Math.round(run.elapsedSec||0)} s</span><span>progress</span><span>${run.progress?esc(run.progress.done+'/'+run.progress.total+' '+(run.progress.phase||'')):'–'}</span>${s?`<span>engine slot</span><span>${sl} · ${esc(slotText(s))}</span>`:''}</div>${s?'<b>Decode t/s</b>'+chart(s.decodeSeries,'tok/s','',null)+'<b>Prefill t/s</b>'+chart(s.prefillSeries,'tok/s','b',null):'<p class="mute">No live slot figures for this job (the daemon predates slot pinning, or the engine is not a llama-server).</p>'}`;
      const mine=((pkg&&pkg.files)||[]).filter(f=>f.p.startsWith('items/'+i.id+'/')).sort((x,y)=>x.p<y.p?-1:1);
      c=`<b>Artifacts</b> <span class="mute">${mine.length} files</span><div class="fl" style="padding-left:0">${mine.map(f=>`<div>${fl(f.p,f.p.replace('items/'+i.id+'/',''))} <span class="mute">${fmt(f.s)} B</span></div>`).join('')||'<span class="mute">none yet</span>'}</div>`
        +`<b>Jobs</b><table><tbody>${j.map(x=>`<tr><td>${esc(x.id)}<br><span class="mute">${fl('@jobs/'+x.id+'/run-'+(x.run||1)+'/stdout.log','out')} ${fl('@jobs/'+x.id+'/run-'+(x.run||1)+'/stderr.log','err')} ${fl('@jobs/'+x.id+'/run-'+(x.run||1)+'/exit.json','exit')}</span></td><td>${esc(x.role)} #${x.attempt}</td><td>${esc(x.state)}${x.note&&!['succeeded','running'].includes(x.state)?`<br><span class="mute">${esc(x.note)}</span>`:''}</td><td>${Math.round(x.elapsedSec||0)} s</td></tr>`).join('')||'<tr><td class="mute">none</td></tr>'}</tbody></table>`
        +`<b>Decisions</b><table><tbody>${decs.map(x=>`<tr><td><b>${esc(x.action)}</b> #${esc(x.inputs.attempt)}<br><span class="mute">${esc((x.reasons||[]).slice(-1)[0]||'')}</span></td></tr>`).join('')||'<tr><td class="mute">none</td></tr>'}</tbody></table>`;}}
  const vw0=document.querySelector('pre.viewer'),atEnd=!vw0||vw0.scrollHeight-vw0.scrollTop-vw0.clientHeight<40;
  setInner($('dA'),a);setInner($('dB'),b);setInner($('dC'),c);
  const vw=document.querySelector('pre.viewer');if(vw&&sel&&sel.type=='file'&&fileView&&fileView.follow&&atEnd)vw.scrollTop=vw.scrollHeight;
}
// ================= polling: one request in flight per endpoint, a 5 s timeout, nothing while the tab is hidden =================
const busy={};
async function poll(name,url,apply){
  if(busy[name]||document.hidden)return;busy[name]=true;const ac=new AbortController(),to=setTimeout(()=>ac.abort(),5000);
  try{apply(await (await fetch(url,{cache:'no-store',signal:ac.signal})).json())}catch(e){if(name=='state')setText($('asof'),'portal unreachable: '+e)}
  finally{clearTimeout(to);busy[name]=false}}
// the long series (900 s) behind the drawer's charts, refreshed every 5 s while a chart is open
function pollBig(force){if(!sel||sel.type=='file'||sel.type=='node')return;if(!force&&Date.now()-bigAt<5000)return;bigAt=Date.now();poll('big','/api/live?n=900',j=>{big=j;drawDrawer()})}
const tickState=()=>poll('state','/api/state',j=>{state=j;render();loadPkg()});
const tickLive=()=>poll('live','/api/live?n=120',j=>{live=j;renderLive();if(state)render();if($('drawer').classList.contains('open'))pollBig(false)});
// one delegated handler per region instead of handlers re-attached on every refresh
$('sys').addEventListener('click',ev=>{const t=ev.target.closest('.tile');if(t)openDrawer({type:'tile',key:t.getAttribute('data-k')})});
$('engines').addEventListener('click',ev=>{
  const eng=ev.target.closest('.eng');if(!eng)return;const base=eng.getAttribute('data-k');
  if(ev.target.closest('.enghead'))return openDrawer({type:'engine',key:base});
  const tile=ev.target.closest('.tile');if(tile)return openDrawer({type:'engtile',key:base+'|'+tile.getAttribute('data-k')});
  const row=ev.target.closest('tr');if(row&&row.getAttribute('data-s')!=null)openDrawer({type:'slot',key:base+'|'+row.getAttribute('data-s')})});
$('graph').addEventListener('click',ev=>{const n=ev.target.closest('.node');if(n)openDrawer({type:'node',key:n.getAttribute('data-k')})});
$('nodes').addEventListener('click',ev=>{const r=ev.target.closest('tr');if(r)openDrawer({type:'node',key:r.getAttribute('data-k')})});
document.addEventListener('click',ev=>{const a=ev.target.closest('.flink');if(a){ev.preventDefault();openFile(a.dataset.p)}});
document.addEventListener('selectionchange',()=>{if(!frozen()){renderLive();render()}});
document.addEventListener('visibilitychange',()=>{if(!document.hidden){tickState();tickLive()}});
tickState();tickLive();setInterval(tickState,2000);setInterval(tickLive,1000);setInterval(tickFile,1500);
</script></body></html>)HTML";
}

}  // namespace sub0squad
