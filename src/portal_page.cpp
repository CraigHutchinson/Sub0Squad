#include "sub0squad/portal.hpp"

namespace sub0squad {

const char* portalPage()
{
    return R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sub0Squad portal</title>
<style>
:root{--bg:#f7f7f5;--panel:#fff;--ink:#1c1c1a;--mute:#6b6b66;--line:#d9d9d3;--pending:#9a9a94;--running:#2f6fd6;--accepted:#2a8a4a;--failed:#c23a30;--edge:#a9a9a2;--accent:#7a4fd0}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--panel:#20201e;--ink:#ecece8;--mute:#9a9a94;--line:#3a3a36;--pending:#77776f;--running:#5b9bff;--accepted:#4cc274;--failed:#ff6b5e;--edge:#5c5c56;--accent:#a98bf0}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 system-ui,sans-serif}
header{padding:12px 16px;border-bottom:1px solid var(--line);display:flex;gap:16px;align-items:baseline;flex-wrap:wrap}
h1{font-size:16px;margin:0}.mute{color:var(--mute)}main{padding:12px 16px;display:grid;gap:12px;grid-template-columns:1fr;max-width:1400px;margin:0 auto}
.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px}
.kpis{display:flex;gap:10px;flex-wrap:wrap}.kpi{min-width:120px}.kpi b{display:block;font-size:20px}
svg{width:100%;height:auto;display:block;overflow:visible}
.node rect{fill:var(--panel);stroke-width:2}.node text{fill:var(--ink);font-size:12px}.node .sub{fill:var(--mute);font-size:10.5px}
.s-pending rect{stroke:var(--pending)}.s-running rect{stroke:var(--running);animation:pulse 1.4s ease-in-out infinite}.s-accepted rect{stroke:var(--accepted)}.s-failed rect{stroke:var(--failed)}
@keyframes pulse{50%{stroke-opacity:.35}}.edge{fill:none;stroke:var(--edge);stroke-width:1.5}.edge.parent{stroke-dasharray:4 4;stroke:var(--accent)}
.lane{fill:none;stroke:var(--line);stroke-dasharray:2 5}.lanelabel{fill:var(--mute);font-size:11px}
table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:4px 8px;border-bottom:1px solid var(--line);font-size:13px;vertical-align:top}
.pill{display:inline-block;padding:0 8px;border-radius:10px;font-size:12px;border:1px solid var(--line)}
.legend span{margin-right:14px}.dot{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:4px;border:2px solid}
@media(min-width:900px){main{grid-template-columns:3fr 2fr}.wide{grid-column:1/-1}}
</style></head><body>
<header><h1>Sub0Squad portal</h1><span id="asof" class="mute">connecting…</span><span class="mute">read-only · loopback · refreshes every 2 s</span></header>
<main>
<section class="card wide"><div class="kpis" id="kpis"></div></section>
<section class="card wide"><div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:8px"><b id="wfname">Work graph</b>
<span class="legend mute"><span><i class="dot" style="border-color:var(--pending)"></i>pending</span><span><i class="dot" style="border-color:var(--running)"></i>running</span><span><i class="dot" style="border-color:var(--accepted)"></i>accepted</span><span><i class="dot" style="border-color:var(--failed)"></i>failed</span><span>solid: depends on · dashed: divided into</span></span></div>
<div id="graph"></div>
<div class="mute" id="note" style="margin-top:6px"></div></section>
<section class="card"><b>Nodes</b><table id="nodes"></table></section>
<section class="card"><b>Decisions</b><table id="decisions"></table></section>
<section class="card wide"><b>Daemon jobs</b><table id="jobs"></table></section>
</main>
<script>
const $=id=>document.getElementById(id);
const fmt=n=>n>=1000?(n/1000).toFixed(1)+'k':Math.round(n);
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let sel=null;
function layout(items){
  const byId=Object.fromEntries(items.map(i=>[i.id,i]));
  const col={};const colOf=id=>{if(id in col)return col[id];col[id]=0;const it=byId[id];let c=0;for(const d of it.deps||[])if(byId[d])c=Math.max(c,colOf(d)+1);
    if(it.parent&&byId[it.parent])c=Math.max(c,colOf(it.parent)+1);return col[id]=c};
  items.forEach(i=>colOf(i.id));
  const row=i=>(i.id.match(/\./g)||[]).length;
  const cells={};const pos={};
  items.forEach(i=>{const k=col[i.id]+','+row(i);cells[k]=(cells[k]||0);pos[i.id]={c:col[i.id],r:row(i),s:cells[k]++}});
  return pos;
}
function drawGraph(items){
  items=items.map(i=>({persona:'',deps:[],parent:'',tokensIn:0,tokensOut:0,durationSec:0,engine:'',...i}));
  if(!items.length){$('graph').innerHTML='<p class="mute">No work items yet.</p>';return}
  const pos=layout(items),W=190,H=62,GX=44,GY=26,PX=70,PY=34;
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
  items.forEach(i=>{const p=pos[i.id],x=X(p),yy=Y(p);
    s+=`<g class="node s-${i.status}" data-id="${esc(i.id)}" style="cursor:pointer"><rect x="${x}" y="${yy}" width="${W}" height="${H}" rx="8"/>
    <text x="${x+10}" y="${yy+18}"><tspan font-weight="600">${esc(i.id)}</tspan> <tspan class="sub">${esc(i.persona)}</tspan></text>
    <text class="sub" x="${x+10}" y="${yy+34}">${esc(i.status)} · attempt${i.attempts==1?'':'s'} ${i.attempts}${i.engine?' · '+esc(i.engine):''}</text>
    <text class="sub" x="${x+10}" y="${yy+50}">tokens in ${fmt(i.tokensIn)} · out ${fmt(i.tokensOut)}</text></g>`});
  $('graph').innerHTML=s+'</svg>';
  document.querySelectorAll('.node').forEach(n=>n.onclick=()=>{sel=n.dataset.id;render(last)});
}
let last=null;
function render(d){
  last=d;const wfs=(d.workflows&&d.workflows.workflows)||[];
  $('asof').textContent='as of '+new Date((d.workflows.asOf||Date.now()/1000)*1000).toLocaleTimeString()+' · '+(d.workflows.source||'');
  const wf=wfs.length?wfs[wfs.length-1]:null;
  if(!wf){$('kpis').innerHTML='<span class="mute">No workflows yet. Start one with <code>sub0squad do "…"</code>.</span>';drawGraph([]);$('nodes').innerHTML=$('decisions').innerHTML='';}
  else{
    const L=wf.ledger||{},eng=Object.entries(L.byEngine||{}).map(([n,e])=>`${esc(n)} ${e.decodeTps.toFixed(1)} tok/s`).join(' · ')||'—';
    const done=wf.items.filter(i=>i.status=='accepted').length;
    $('kpis').innerHTML=[['workflow',esc(wf.id)+' <span class="pill">'+esc(wf.state)+'</span>'],['items accepted',done+' / '+wf.items.length],['jobs active',wf.jobsActive+' of '+wf.jobs],
      ['tokens',fmt(L.totalTokens||0)],['work ratio',((L.workRatio||0)*100).toFixed(1)+' %'],['rework tokens',fmt(L.reworkTokens||0)],['engine decode',eng]]
      .map(([k,v])=>`<div class="kpi"><span class="mute">${k}</span><b>${v}</b></div>`).join('');
    $('wfname').textContent='Work graph · '+wf.id+' · '+wf.task;
    $('note').textContent=wf.note||'';
    drawGraph(wf.items);
    $('nodes').innerHTML='<tr><th>node</th><th>status</th><th>att.</th><th>in</th><th>out</th><th>time</th></tr>'+wf.items.map(i=>({tokensIn:0,tokensOut:0,durationSec:0,...i})).map(i=>`<tr style="${i.id==sel?'background:var(--bg)':''}"><td>${esc(i.id)}</td><td>${esc(i.status)}</td><td>${i.attempts}</td><td>${fmt(i.tokensIn)}</td><td>${fmt(i.tokensOut)}</td><td>${Math.round(i.durationSec)} s</td></tr>`).join('');
    const ds=(wf.decisions||[]).slice(-12).reverse();
    $('decisions').innerHTML=ds.length?ds.map(x=>`<tr><td><b>${esc(x.action)}</b> ${esc((x.inputs&&x.inputs.item)||'')} #${esc((x.inputs&&x.inputs.attempt)||'')}<br><span class="mute">${esc((x.reasons||[]).slice(-1)[0]||'')}</span></td></tr>`).join(''):'<tr><td class="mute">none yet</td></tr>';
  }
  const jobs=((d.jobs&&d.jobs.jobs)||[]).slice(-14).reverse();
  $('jobs').innerHTML='<tr><th>job</th><th>state</th><th>tool / role</th><th>progress</th><th>elapsed</th></tr>'+jobs.map(j=>`<tr><td>${esc(j.id)}</td><td>${esc(j.state)}</td><td>${esc(j.tool||'')} ${esc(j.role||'')}</td><td>${j.progress?esc(j.progress.done+'/'+j.progress.total+' '+(j.progress.label||'')):''}</td><td>${j.elapsedSec!=null?Math.round(j.elapsedSec)+' s':''}</td></tr>`).join('');
}
async function tick(){try{const r=await fetch('/api/state',{cache:'no-store'});render(await r.json())}catch(e){$('asof').textContent='portal unreachable: '+e}}
tick();setInterval(tick,2000);
</script></body></html>)HTML";
}

}  // namespace sub0squad
