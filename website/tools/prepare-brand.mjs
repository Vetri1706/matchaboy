import {Resvg} from '@resvg/resvg-js';
import {mkdirSync, writeFileSync, copyFileSync, readFileSync} from 'node:fs';
import {resolve} from 'node:path';

// Scalable pixel construction transcribed from the maintainer's supplied sprout logo.
const rows = [
'........................',
'.gggggggg...............',
'.gggggggggg......gggggg.',
'.ggggggggggg....ggggggg.',
'.ggggggggggg...gggggggg.',
'..gggggggggg..ggggggggg.',
'...ggggggggg.ggggggggg..',
'....gggggggg.gggggggg...',
'.....ggggggg.ggggggg....',
'.......ggggg.ggggg......',
'..........gg.gg.........',
'..........gg.gg.........',
'..........gg.gg.........',
'......dddddddddddd......',
'......dddddddddddd......',
'....ddddwwwwwwwwdddd....',
'....ddddwwwwwwwwdddd....',
'....ddwwwwwwwwwwwwdd....',
'....ddwwwddwwddwwwdd....',
'....ddwwwddwwddwwwdd....',
'....ddwwwddwwddwwwdd....',
'....ddwwwwwwwwwwwwdd....',
'....ddddwwwwwwwwdddd....',
'......dddddddddddd......',
'......dddddddddddd......',
'........................',
];
const palette={g:'#738b58',d:'#30332d',w:'#f6f4ed'};
let cells='';
rows.forEach((r,y)=>[...r].forEach((c,x)=>{if(palette[c])cells+=`<rect x="${x}" y="${y}" width="1" height="1" fill="${palette[c]}"/>`;}));
const mark=`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 26 28" shape-rendering="crispEdges"><g transform="translate(1 1)">${cells}</g></svg>`;
const icon=`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 40" shape-rendering="crispEdges"><rect width="40" height="40" rx="9" fill="#718958"/><g transform="translate(8 7)">${cells.replaceAll('#738b58','#f4f2df').replaceAll('#30332d','#f4f2df').replaceAll('#f6f4ed','#718958')}</g></svg>`;
const web=resolve('public/brand'),native=resolve('../assets');
mkdirSync(web,{recursive:true});
writeFileSync(resolve(web,'mark.svg'),mark);writeFileSync(resolve(web,'app-icon.svg'),icon);
writeFileSync(resolve(native,'matchaboy.svg'),icon);
writeFileSync(resolve(native,'matchaboy-mark.svg'),mark);
const png=(size)=>new Resvg(icon,{fitTo:{mode:'width',value:size}}).render().asPng();
writeFileSync(resolve(web,'app-icon.png'),png(256));
const sizes=[16,32,48,64,128,256],images=sizes.map(png),header=Buffer.alloc(6+16*sizes.length);
header.writeUInt16LE(1,2);header.writeUInt16LE(sizes.length,4);
let offset=header.length;
sizes.forEach((size,i)=>{const start=6+i*16;header[start]=size===256?0:size;header[start+1]=size===256?0:size;header.writeUInt16LE(1,start+4);header.writeUInt16LE(32,start+6);header.writeUInt32LE(images[i].length,start+8);header.writeUInt32LE(offset,start+12);offset+=images[i].length;});
writeFileSync(resolve(native,'matchaboy.ico'),Buffer.concat([header,...images]));
copyFileSync(resolve(native,'matchaboy.ico'),resolve(web,'favicon.ico'));
const icnsParts=[[128,'ic07'],[256,'ic08'],[512,'ic09']].map(([size,key])=>{const data=png(size),h=Buffer.alloc(8);h.write(key);h.writeUInt32BE(data.length+8,4);return Buffer.concat([h,data]);});
const icnsHeader=Buffer.alloc(8);icnsHeader.write('icns');icnsHeader.writeUInt32BE(8+icnsParts.reduce((n,p)=>n+p.length,0),4);
writeFileSync(resolve(native,'matchaboy.icns'),Buffer.concat([icnsHeader,...icnsParts]));
writeFileSync(resolve('../include/matchaboy_logo.hpp'),'// Sprout mark supplied by the Matchaboy maintainer. GPL-3.0-only.\n#pragma once\nnamespace matcha {\ninline constexpr const char *logo_rows[] = {\n'+rows.map(r=>'    "'+r+'",').join('\n')+'\n};\n}\n');
for(const filename of ['autopsy_windows.cpp','autopsy_main.mm']) {
 const file=resolve('../src',filename);let source=readFileSync(file,'utf8');
 if(!source.includes('#include "matchaboy_logo.hpp"'))source='#include "matchaboy_logo.hpp"\n'+source;
 source=source.replace(/void draw_logo\(([\s\S]*?)\n}\n/,(_all,args)=>{
 const signature=args.slice(0,args.indexOf('{'));
 return 'void draw_logo('+signature+'{\n    const double pixel = size / 28.0;\n    for (unsigned row = 0; row < 26; ++row) {\n        for (unsigned col = 0; col < 24; ++col) {\n            const char cell = matcha::logo_rows[row][col];\n            if (cell == \'.\') continue;\n            const Color color = cell == \'g\' ? Color{0.45,0.55,0.35} :\n                cell == \'d\' ? Color{0.94,0.95,0.88} : Color{0.13,0.22,0.19};\n            fill(context, x + (col+2)*pixel, y + (row+1)*pixel, pixel, pixel, color);\n        }\n    }\n}\n';
 });writeFileSync(file,source);
}
console.log('Sprout SVG, PNG, ICO, ICNS and native header generated.');
