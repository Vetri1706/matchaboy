import {useState,useEffect} from 'react';
import before from '../public/captures/inspector-0.png.json';
import after from '../public/captures/inspector-step-0.png.json';
const tabs=[
 {name:'CPU',index:1,title:'One instruction at a time.',description:'See the program counter, registers and instructions the CPU has executed. In the app, pause and step to follow what a game does next.',detail:'Registers & instruction history'},
 {name:'Memory',index:2,title:'Where a world lives.',description:'The address bus shows which memory the game reads, writes and executes. Activity connects game logic to the bytes behind it.',detail:'Reads · writes · execution'},
 {name:'Picture',index:0,title:'A frame, pixel by pixel.',description:'Follow the scanline, tile fetches and pixel queues as the Game Boy picture is assembled. The desktop view combines game output with the pixel pipeline.',detail:'Scanline · tiles · pixel queues'},
 {name:'Audio',index:3,title:'Four voices. One soundtrack.',description:'Two pulse channels, a wave channel and noise make the original Game Boy sound. These captured waveforms show each channel separately.',detail:'Pulse 1 · pulse 2 · wave · noise'},
];
export function Inspector({request=0}:{request?:number}){
 const [tab,setTab]=useState(0),[step,setStep]=useState(false),current=tabs[tab],data=step?after:before;
 useEffect(()=>{if(request){setTab(0);setStep(false);document.getElementById('recorded-step')?.focus({preventScroll:true});}},[request]);
 const source='/captures/inspector-'+(step?'step-':'')+current.index+'.png';
 return <section className="inspector-section" id="inspector">
  <div className="section-heading"><p className="eyebrow">INSIDE THE DESKTOP APP</p><h2>Curiosity has<br/>a pause button.</h2><p>Take a game apart without losing your place.<br/>Four focused views. As much detail as you want.</p></div>
  <div className="inspector-window">
   <div className="inspector-bar"><span><i/> MATCHABOY INSPECTOR</span><span>RECORDED APP CAPTURES</span></div>
   <div className="inspector-tabs" role="tablist" aria-label="Inspector views">{tabs.map((t,i)=><button key={t.name} id={'inspector-tab-'+i} role="tab" aria-selected={tab===i} aria-controls="inspector-panel" tabIndex={tab===i?0:-1} onClick={()=>setTab(i)} onKeyDown={e=>{if(e.key==='ArrowRight'||e.key==='ArrowLeft'){e.preventDefault();const n=(tab+(e.key==='ArrowRight'?1:3))%4;setTab(n);document.getElementById('inspector-tab-'+n)?.focus();}}}><span>0{i+1}</span>{t.name}<i>↗</i></button>)}</div>
   <div className="inspector-body" id="inspector-panel" role="tabpanel" aria-labelledby={'inspector-tab-'+tab}>
    <figure className="capture-view"><img key={source} src={source} width="1280" height="920" loading="lazy" alt={current.name+' Inspector in Matchaboy running Moon Courier, recorded frame '+data.frames}/><figcaption>Moon Courier · Native Windows app · Frame {data.frames}<a href={source} target="_blank" rel="noreferrer">View full size ↗</a></figcaption></figure>
    <aside className="inspector-explanation"><p className="eyebrow">{current.detail}</p><h3>{current.title}</h3><p>{current.description}</p><dl><div><dt>Recorded frame</dt><dd>{data.frames}</dd></div><div><dt>Clock cycles</dt><dd>{data.cycles.toLocaleString()}</dd></div><div><dt>Retired instructions</dt><dd>{data.instructions.toLocaleString()}</dd></div></dl><button id="recorded-step" className="pill tactile" onClick={()=>setStep(!step)}>{step?'Previous recorded frame':'Next recorded frame'} <span>{step?'←':'→'}</span></button><p className="recorded-delta" aria-live="polite">{step?`${(after.cycles-before.cycles).toLocaleString()} clock cycles and ${(after.instructions-before.instructions).toLocaleString()} instructions separate these frames.`:"Frame 154 is paused. Advance one recorded frame to see what changed."}</p><small>Captured from two consecutive emulated frames. This preview shows recorded output; run Matchaboy to inspect a game live.</small><a className="text-button" href={'/captures/inspector-'+(step?'step-':'')+'0.png.json'} target="_blank" rel="noreferrer">Capture data ↗</a></aside>
   </div>
  </div>
 </section>;
}
