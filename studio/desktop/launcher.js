const api=window.archDesktop;
if(api.platform==='linux')document.getElementById('distro-field').hidden=true;
document.getElementById('pick').onclick=async()=>{
 try{const p=await api.pickProject();if(p)document.getElementById('project').value=p;}
 catch(e){document.getElementById('status').textContent=e.message;}
};
document.getElementById('launch').onclick=async()=>{
 const status=document.getElementById('status'),button=document.getElementById('launch');
 status.textContent='Preparing managed Local Host…';button.disabled=true;
 try{
  const options={};
  for(const key of ['project','config','binary','case','source','distro']){
   if(key==='distro'&&api.platform==='linux')continue;
   const value=document.getElementById(key).value;
   if(value)options[key]=value;
  }
  const result=await api.launch(options);
  if(result.error)status.textContent=result.error;
 }catch(e){status.textContent=e.message;}
 finally{button.disabled=false;}
};
if(api.error)document.getElementById('status').textContent=api.error;
