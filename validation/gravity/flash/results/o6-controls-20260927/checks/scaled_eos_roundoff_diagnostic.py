from pathlib import Path
import json,shlex,subprocess
r=Path('/home/shiroakane/.codex/worktrees/compute-optim/ARCH');src=r/'tests/cuda/microphysics/eos/test_eos_host_device_parity.cu'
s=src.read_text();old='throw std::runtime_error(std::string(field) + " value/reference mismatch");';assert s.count(old)==1
s=s.replace(old,'if (field.rfind("scaled",0)!=0) '+old)
needle='const double abs_error = std::abs(actual - authority);'
s=s.replace(needle,'if (field.rfind("scaled",0)==0) { std::cerr << std::setprecision(17) << "SCALED_PROBE field=" << field << " actual=" << actual << " reference=" << authority << " ulp=" << std::abs(actual-authority)/(std::nextafter(authority,INFINITY)-authority) << "\\n"; }\n    '+needle,1)
s=s.replace('compare(run_device(scaled_owner.view(),Xi,point[0],point[1],stream),','std::cerr << "DIAGNOSTIC_ONLY fraction=" << fraction << " rho=" << point[0] << " T=" << point[1] << "\\n";\n            compare(run_device(scaled_owner.view(),Xi,point[0],point[1],stream),',1)
diag=Path('/tmp/o6_scaled_eos_diagnostic.cu');diag.write_text(s)
e=next(x for x in json.loads((r/'build-cuda/compile_commands.json').read_text()) if x['file']==str(src))
cmd=shlex.split(e['command']);original_object=cmd[cmd.index('-o')+1];cmd[cmd.index('-o')+1]='/tmp/o6_scaled_eos_diagnostic.o';cmd=[str(diag) if x==str(src) else x for x in cmd]
if '-MF' in cmd:cmd[cmd.index('-MF')+1]='/tmp/o6_scaled_eos_diagnostic.o.d'
subprocess.run(cmd,cwd=e['directory'],check=True)
link=Path('/tmp/o6_scaled_eos_link.txt').read_text();assert link.startswith(': && ') and link.endswith(' && :');args=shlex.split(link[5:-5]);args=[('/tmp/o6_scaled_eos_diagnostic.o' if x==original_object else x) for x in args];args[args.index('-o')+1]='/tmp/o6_scaled_eos_diagnostic';subprocess.run(args,cwd=r/'build-cuda',check=True)
print('DIAGNOSTIC built; source acceptance comparator remains unchanged.',flush=True)
