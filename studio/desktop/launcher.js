const api=window.archDesktop;
document.getElementById('pick').onclick=async()=>{try{const p=await api.pickProject();if(p)document.getElementById('project').value=p;}catch(e){document.getElementById('status').textContent=e.message;}};
document.getElementById('launch').onclick=async()=>{const status=document.getElementById('status');status.textContent='Preparing managed Local Host…';try{const result=await api.launch({project:document.getElementById('project').value,distro:document.getElementById('distro').value,config:document.getElementById('config').value});if(result.error)status.textContent=result.error;}catch(e){status.textContent=e.message;}};
if(api.error)document.getElementById('status').textContent=api.error;
