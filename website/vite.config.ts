import {defineConfig} from "vite";
import react from "@vitejs/plugin-react";
import {mkdir,writeFile,readFile} from 'node:fs/promises';
import path from 'node:path';

// Local, development-only evidence sink. Only fixed capture names are accepted;
// neither arbitrary paths nor external destinations are supplied by the page.
const reviewEvidence={name:'local-review-evidence',configureServer(server:any){
  const folder=path.resolve('verification/sequence-20260914');
  server.middlewares.use('/__review',async(req:any,res:any,next:any)=>{
    if(req.method==='POST'&&req.url==='/store'){
      try{let body='';for await(const chunk of req){body+=chunk;if(body.length>12_000_000)throw new Error('Capture too large');}
        const {name,meta,image}=JSON.parse(body);
        if(!/^(desktop|mobile)-(profile|(forward|reverse)-[0-9]{3,4})$/.test(name))throw new Error('Invalid capture name');
        await mkdir(folder,{recursive:true});await writeFile(path.join(folder,name+'.json'),JSON.stringify(meta,null,2));
        if(image){if(!image.startsWith('data:image/webp;base64,'))throw new Error('Expected WebP capture');const bytes=Buffer.from(image.split(',')[1],'base64');await writeFile(path.join(folder,name+'.webp'),bytes);
          if(name==='desktop-forward-000'||name==='desktop-forward-1000'){await mkdir(path.resolve('public/scene'),{recursive:true});await writeFile(path.resolve('public/scene',name.endsWith('-000')?'closed.webp':'opened.webp'),bytes);}
        }
        res.end('Saved');
      }catch(error){res.statusCode=400;res.end(String(error));}return;
    }
    if(req.method==='GET'&&/^\/(report\.html|contact-sheet\.svg|(desktop|mobile)-[a-z-]+[0-9]*\.(webp|json))$/.test(req.url)){
      try{const ext=path.extname(req.url);res.setHeader('Content-Type',ext==='.html'?'text/html':ext==='.svg'?'image/svg+xml':ext==='.webp'?'image/webp':'application/json');res.end(await readFile(path.join(folder,req.url.slice(1))));}catch{res.statusCode=404;res.end('Not found');}return;
    }
    next();
  });
}};

export default defineConfig({
  plugins: [react(),reviewEvidence],
  build: {target: "es2022", sourcemap: false},
});
