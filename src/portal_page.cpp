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
.node rect{fill:var(--panel);stroke-width:2}.node text{fill:var(--ink);font-size:12px}.node .sub{fill:var(--mute);font-size:10.5px}.node .live{fill:var(--running);font-size:10.5px}
.s-pending rect{stroke:var(--pending)}.s-running rect{stroke:var(--running);animation:pulse 1.4s ease-in-out infinite}.s-accepted rect{stroke:var(--accepted)}.s-failed rect{stroke:var(--failed)}
.node.sel rect{stroke-width:4}.badge{display:inline-block;padding:0 7px;border-radius:9px;font-size:11px;font-weight:600;border:1px solid var(--accent);color:var(--accent);vertical-align:1px;cursor:help}.badge.fr{border-color:var(--warn);color:var(--warn)}.badge.mock{border-color:var(--mute);color:var(--mute)}.bpill{fill:none;stroke:var(--accent);stroke-width:1.2}.bpill.fr{stroke:var(--warn)}.btext{fill:var(--accent);font-size:10px;font-weight:600}.btext.fr{fill:var(--warn)}@keyframes pulse{50%{stroke-opacity:.35}}
.edge{fill:none;stroke:var(--edge);stroke-width:1.5}.edge.parent{stroke-dasharray:4 4;stroke:var(--accent)}
.lane{fill:none;stroke:var(--line);stroke-dasharray:2 5}.lanelabel{fill:var(--mute);font-size:11px}
.spark{fill:none;stroke:var(--running);stroke-width:1.5}.spark.b{stroke:var(--accepted)}.spark.c{stroke:var(--warn)}.sparkfill{fill:var(--running);opacity:.12}
table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:4px 8px;border-bottom:1px solid var(--line);font-size:13px;vertical-align:middle}
tr.click{cursor:pointer}tr.click:hover{background:var(--bg)}
.pill{display:inline-block;padding:0 8px;border-radius:10px;font-size:12px;border:1px solid var(--line)}.pill.warn{color:var(--warn);border-color:var(--warn)}.pill.bad{color:var(--failed);border-color:var(--failed)}
.legend span{margin-right:14px}.dot{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:4px;border:2px solid}
.bar{height:8px;background:var(--line);border-radius:4px;overflow:hidden;min-width:90px}.bar i{display:block;height:100%;background:var(--running)}
#drawer{position:fixed;top:0;right:0;width:min(420px,100vw);height:100vh;overflow:auto;background:var(--panel);border-left:1px solid var(--line);padding:14px;transform:translateX(105%);transition:transform .18s;z-index:5;box-shadow:-6px 0 20px rgba(0,0,0,.15)}
#drawer.open{transform:none}#drawer h2{margin:0 0 6px;font-size:16px}#drawer .x{position:absolute;top:8px;right:12px;cursor:pointer;font-size:20px;color:var(--mute);background:none;border:0}
.kv{display:grid;grid-template-columns:130px 1fr;gap:2px 8px;font-size:13px;margin:8px 0}.kv span:nth-child(odd){color:var(--mute)}
code{font-size:12px;word-break:break-all}
@media(min-width:900px){main{grid-template-columns:3fr 2fr}.wide{grid-column:1/-1}}
</style></head><body>
<header><h1>Sub0Squad portal</h1><span id="asof" class="mute">connecting…</span><span class="mute">read-only · loopback · live 1 s · click anything for detail</span></header>
<main>
<section class="card wide"><div class="kpis" id="kpis"></div></section>
<section class="card wide"><b>System</b> <span class="mute" id="sysnote"></span><div class="tiles" id="sys"></div></section>
<section class="card wide"><b>Engines</b> <span class="mute">(live from each llama-server's /slots: prefill progress, tokens decoded, rates)</span><div id="engines"></div></section>
<section class="card wide"><div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:8px"><b id="wfname">Work graph</b>
<span class="legend mute"><span><i class="dot" style="border-color:var(--pending)"></i>pending</span><span><i class="dot" style="border-color:var(--running)"></i>running</span><span><i class="dot" style="border-color:var(--accepted)"></i>accepted</span><span><i class="dot" style="border-color:var(--failed)"></i>failed</span><span>solid: depends on · dashed: divided into</span></span></div>
<div id="graph" class="graph"></div><div class="mute" id="note" style="margin-top:6px"></div></section>
<section class="card wide"><b>Workflow package</b> <span class="mute">the request, the plan, the deliverables and every artifact of every node</span><div id="pkg"></div></section>
<section class="card"><b>Nodes</b><table id="nodes"></table></section>
<section class="card"><b>Decisions</b><table id="decisions"></table></section>
<section class="card wide"><b>Daemon jobs</b><table id="jobs"></table></section>
</main>
<aside id="drawer" aria-live="polite"><button class="x" id="dx" aria-label="close">×</button><div id="dbody"></div></aside>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const fmt=n=>n==null||isNaN(n)?'–':n>=10000?(n/1000).toFixed(1)+'k':n>=1000?(n/1000).toFixed(2)+'k':Math.round(n);
const f1=n=>n==null||isNaN(n)?'–':(+n).toFixed(1);
// While text is selected the page stops repainting, so it can be highlighted and copied; it catches up as soon as the selection is cleared.
const frozen=()=>{const g=window.getSelection();return !!g&&!g.isCollapsed&&g.toString().length>0};
let state=null,live=null,sel=null,big=null;   // sel: {type:'node'|'tile'|'slot', key}
function spark(vals,w,h,cls,max){
  if(!vals||vals.length<2)return `<svg width="${w}" height="${h}"></svg>`;
  const m=Math.max(max||0,...vals,1e-9),n=vals.length,pts=vals.map((v,i)=>[i*(w-2)/(n-1)+1,h-2-(v/m)*(h-4)]);
  const d=pts.map((p,i)=>(i?'L':'M')+p[0].toFixed(1)+','+p[1].toFixed(1)).join(' ');
  return `<svg width="${w}" height="${h}" viewBox="0 0 ${w} ${h}"><path class="sparkfill" d="${d} L${w-1},${h} L1,${h}Z"/><path class="spark ${cls||''}" d="${d}"/></svg>`;
}
const last=a=>a&&a.length?a[a.length-1]:null;
// ---------- live: system and engines
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
function engineKey(base){return base.replace(/\/v1$/,'')}
// ---------- which model: engine profile name -> server -> model identity
function profiles(){const m={};const wf=currentWf();((wf&&wf.engines)||[]).forEach(e=>{const head=e.split(',')[0],eq=head.indexOf('=');if(eq<1)return;const name=head.slice(0,eq),url=head.slice(eq+1);
  const g=k=>{const r=e.match(new RegExp('(?:^|,)'+k+'=([^,]*)'));return r?r[1]:''};m[name]={name,base:engineKey(url),model:g('model'),ctx:g('ctx'),slots:g('slots'),seed:g('seed'),cache:g('cache'),thinking:g('thinking'),raw:e}});return m}
function currentWf(){const w=((state&&state.workflows&&state.workflows.workflows)||[]);return w.length?w[w.length-1]:null}
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
// slot owner: running agent/architect job with a lease on this engine and metric engine.slot
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
function renderLive(){
  if(frozen())return;
  const tiles=sysTiles();
  paint('sys',tiles.map(t=>`<div class="tile" data-k="${t.k}"><div class="l">${t.l}</div><div class="v">${t.v}${t.warn?' <span class="pill warn">high</span>':''}</div><div class="l">${t.sub||'&nbsp;'}</div>${spark(t.a,150,28,t.c,t.max)}</div>`).join(''));
  $('sys').querySelectorAll('.tile').forEach(e=>e.onclick=()=>openDrawer({type:'tile',key:e.dataset.k}));
  const owners=slotOwners();
  $('engines').innerHTML=((live&&live.engines)||[]).map(e=>{
    const act=last(e.active)||0;
    const eb=badgeOf(e.model&&(e.model.file||e.model.alias)?e.model:null,null);
    const head=`<div style="margin-top:8px;cursor:pointer" class="enghead" data-e="${esc(e.base)}"><b>${esc(e.base)}</b> ${eb?`<span class="badge" title="${esc(modelTitle(e.model,null))}">${esc(eb.short)}${eb.quant?' '+esc(eb.quant):''}</span> <span class="mute">${esc(e.model.file||e.model.alias)}</span>`:''} <span class="pill ${e.ok?'':'bad'}">${e.ok?'up':'unreachable'}</span> ${e.ok?'':`<span class="mute">${esc(e.error)}</span>`}</div>`;
    const t=[['decode','Decode now',f1(last(e.decodeTps))+' tok/s',e.decodeTps,''],['prefill','Prefill now',f1(last(e.prefillTps))+' tok/s',e.prefillTps,'b'],['active','Slots busy',act+' of '+e.slots.length,e.active,'c']];
    const tiles=t.map(x=>`<div class="tile" data-e="${esc(e.base)}" data-k="${x[0]}"><div class="l">${x[1]}</div><div class="v">${x[2]}</div>${spark(x[3],150,28,x[4])}</div>`).join('');
    const rows=e.slots.map(s=>{const o=owners[e.base+'#'+s.id];
      const pre=s.processing&&s.promptProcessed<s.promptTokens&&s.decoded===0;const pct=s.promptTokens?Math.min(100,100*s.promptProcessed/s.promptTokens):0;
      return `<tr class="click" data-e="${esc(e.base)}" data-s="${s.id}"><td>slot ${s.id}</td><td>${o?esc(o.item)+' <span class="mute">#'+o.attempt+'</span>':'<span class="mute">—</span>'}</td><td>${esc(slotText(s))}${pre?`<div class="bar"><i style="width:${pct}%"></i></div>`:''}</td><td>${spark(s.decodeSeries.slice(-60),90,20,'',null)}</td></tr>`}).join('');
    return head+`<div class="tiles">${tiles}</div><table style="margin-top:6px"><tr><th>slot</th><th>node</th><th>activity</th><th>decode t/s (60 s)</th></tr>${rows}</table>`}).join('')||'<p class="mute">No llama-server found yet: engines are discovered from the workflow profiles (or start the portal with --engine-url http://127.0.0.1:PORT).</p>';
  $('engines').querySelectorAll('.enghead').forEach(e=>e.onclick=()=>openDrawer({type:'engine',key:e.dataset.e}));
  $('engines').querySelectorAll('.tile').forEach(e=>e.onclick=()=>openDrawer({type:'engtile',key:e.dataset.e+'|'+e.dataset.k}));
  $('engines').querySelectorAll('tr.click').forEach(e=>e.onclick=()=>openDrawer({type:'slot',key:e.dataset.e+'|'+e.dataset.s}));
  $('sysnote').textContent=live&&live.system&&live.system.memTotalMiB?`${Math.round(live.system.memTotalMiB/1024)} GiB RAM`:'';
}
// ---------- graph
function layout(items){
  const byId=Object.fromEntries(items.map(i=>[i.id,i]));const col={};
  const colOf=id=>{if(id in col)return col[id];col[id]=0;const it=byId[id];let c=0;for(const d of it.deps||[])if(byId[d])c=Math.max(c,colOf(d)+1);if(it.parent&&byId[it.parent])c=Math.max(c,colOf(it.parent)+1);return col[id]=c};
  items.forEach(i=>colOf(i.id));const row=i=>(i.id.match(/\./g)||[]).length;const cells={},pos={};
  items.forEach(i=>{const k=col[i.id]+','+row(i);cells[k]=cells[k]||0;pos[i.id]={c:col[i.id],r:row(i),s:cells[k]++}});return pos;
}
function nodeLive(i,jobsByItem,owners,engines){
  const j=jobsByItem[i.id];if(!j)return null;
  const slotNo=j.metrics&&j.metrics['engine.slot'];const base=j.lease?engineKey(j.lease):null;
  const eng=engines.find(e=>e.base===base);const s=eng&&slotNo!=null?eng.slots.find(x=>x.id===slotNo):null;
  return {job:j,slot:s,slotNo,base};
}
const painted={};
function paint(id,html){const el=$(id);if(painted[id]===html)return;painted[id]=html;el.innerHTML=html}
// ---------- workflow package: request, plan, deliverables, files
let pkg=null,pkgWf=null,pkgAt=0,fileView=null;
// generated sites are served from the next port: a different origin from this page, with no network access of their own
const siteBase=()=>location.protocol+'//'+location.hostname+':'+(+location.port+1);
async function loadPkg(force){const wf=currentWf();if(!wf||document.hidden)return;if(!force&&pkgWf===wf.id&&Date.now()-pkgAt<5000)return;pkgAt=Date.now();pkgWf=wf.id;
  try{pkg=await (await fetch('/api/files?wf='+encodeURIComponent(wf.id),{cache:'no-store'})).json();renderPkg()}catch(e){}}
const fl=(p,label)=>`<a href="#" class="flink" data-p="${esc(p)}">${esc(label||p)}</a>`;
function groupFiles(files){const g={};files.forEach(f=>{const parts=f.p.split('/');const k=parts[0]==='items'&&parts.length>2?'items/'+parts[1]:parts[0]==='project'?'project':'(workflow)';(g[k]=g[k]||[]).push(f)});return g}
function renderPkg(){
  const wf=currentWf();if(!wf||!pkg)return;if(frozen())return;
  const d=pkg.deliverables&&pkg.deliverables.items||[];const files=pkg.files||[];const g=groupFiles(files);
  const kind={'web-page':'page','report':'report','source-tree':'sources'};
  let h=`<div class="kv"><span>request</span><span>${esc(wf.taskText||wf.task)}</span><span>workflow</span><span>${esc(wf.id)} · ${esc(wf.state)} · <code>${esc(wf.sandbox)}</code></span><span>engines</span><span>${(wf.engines||[]).map(e=>esc(e)).join('<br>')||'mock agents'}</span></div>`;
  h+=`<b>Deliverables</b>`;
  if(d.length){h+='<table>'+d.map(x=>`<tr><td><span class="pill">${esc(kind[x.kind]||x.kind)}</span></td><td>${esc(x.title)}${x.run?`<br><span class="mute"><code>${esc(x.run)}</code></span>`:''}</td><td>${x.open==='site'?`<a href="${siteBase()}/site/${encodeURIComponent(wf.id)}/${esc((x.path||'').replace(/^project\//,'')||'index.html')}" target="_blank" rel="noopener">open ↗</a>`:x.open==='file'?fl(x.path,'view'):''}</td></tr>`).join('')+'</table>';
    if(pkg.deliverables.summary)h+=`<div class="mute">${esc(pkg.deliverables.summary)}</div>`}
  else h+=`<div class="mute">${wf.state==='running'?'The deliverables appear when the integrator has passed.':'No deliverables manifest in this workflow.'} ${files.some(f=>f.p==='plan.json')?'':''}</div>`;
  h+=`<b>Files</b> <span class="mute">${files.length} files, per-attempt workspaces (<code>ws/</code>) left out</span>`;
  const top=(g['(workflow)']||[]).map(f=>f.p).sort();
  h+=`<div>${top.map(p=>fl(p)).join(' · ')}</div>`;
  h+=Object.keys(g).filter(k=>k!=='(workflow)').sort().map(k=>`<details><summary>${esc(k)} <span class="mute">(${g[k].length})</span></summary><div style="padding-left:12px;font-size:13px">${g[k].sort((a,b)=>a.p<b.p?-1:1).map(f=>`<div>${fl(f.p,f.p.replace(k+'/',''))} <span class="mute">${fmt(f.s)} B</span></div>`).join('')}</div></details>`).join('');
  paint('pkg',h);bindFileLinks($('pkg'));
}
function bindFileLinks(root){root.querySelectorAll('.flink').forEach(a=>a.onclick=e=>{e.preventDefault();openFile(a.dataset.p)})}
async function openFile(p){const wf=currentWf();if(!wf)return;sel={type:'file',key:p};fileView={p,text:'loading…',wf:wf.id};$('drawer').classList.add('open');drawDrawer();
  try{const r=await fetch('/api/file?wf='+encodeURIComponent(wf.id)+'&p='+encodeURIComponent(p),{cache:'no-store'});fileView.text=r.ok?await r.text():'(not available: '+r.status+')'}catch(e){fileView.text='(error: '+e+')'}drawDrawer()}
function drawGraph(items,wfId){
  items=items.map(i=>({persona:'',deps:[],parent:'',tokensIn:0,tokensOut:0,durationSec:0,engine:'',...i}));
  if(!items.length){$('graph').innerHTML='<p class="mute">No work items yet.</p>';return}
  const jobsByItem={};((state.jobs&&state.jobs.jobs)||[]).forEach(j=>{if(j.workflow===wfId&&j.item&&['running','starting'].includes(j.state)&&['agent','gate'].includes(j.role)&&(!jobsByItem[j.item]||j.role=='agent'))jobsByItem[j.item]=j});
  const engines=(live&&live.engines)||[],owners=slotOwners();
  const pos=layout(items),W=215,H=74,GX=44,GY=26,PX=70,PY=34;
  const maxC=Math.max(...Object.values(pos).map(p=>p.c)),maxR=Math.max(...Object.values(pos).map(p=>p.r));
  const stack={};Object.values(pos).forEach(p=>{const k=p.c+','+p.r;stack[k]=Math.max(stack[k]||0,p.s+1)});
  const rowH=r=>Math.max(1,...Object.entries(stack).filter(([k])=>+k.split(',')[1]===r).map(([,v])=>v))*(H+GY);
  const rowY=[];let y=PY;for(let r=0;r<=maxR;r++){rowY[r]=y;y+=rowH(r)+8}
  const width=PX+(maxC+1)*(W+GX),height=y+10;
  let s=`<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="work graph">`;
  for(let r=0;r<=maxR;r++)s+=`<line class="lane" x1="0" x2="${width}" y1="${rowY[r]-10}" y2="${rowY[r]-10}"/><text class="lanelabel" x="4" y="${rowY[r]+12}">level ${r}${r==0?' (top)':''}</text>`;
  const X=p=>PX+p.c*(W+GX),Y=p=>rowY[p.r]+p.s*(H+GY);
  const edge=(a,b,cls)=>{const x1=X(a)+W,y1=Y(a)+H/2,x2=X(b),y2=Y(b)+H/2,mx=(x1+x2)/2;s+=`<path class="edge ${cls}" d="M${x1},${y1} C${mx},${y1} ${mx},${y2} ${x2},${y2}"/>`};
  items.forEach(i=>{for(const d of i.deps||[])if(pos[d])edge(pos[d],pos[i.id],'');if(i.parent&&pos[i.parent])edge(pos[i.parent],pos[i.id],'parent')});
  items.forEach(i=>{const p=pos[i.id],x=X(p),yy=Y(p),L=i.status=='running'?nodeLive(i,jobsByItem,owners,engines):null;
    let liveLine='';
    if(L){const j=L.job,pr=j.progress;let t=`${esc(j.role)} running ${Math.round(j.elapsedSec||0)} s`;
      if(L.slot)t=L.slot.processing?slotText(L.slot):t;else if(pr&&pr.total)t+=` · ${esc(pr.phase||'')} ${pr.done}/${pr.total}`;
      liveLine=`<text class="live" x="${x+10}" y="${yy+66}">${L.slotNo!=null?'slot '+L.slotNo+' · ':''}${t}</text>`;
      if(L.slot)liveLine+=`<g transform="translate(${x+W-82},${yy+34})">${spark(L.slot.decodeSeries.slice(-40),72,22,'',null).replace('<svg','<svg x="0" y="0"')}</g>`}
    const NM=i.attempts>0||L?nodeModel(i,jobsByItem[i.id]):null;
    const bw=NM?(NM.b.short.length+(NM.b.quant?NM.b.quant.length+1:0))*6.3+10:0;
    const badge=NM?`<g><title>${esc(NM.title)}</title><rect class="bpill ${NM.b.cls}" x="${x+W-8-bw}" y="${yy+6}" width="${bw}" height="16" rx="8"/><text class="btext ${NM.b.cls}" x="${x+W-3-bw}" y="${yy+17}">${esc(NM.b.short)}${NM.b.quant?' '+esc(NM.b.quant):''}</text></g>`:'';
    s+=`<g class="node s-${i.status}${sel&&sel.type=='node'&&sel.key==i.id?' sel':''}" data-id="${esc(i.id)}" style="cursor:pointer"><rect x="${x}" y="${yy}" width="${W}" height="${H}" rx="8"/>
    <text x="${x+10}" y="${yy+18}"><tspan font-weight="600">${esc(i.id)}</tspan> <tspan class="sub">${esc(i.persona)}</tspan></text>
    <text class="sub" x="${x+10}" y="${yy+34}">${esc(i.status)} · attempt${i.attempts==1?'':'s'} ${i.attempts}${i.engine?' · '+esc(i.engine):''}</text>
    <text class="sub" x="${x+10}" y="${yy+50}">tokens in ${fmt(i.tokensIn)} · out ${fmt(i.tokensOut)}</text>${liveLine}${badge}</g>`});
  paint('graph',s+'</svg>');
  document.querySelectorAll('.node').forEach(n=>n.onclick=()=>openDrawer({type:'node',key:n.dataset.id}));
}
// ---------- render
function render(){
  if(!state)return;
  if(frozen()){if(!$('asof').textContent.includes('paused'))$('asof').textContent+=' · paused while text is selected';return}const d=state;const wfs=(d.workflows&&d.workflows.workflows)||[];
  $('asof').textContent='as of '+new Date((d.workflows.asOf||Date.now()/1000)*1000).toLocaleTimeString()+' · '+(d.workflows.source||'');
  const wf=wfs.length?wfs[wfs.length-1]:null;
  if(!wf){$('kpis').innerHTML='<span class="mute">No workflows yet. Start one with <code>sub0squad do "…"</code>.</span>';drawGraph([],'');$('nodes').innerHTML=$('decisions').innerHTML='';}
  else{
    const L=wf.ledger||{},eng=Object.entries(L.byEngine||{}).map(([n,e])=>{const pr=profiles()[n],b=pr?badgeOf(modelInfo(pr.base),pr.model):null;return `${esc(n)}${b?` <span class="badge" title="${esc(modelTitle(modelInfo(pr.base),pr))}">${esc(b.short)}${b.quant?' '+esc(b.quant):''}</span>`:''} ${e.decodeTps.toFixed(1)} tok/s (finished)`}).join(' · ')||'—';
    const done=wf.items.filter(i=>i.status=='accepted').length;
    $('kpis').innerHTML=[['workflow',esc(wf.id)+' <span class="pill">'+esc(wf.state)+'</span>'],['items accepted',done+' / '+wf.items.length],['jobs active',wf.jobsActive+' of '+wf.jobs],
      ['tokens (finished)',fmt(L.totalTokens||0)],['work ratio',((L.workRatio||0)*100).toFixed(1)+' %'],['rework tokens',fmt(L.reworkTokens||0)],['engine',eng]]
      .map(([k,v])=>`<div class="kpi"><span class="mute">${k}</span><b>${v}</b></div>`).join('');
    $('wfname').textContent='Work graph · '+wf.id+' · '+wf.task;$('note').textContent=wf.note||'';
    drawGraph(wf.items,wf.id);
    $('nodes').innerHTML='<tr><th>node</th><th>model</th><th>status</th><th>att.</th><th>in</th><th>out</th><th>time</th></tr>'+wf.items.map(i=>({tokensIn:0,tokensOut:0,durationSec:0,...i})).map(i=>{const nm=i.attempts>0?nodeModel(i,null):null;return `<tr class="click" data-id="${esc(i.id)}" style="${sel&&sel.type=='node'&&sel.key==i.id?'background:var(--bg)':''}"><td>${esc(i.id)}</td><td>${nm?`<span class="badge ${nm.b.cls}" title="${esc(nm.title)}">${esc(nm.b.short)}${nm.b.quant?' '+esc(nm.b.quant):''}</span>`:''}</td><td>${esc(i.status)}</td><td>${i.attempts}</td><td>${fmt(i.tokensIn)}</td><td>${fmt(i.tokensOut)}</td><td>${Math.round(i.durationSec)} s</td></tr>`}).join('');
    $('nodes').querySelectorAll('tr.click').forEach(r=>r.onclick=()=>openDrawer({type:'node',key:r.dataset.id}));
    const ds=(wf.decisions||[]).slice(-12).reverse();
    $('decisions').innerHTML=ds.length?ds.map(x=>`<tr><td><b>${esc(x.action)}</b> ${esc((x.inputs&&x.inputs.item)||'')} #${esc((x.inputs&&x.inputs.attempt)||'')}<br><span class="mute">${esc((x.reasons||[]).slice(-1)[0]||'')}</span></td></tr>`).join(''):'<tr><td class="mute">none yet</td></tr>';
  }
  const jobs=((d.jobs&&d.jobs.jobs)||[]).slice(-16).reverse();
  $('jobs').innerHTML='<tr><th>job</th><th>state</th><th>tool / role</th><th>node</th><th>progress</th><th>elapsed</th></tr>'+jobs.map(j=>`<tr><td>${esc(j.id)}</td><td>${esc(j.state)}</td><td>${esc(j.tool||'')} ${esc(j.role||'')}${j.service?' <span class="pill">service</span>':''}</td><td>${esc(j.item||'')}</td><td>${j.progress?esc(j.progress.done+'/'+j.progress.total+' '+(j.progress.phase||'')):''}</td><td>${j.elapsedSec!=null?Math.round(j.elapsedSec)+' s':''}</td></tr>`).join('');
  if($('drawer').classList.contains('open'))drawDrawer();
}
// ---------- drawer (detail)
function openDrawer(s){sel=s;$('drawer').classList.add('open');big=null;fetch('/api/live?n=900',{cache:'no-store'}).then(r=>r.json()).then(j=>{big=j;drawDrawer()}).catch(()=>{});drawDrawer();render()}
$('dx').onclick=()=>{$('drawer').classList.remove('open');sel=null;render()};
document.addEventListener('keydown',e=>{if(e.key=='Escape')$('dx').onclick()});
function chart(vals,unit,cls,max){
  if(!vals||vals.length<2)return '<p class="mute">collecting…</p>';
  const mn=Math.min(...vals),mx=Math.max(...vals),av=vals.reduce((a,b)=>a+b,0)/vals.length;
  return `<div>${spark(vals,380,90,cls,max)}</div><div class="mute" style="font-size:12px">last ${vals.length} s · min ${f1(mn)} · avg ${f1(av)} · max ${f1(mx)} ${unit}</div>`;
}
function drawDrawer(){
  if(!sel||frozen())return;const L=big||live||{};let h='';
  if(sel.type=='file'&&fileView){const wf=fileView.wf;const raw='/api/file?wf='+encodeURIComponent(wf)+'&p='+encodeURIComponent(fileView.p);const isSite=fileView.p.startsWith('project/')&&/\.html?$/.test(fileView.p);
    $('dbody').innerHTML=`<h2 style="word-break:break-all">${esc(fileView.p)}</h2><div class="mute"><a href="${raw}" target="_blank" rel="noopener">raw</a>${isSite?` · <a href="${siteBase()}/site/${encodeURIComponent(wf)}/${esc(fileView.p.replace(/^project\//,''))}" target="_blank" rel="noopener">open as a page ↗</a>`:''}</div><pre style="white-space:pre-wrap;word-break:break-word;font-size:12px;max-height:75vh;overflow:auto;border:1px solid var(--line);padding:8px;border-radius:6px">${esc(fileView.text)}</pre>`;return}
  if(sel.type=='tile'){const s=L.system||{};const t=sysTiles().find(x=>x.k==sel.key);const m={cpu:['CPU busy','%'],mem:['Memory used','%'],swapMiB:['Swap used','MiB'],load:['Load average',''],gpu:['GPU busy','%'],vramMiB:['GPU VRAM','MiB'],gttMiB:['GPU GTT','MiB']}[sel.key]||[sel.key,''];
    h=`<h2>${m[0]}</h2><div class="mute">now ${t?t.v:''}</div>${chart(s[sel.key],m[1],'',null)}`;}
  else if(sel.type=='engine'){const e=((L.engines)||[]).find(x=>x.base===sel.key);const m=e&&e.model||{};const pr=Object.values(profiles()).filter(x=>x.base===sel.key);const b=badgeOf(m.file||m.alias?m:null,null);
    h=`<h2>${esc(sel.key)} ${b?`<span class="badge">${esc(b.short)}${b.quant?' '+esc(b.quant):''}</span>`:''}</h2><div class="kv"><span>model</span><span>${esc(m.alias||'–')}</span><span>file</span><span><code>${esc(m.path||'–')}</code></span><span>quantisation</span><span>${esc(m.ftype||'–')}</span><span>parameters</span><span>${m.nParams?(m.nParams/1e9).toFixed(2)+' B':'–'}</span><span>size</span><span>${m.sizeBytes?(m.sizeBytes/1073741824).toFixed(1)+' GiB':'–'}</span><span>trained context</span><span>${m.nCtxTrain||'–'}</span><span>server</span><span>llama.cpp ${esc(m.build||'–')}, ${m.totalSlots||'?'} slots</span><span>profiles</span><span>${pr.map(x=>esc(x.name)+' (ctx '+esc(x.ctx||'?')+', slots '+esc(x.slots||'?')+(x.seed?', seed '+esc(x.seed):'')+')').join('<br>')||'–'}</span></div><b>Decode</b>${e?chart(e.decodeTps,'tok/s','',null):''}<b>Prefill</b>${e?chart(e.prefillTps,'tok/s','b',null):''}`;}
  else if(sel.type=='engtile'||sel.type=='slot'){const [base,k]=sel.key.split('|');const e=((L.engines)||[]).find(x=>x.base===base);
    if(!e)h='<p class="mute">engine not sampled</p>';
    else if(sel.type=='engtile')h=`<h2>${esc(base)}</h2>${chart(e[{decode:'decodeTps',prefill:'prefillTps',active:'active'}[k]],k=='active'?'slots':'tok/s',k=='prefill'?'b':'',null)}`;
    else{const s=e.slots.find(x=>x.id==+k)||{};const o=slotOwners()[base+'#'+k];
      h=`<h2>${esc(base)} · slot ${k}</h2><div class="kv"><span>node</span><span>${o?esc(o.item)+' attempt '+o.attempt+' ('+esc(o.job)+')':'—'}</span><span>state</span><span>${esc(slotText(s))}</span><span>prompt</span><span>${fmt(s.promptProcessed)} of ${fmt(s.promptTokens)} tokens processed</span><span>decoded</span><span>${fmt(s.decoded)} tokens</span></div><b>Decode</b>${chart(s.decodeSeries,'tok/s','',null)}<b>Prefill</b>${chart(s.prefillSeries,'tok/s','b',null)}`;}}
  else if(sel.type=='node'){const wf=((state.workflows&&state.workflows.workflows)||[]).slice(-1)[0];const i=wf&&wf.items.find(x=>x.id==sel.key);
    if(!i)h='<p class="mute">node not found</p>';else{
      const j=((state.jobs&&state.jobs.jobs)||[]).filter(x=>x.workflow==wf.id&&x.item==i.id).slice(-4).reverse();const run=j.find(x=>['running','starting'].includes(x.state));
      const sl=run&&run.metrics&&run.metrics['engine.slot'],base=run&&run.lease?engineKey(run.lease):null;const e=((L.engines)||[]).find(x=>x.base===base);const s=e&&sl!=null?e.slots.find(x=>x.id===sl):null;
      const decs=(wf.decisions||[]).filter(x=>x.inputs&&x.inputs.item==i.id).slice(-5).reverse();
      h=`<h2>${esc(i.id)} <span class="pill">${esc(i.status)}</span></h2><div class="mute">${esc(i.title||'')}</div><div class="kv"><span>persona</span><span>${esc(i.persona||'')}</span><span>attempts</span><span>${i.attempts}</span><span>tier / engine</span><span>${esc(i.tier||'–')} / ${esc(i.engine||'–')}</span><span>model</span><span>${(()=>{const nm=nodeModel(i,run);return nm?`<span class="badge ${nm.b.cls}">${esc(nm.b.short)}${nm.b.quant?' '+esc(nm.b.quant):''}</span><br><span class="mute" style="white-space:pre-line">${esc(nm.title)}</span>`:'–'})()}</span><span>tokens</span><span>in ${fmt(i.tokensIn||0)} · out ${fmt(i.tokensOut||0)}</span><span>time (finished)</span><span>${Math.round(i.durationSec||0)} s</span><span>depends on</span><span>${esc((i.deps||[]).join(', ')||'—')}</span><span>parent</span><span>${esc(i.parent||'—')}</span><span>files</span><span><code>${esc(wf.sandbox)}/items/${esc(i.id)}/attempt-${i.attempts}/</code></span></div>`;
      if(run)h+=`<b>Running now</b><div class="kv"><span>job</span><span>${esc(run.id)} (${esc(run.role)}, attempt ${run.attempt})</span><span>elapsed</span><span>${Math.round(run.elapsedSec||0)} s</span><span>progress</span><span>${run.progress?esc(run.progress.done+'/'+run.progress.total+' '+(run.progress.phase||'')):'–'}</span>${s?`<span>engine slot</span><span>${sl} · ${esc(slotText(s))}</span>`:''}</div>${s?'<b>Decode t/s</b>'+chart(s.decodeSeries,'tok/s','',null)+'<b>Prefill t/s</b>'+chart(s.prefillSeries,'tok/s','b',null):'<p class="mute">No live slot figures for this job (the daemon predates slot pinning, or the engine is not a llama-server).</p>'}`;
      {const mine=((pkg&&pkg.files)||[]).filter(f=>f.p.startsWith('items/'+i.id+'/')).sort((a,b)=>a.p<b.p?-1:1);h+=`<b>Artifacts</b> <span class="mute">${mine.length} files</span><div style="font-size:13px">${mine.map(f=>`<div>${fl(f.p,f.p.replace('items/'+i.id+'/',''))} <span class="mute">${fmt(f.s)} B</span></div>`).join('')||'<span class="mute">none yet</span>'}</div>`}
      h+=`<b>Jobs</b><table>${j.map(x=>`<tr><td>${esc(x.id)}</td><td>${esc(x.role)} #${x.attempt}</td><td>${esc(x.state)}</td><td>${Math.round(x.elapsedSec||0)} s</td></tr>`).join('')||'<tr><td class="mute">none</td></tr>'}</table>`;
      h+=`<b>Decisions</b><table>${decs.map(x=>`<tr><td><b>${esc(x.action)}</b> #${esc(x.inputs.attempt)}<br><span class="mute">${esc((x.reasons||[]).slice(-1)[0]||'')}</span></td></tr>`).join('')||'<tr><td class="mute">none</td></tr>'}</table>`;}}
  $('dbody').innerHTML=h;bindFileLinks($('dbody'));
}
// Polling is defensive: one request in flight per endpoint (a stalled server cannot pile up requests), a 5 s timeout, and nothing while the tab is hidden.
const busy={};
async function poll(name,url,apply){
  if(busy[name]||document.hidden)return;busy[name]=true;const ac=new AbortController(),to=setTimeout(()=>ac.abort(),5000);
  try{apply(await (await fetch(url,{cache:'no-store',signal:ac.signal})).json())}catch(e){if(name=='state')$('asof').textContent='portal unreachable: '+e}
  finally{clearTimeout(to);busy[name]=false}}
const tickState=()=>poll('state','/api/state',j=>{state=j;render();loadPkg()});
const tickLive=()=>poll('live','/api/live?n=120',j=>{live=j;renderLive();if(state)render()});
document.addEventListener('selectionchange',()=>{if(!frozen()){renderLive();render()}});
document.addEventListener('visibilitychange',()=>{if(!document.hidden){tickState();tickLive()}});
tickState();tickLive();setInterval(tickState,2000);setInterval(tickLive,1000);
</script></body></html>)HTML";
}

}  // namespace sub0squad
