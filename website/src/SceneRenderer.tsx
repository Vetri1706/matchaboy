import {useEffect,useRef} from 'react';
import {useFrame,useThree} from '@react-three/fiber';

export const reviewMode=import.meta.env.DEV&&new URLSearchParams(location.search).has('review');
const percentile=(values:number[],q:number)=>[...values].sort((a,b)=>a-b)[Math.floor((values.length-1)*q)];

// This is the sole owner of the main render call. Timing is CPU submission and
// browser frame cadence; it is deliberately not reported as GPU execution time.
export function SceneRenderer({onReady}:{onReady:()=>void}){
  const {gl,scene,camera,invalidate}=useThree(),ready=useRef(false);
  const run=useRef<null|{previous:number;cadence:number[];submit:number[]}>(null);
  useEffect(()=>{const refresh=()=>invalidate();window.addEventListener('presentation-change',refresh);return()=>window.removeEventListener('presentation-change',refresh);},[invalidate]);
  useEffect(()=>{if(!reviewMode)return;const start=()=>{delete gl.domElement.dataset.profile;run.current={previous:performance.now(),cadence:[],submit:[]};invalidate();};window.addEventListener('measure-scene',start);return()=>window.removeEventListener('measure-scene',start);},[gl,invalidate]);
  useFrame(()=>{
    const start=performance.now();gl.render(scene,camera);const finish=performance.now();
    if(!ready.current){ready.current=true;onReady();}
    if(run.current){invalidate();const r=run.current;r.cadence.push(start-r.previous);r.previous=start;r.submit.push(finish-start);
      if(r.cadence.length===120){
        gl.domElement.dataset.profile=JSON.stringify({samples:119,frameMsP50:percentile(r.cadence.slice(1),.5),frameMsP95:percentile(r.cadence.slice(1),.95),cpuSubmitMsP50:percentile(r.submit.slice(1),.5),cpuSubmitMsP95:percentile(r.submit.slice(1),.95),drawCalls:gl.info.render.calls,triangles:gl.info.render.triangles,geometries:gl.info.memory.geometries,textures:gl.info.memory.textures,drawingBuffer:[gl.domElement.width,gl.domElement.height],dpr:gl.getPixelRatio(),renderer:gl.getContext().getParameter(gl.getContext().RENDERER)});
        run.current=null;
      }
    }
  },1);
  return null;
}
