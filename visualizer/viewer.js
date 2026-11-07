'use strict';
const {MODES, DEFAULT_MODE}=wsRequire('modes/index');
const {PALETTES}=wsRequire('palettes');
const {analyzeFrame,newAnalysisState}=wsRequire('analyze');
const canvas=document.getElementById('scope'), ctx=canvas.getContext('2d');
// The native GTK host owns the fullscreen window. Keep the rendering context
// and active preset in this same WebKit page when switching modes.
document.querySelector('main').addEventListener('dblclick', e=>{
 if(e.target.tagName==='CANVAS') report('toggle-fullscreen');
});
document.addEventListener('keydown', e=>{
 if(e.ctrlKey&&!e.altKey&&!e.metaKey&&(e.key==='ArrowLeft'||e.key==='ArrowRight')){
  e.preventDefault();report(e.key==='ArrowLeft'?'previous-song':'next-song');
 }
 else if(e.key==='F11') {e.preventDefault();report('toggle-fullscreen');}
 else if(e.key==='Escape') report('exit-fullscreen');
});
const modeSelect=document.getElementById('mode'),paletteSelect=document.getElementById('palette');
const status=document.getElementById('status');
for(const m of MODES)modeSelect.add(new Option(m.name,m.id));
modeSelect.value=DEFAULT_MODE.id;
const aqua={id:'aqua',name:'Aqua',bg:'#0b0d0c',color:(pos,alpha=1)=>`hsla(180,65%,${30+pos*50}%,${alpha})`};
const palettes=[aqua,...PALETTES];for(const p of palettes)paletteSelect.add(new Option(p.name,p.id));
let mode=DEFAULT_MODE,palette=aqua,state={},pointer=null,packets=0,frames=0,receivedAt=0;
let visible=true;
let tone=false,lastPaint=0,analysis=newAnalysisState();
const N=2048,ring=new Float32Array(N),re=new Float64Array(N),im=new Float64Array(N);
const fft=new Uint8Array(N/2),wave=new Uint8Array(N),smoothed=new Float64Array(N/2);let write=0;
function add(v){ring[write]=v;write=(write+1)%N;}
window.modulezBridge={setVisible(value){visible=!!value;if(!visible){ring.fill(0);window.modulezProjectM?.clearAudio();}},feed(encoded){
 try{
  const raw=atob(encoded);const s16=o=>{let v=raw.charCodeAt(o)|(raw.charCodeAt(o+1)<<8);return (v>=32768?v-65536:v)/32768;};
  for(let i=0;i+3<raw.length;i+=4)add((s16(i)+s16(i+2))/2);
  packets++;frames+=Math.floor(raw.length/4);receivedAt=performance.now();
  window.modulezProjectM?.feed(raw);
 }catch(e){status.textContent='PCM error: '+e.message;report('PCM error: '+e.message);}
}};
function report(s){if(window.webkit?.messageHandlers?.modulez)window.webkit.messageHandlers.modulez.postMessage(s);}
function spectrum(){
 for(let i=0;i<N;i++){
  const sample=ring[(write+i)%N];wave[i]=Math.max(0,Math.min(255,Math.round(128+sample*127)));
  re[i]=sample*(.5-.5*Math.cos(2*Math.PI*i/(N-1)));im[i]=0;
 }
 // Iterative radix-2 FFT; the source is PCM, no capture/audio API is involved.
 for(let i=1,j=0;i<N;i++){let bit=N>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){const v=re[i];re[i]=re[j];re[j]=v;}}
 for(let len=2;len<=N;len<<=1){const a=-2*Math.PI/len,cr=Math.cos(a),ci=Math.sin(a);
  for(let i=0;i<N;i+=len){let wr=1,wi=0;for(let j=0;j<len/2;j++){
   const u=i+j,v=u+len/2,tr=re[v]*wr-im[v]*wi,ti=re[v]*wi+im[v]*wr;
   re[v]=re[u]-tr;im[v]=im[u]-ti;re[u]+=tr;im[u]+=ti;
   const next=wr*cr-wi*ci;wi=wr*ci+wi*cr;wr=next;
  }}
 }
 for(let i=0;i<N/2;i++){
  const magnitude=Math.hypot(re[i],im[i])/(N/2);
  smoothed[i]=.8*smoothed[i]+.2*magnitude;
  const db=20*Math.log10(Math.max(1e-8,smoothed[i]));
  fft[i]=Math.max(0,Math.min(255,Math.round((db+100)*255/70)));
 }
}
function reset(){state={};ctx.fillStyle=palette.bg;ctx.fillRect(0,0,canvas.width,canvas.height);}
modeSelect.onchange=()=>{mode=MODES.find(m=>m.id===modeSelect.value);reset();};
paletteSelect.onchange=()=>{palette=palettes.find(p=>p.id===paletteSelect.value);reset();};
document.getElementById('tone').onclick=()=>{tone=!tone;document.getElementById('tone').textContent='Test signal: '+(tone?'on':'off');};
canvas.onpointermove=e=>{const r=canvas.getBoundingClientRect();pointer={x:(e.clientX-r.left)*canvas.width/r.width,y:(e.clientY-r.top)*canvas.height/r.height};};canvas.onpointerleave=()=>pointer=null;
function draw(now){
 requestAnimationFrame(draw);if(!visible||document.hidden||now-lastPaint<33)return;
 const dt=Math.min(.1,Math.max(.001,(now-lastPaint)/1000));lastPaint=now;
 const rect=canvas.getBoundingClientRect(),scale=Math.min(1,960/Math.max(1,rect.width));
 const w=Math.max(1,Math.round(rect.width*scale)),h=Math.max(1,Math.round(rect.height*scale));
 if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;reset();}
 if(tone){for(let i=0;i<N;i++)ring[i]=.4*Math.sin(2*Math.PI*220*(now/1000+i/44100))*(.55+.45*Math.sin(now/180));}
 else if(now-receivedAt>180){ring.fill(0);}
 if(window.modulezProjectM?.active){window.modulezProjectM.render(now);
  status.textContent=(now-receivedAt<250&&packets?'LIVE PLAYER PCM':'Waiting for player PCM')+` • ${packets} batches • ${frames} frames`;return;}
 spectrum();const f=analyzeFrame(fft,wave,44100,now/1000,dt,analysis);
 ctx.save();ctx.globalAlpha=mode.fade??1;ctx.fillStyle=palette.bg;ctx.fillRect(0,0,w,h);ctx.restore();
 ctx.save();try{mode.draw({ctx,w,h,f,p:palette,state,pointer});}catch(e){status.textContent='Renderer error: '+e.message;report('Renderer error: '+e.message);}finally{ctx.restore();}
 status.textContent=(tone?'TEST SIGNAL':now-receivedAt<250&&packets?'LIVE PLAYER PCM':'Waiting for player PCM')+` • ${packets} batches • ${frames} frames • level ${f.level.toFixed(3)}`;
}
requestAnimationFrame(draw);report('Local Wavescope ready • no capture APIs used');
