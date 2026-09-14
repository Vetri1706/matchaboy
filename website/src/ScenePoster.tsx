export function ScenePoster({opened=false,failed=false,loading=false}:{opened?:boolean;failed?:boolean;loading?:boolean}){
  return <div className="scene-poster" role="img" aria-label={opened?'Illustrated handheld with front, board and rear separated':'Matchaboy handheld illustration'}>
    <img src={opened?'/scene/opened.webp':'/scene/closed.webp'} alt=""/>
    {loading&&<span role="status">Preparing the console…</span>}
    {failed&&<span role="status">3D is unavailable. Explore the still view or continue to the Inspector.</span>}
  </div>;
}
