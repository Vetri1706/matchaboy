import * as THREE from 'three';

export function roundRect(w:number,h:number,r:number,x=0,y=0,bottomRight=r){
 const s=new THREE.Shape(),l=x-w/2,b=y-h/2,t=y+h/2,rr=x+w/2;
 s.moveTo(l+r,b);s.lineTo(rr-bottomRight,b);s.quadraticCurveTo(rr,b,rr,b+bottomRight);
 s.lineTo(rr,t-r);s.quadraticCurveTo(rr,t,rr-r,t);s.lineTo(l+r,t);s.quadraticCurveTo(l,t,l,t-r);
 s.lineTo(l,b+r);s.quadraticCurveTo(l,b,l+r,b);s.closePath();return s;
}
export function plate(shape:THREE.Shape,depth=.25,bevel=.035){
 const geo=new THREE.ExtrudeGeometry(shape,{depth,bevelEnabled:bevel>0,bevelSegments:3,steps:1,bevelSize:bevel,bevelThickness:bevel,curveSegments:16});geo.translate(0,0,-depth/2);geo.computeVertexNormals();return geo;
}
export function shellGeometry(){
 const shape=roundRect(3.68,5.7,.25,0,0,.72);
 shape.holes.push(roundRect(3.13,2.84,.16,0,.86));
 // Button apertures expose actual recess walls instead of painted circles.
 for(const [x,y] of [[.63,-1.53],[1.21,-1.18]]){const hole=new THREE.Path();hole.absarc(x,y,.321,0,Math.PI*2,false);shape.holes.push(hole);}
 // Speaker slots are actual holes through the moulded front, with a dark backing.
 for(let i=0;i<5;i++){
  const slot=roundRect(.075,.64,.036);const points=slot.getPoints(12).map(p=>new THREE.Vector2(p.x*Math.cos(-.3)-p.y*Math.sin(-.3)+.72+i*.19,p.x*Math.sin(-.3)+p.y*Math.cos(-.3)-2.22+i*.015));
  shape.holes.push(new THREE.Path(points));
 }
 return plate(shape,.30,.035);
}
export function bezelGeometry(){
 const s=roundRect(3.13,2.84,.16,0,.86,.36);s.holes.push(roundRect(2.2,1.99,.025,0,.78));return plate(s,.14,.022);
}
export function dpadGeometry(){
 const s=new THREE.Shape(),a=.19,b=.57;
 s.moveTo(-a,b);s.lineTo(a,b);s.lineTo(a,a);s.lineTo(b,a);s.lineTo(b,-a);s.lineTo(a,-a);s.lineTo(a,-b);s.lineTo(-a,-b);s.lineTo(-a,-a);s.lineTo(-b,-a);s.lineTo(-b,a);s.lineTo(-a,a);s.closePath();return plate(s,.14,.018);
}
