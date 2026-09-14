import {useState} from 'react';

const frame=()=>new Promise<void>(resolve=>requestAnimationFrame(()=>resolve()));
async function until(check:()=>boolean){const end=performance.now()+12000;while(!check()){if(performance.now()>end)throw new Error('Scene did not settle within 12 seconds');await frame();}}
const rect=(selector:string)=>document.querySelector(selector)?.getBoundingClientRect().toJSON();
const canvas=()=>document.querySelector<HTMLCanvasElement>('.scene-interaction canvas');
const store=async(name:string,meta:unknown,image?:string)=>{
  const response=await fetch('/__review/store',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,meta,image})});
  if(!response.ok)throw new Error(await response.text());
};

// Development-only controls. Seeking uses the real scroll input and waits for
// the presentation to settle; it never overrides a mesh or camera transform.
export default function ReviewTools({seek,setReduced,fail}:{seek:(p:number)=>void;setReduced:(r:boolean)=>void;fail:()=>void}){
  const [status,setStatus]=useState('Ready'),[busy,setBusy]=useState(false),[position,setPosition]=useState('0');
  const profileName=()=>innerWidth<761?'mobile':'desktop';
  const capture=async(reverse:boolean)=>{setBusy(true);try{
    const positions=Array.from({length:9},(_,i)=>(reverse?8-i:i)/8);
    for(const p of positions){
      setPosition(String(p));setStatus(`Capturing ${reverse?'reverse':'forward'} ${p*100}%`);seek(p);
      let settledAt=0;
      await until(()=>{
        const el=canvas(),area=document.querySelector('.scene-area')?.getBoundingClientRect();
        const ready=el&&area&&Math.abs(Number(el.dataset.progress)-p)<.0006&&Math.abs(el.getBoundingClientRect().width-area.width)<.1;
        if(!ready){settledAt=0;return false;}if(!settledAt)settledAt=performance.now();
        return performance.now()-settledAt>350;
      });await frame();await frame();
      const element=canvas();if(!element)throw new Error('3D canvas unavailable');
      const active=document.querySelector<HTMLElement>('.story-copy.is-active');
      const meta={progress:p,actualProgress:Number(element.dataset.progress),direction:reverse?'reverse':'forward',viewport:[innerWidth,innerHeight],documentWidth:document.documentElement.scrollWidth,title:active?.querySelector('h1,h2')?.textContent,copy:active?.textContent,copyRect:rect('.story-copy.is-active'),sceneRect:rect('.scene-area'),navigationRect:rect('.journey-nav'),canvasSize:[element.width,element.height],pose:JSON.parse(element.dataset.pose||'null'),background:active?getComputedStyle(active).color:'',timestamp:new Date().toISOString()};
      const name=`${profileName()}-${reverse?'reverse':'forward'}-${String(Math.round(p*1000)).padStart(3,'0')}`;
      await store(name,meta,element.toDataURL('image/webp',.92));
    }
    setStatus('Nine frames saved');
  }catch(error){setStatus(String(error));}finally{setBusy(false);}};
  const measure=async()=>{setBusy(true);setStatus('Measuring 120 rendered frames');try{
    window.dispatchEvent(new Event('measure-scene'));await until(()=>!!canvas()?.dataset.profile);
    const metrics=JSON.parse(canvas()!.dataset.profile!);await store(`${profileName()}-profile`,metrics);setStatus(JSON.stringify(metrics));
  }catch(error){setStatus(String(error));}finally{setBusy(false);}};
  return <aside className="review-tools" aria-label="Sequence verification">
    <label>Review position <select aria-label="Review position" value={position} disabled={busy} onChange={e=>{setPosition(e.target.value);seek(Number(e.target.value));}}>{Array.from({length:9},(_,i)=><option key={i} value={i/8}>{i*12.5}%</option>)}</select></label>
    <button disabled={busy} onClick={()=>capture(false)}>Capture forward frames</button><button disabled={busy} onClick={()=>capture(true)}>Capture reverse frames</button><button disabled={busy} onClick={measure}>Measure rendering</button>
    <button onClick={()=>setReduced(true)}>Review reduced motion</button><button onClick={()=>setReduced(false)}>Restore motion</button><button onClick={fail}>Simulate 3D failure</button>
    <output aria-live="polite">{status}</output>
  </aside>;
}
