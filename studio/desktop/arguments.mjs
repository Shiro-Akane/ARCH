/**
 * Parse the desktop's explicit launch selection and Windows/WSL file association.
 * Return paths and options for the existing launcher; project validation and Core
 * identity are checked by Host before scientific actions become available.
 */
import path from 'node:path';
/** Dialog selections cross the existing Host protocol as canonical '/' paths. */
export function configurationRelativePath(project,selected,platform){
 const paths=platform==='win32'?path.win32:path.posix;
 const relative=paths.relative(project,paths.resolve(selected));
 if(!relative||relative==='..'||relative.startsWith('..'+paths.sep)||paths.isAbsolute(relative)||!relative.endsWith('.par'))
  throw new Error('Choose a .par inside the managed project.');
 return relative.split(paths.sep).join('/');
}
export function hostEnvironment(options,platform){
 if(platform==='linux'){
  if(options.distro)throw new Error('Linux launcher does not select another WSL distribution.');
  return 'linux';
 }
 if(platform!=='win32')throw new Error('Desktop Host supports Linux and Windows.');
 // An explicit project/source owns the host choice; an inherited WSL cwd must
 // not redirect an absolute Windows project back through WSL.
 const selection=options.project||options.source||options.cwd||'';
 return options.distro||selection.startsWith('/')||/^\\\\(?:wsl\.localhost|wsl\$)\\/i.test(selection)?'wsl':'win32';
}
export function parseLaunchArgs(args,cwd){
 const result={cwd};const seen=new Set();const allowed=new Set(['project','binary','case','config','source','distro','cwd','build-dir']);
 const delimiter=args.indexOf('--');const selection=delimiter<0?args:args.slice(delimiter+1);
 for(let i=0;i<selection.length;i++){const key=selection[i].replace(/^--/,'');if(!selection[i].startsWith('--')||!allowed.has(key)||seen.has(key)||!selection[i+1]||selection[i+1].startsWith('--')||(Object.hasOwn(result,key)&&key!=='cwd'))throw new Error('Use --project / --binary / --case / --config / --source / --build-dir / --distro with one value each.');seen.add(key);result[key]=selection[++i];}
 return result;
}
export function windowsAssociation(value,distro){
 if(typeof value!=='string'||!value||value.includes('\0'))throw new Error('Invalid path.');
 if(/^[A-Za-z]:(?![\\/])/.test(value))throw new Error('Drive-relative paths require an explicit absolute path.');
 if(/^\\\\[?.]\\/.test(value))throw new Error('Device paths are not supported for project launch.');
 const unc=/^\\\\(?:wsl\.localhost|wsl\$)\\([^\\]+)\\(.*)$/i.exec(value);
 if(unc){if(distro&&distro!==unc[1])throw new Error('Path belongs to a different WSL distro.');return {distro:unc[1],linux:'/'+unc[2].replaceAll('\\','/')};}
 if(value.startsWith('/'))return {distro,linux:value};
 return {distro,windows:path.win32.resolve(value)};
}
