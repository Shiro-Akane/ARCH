/**
 * Parse the desktop's explicit launch selection and Windows/WSL file association.
 * Return paths and options for the existing launcher; project validation and Core
 * identity are checked by Host before scientific actions become available.
 */
import path from 'node:path';
export function parseLaunchArgs(args,cwd){
 const result={cwd};const seen=new Set();const allowed=new Set(['project','binary','case','config','source','distro','cwd','build-dir']);
 const delimiter=args.indexOf('--');const selection=delimiter<0?args:args.slice(delimiter+1);
 for(let i=0;i<selection.length;i++){const key=selection[i].replace(/^--/,'');if(!selection[i].startsWith('--')||!allowed.has(key)||seen.has(key)||!selection[i+1]||selection[i+1].startsWith('--')||(Object.hasOwn(result,key)&&key!=='cwd'))throw new Error('Use --project / --binary / --case / --config / --source / --build-dir / --distro with one value each.');seen.add(key);result[key]=selection[++i];}
 return result;
}
export function windowsAssociation(value,distro){
 if(typeof value!=='string'||!value||value.includes('\0'))throw new Error('Invalid path.');
 const unc=/^\\\\(?:wsl\.localhost|wsl\$)\\([^\\]+)\\(.*)$/i.exec(value);
 if(unc){if(distro&&distro!==unc[1])throw new Error('Path belongs to a different WSL distro.');return {distro:unc[1],linux:'/'+unc[2].replaceAll('\\','/')};}
 if(value.startsWith('/'))return {distro,linux:value};
 return {distro,windows:path.win32.resolve(value)};
}
