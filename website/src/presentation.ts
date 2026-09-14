// One owner arbitrates scroll and replay before either the DOM or 3D reads time.
export function createPresentation() {
  let value=0, serial=0, replay:null|{elapsed:number;from:number;scroll:number}=null;
  const ease=(x:number)=>{x=Math.max(0,Math.min(1,x));return x*x*x*(x*(x*6-15)+10);};
  return {advance(target:number,dt:number,reduced:boolean,request:number){
    dt=Math.min(Math.max(dt,0),.1);
    if(request!==serial){serial=request;if(!reduced)replay={elapsed:0,from:value,scroll:target};}
    if(replay&&(reduced||Math.abs(target-replay.scroll)>.025))replay=null;
    if(reduced)value=target;
    else if(replay){
      replay.elapsed+=dt;
      value=replay.elapsed<.95
        ?replay.from+(.32-replay.from)*ease(replay.elapsed/.85)
        :.32+(Math.max(.75,replay.scroll)-.32)*ease((replay.elapsed-.95)/3.75);
      if(replay.elapsed>=4.7)replay=null;
    }else value+= (target-value)*(1-Math.exp(-7*dt));
    return value;
  }};
}
