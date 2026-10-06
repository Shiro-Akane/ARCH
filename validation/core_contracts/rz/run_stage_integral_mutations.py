"""Compile actual private gravity-owner mutations; scientific gates stay unchanged."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OLD = "3eb727df077b0839c2048e919a28958fba5b124d"
TEST = "validation/core_contracts/rz/test_candidate_stage_integrals.cpp"

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run_one(source, output, name, header, test):
    folder=output/name
    include=folder/"include/physics/gravity"
    include.mkdir(parents=True)
    (include/"GravitySource.h").write_text(header)
    executable=folder/"test"
    command=["g++","-std=c++20","-O2","-I"+str(folder/"include"),
             "-I"+str(source/"src"),"-I"+str(source/"include"),
             str(test),"-o",str(executable)]
    built=subprocess.run(command,capture_output=True,text=True,timeout=120)
    (folder/"build.log").write_text(built.stdout+built.stderr)
    if built.returncode:
        raise RuntimeError(name+": compile failure is NOT a rejected mutation")
    checked=subprocess.run([str(executable)],capture_output=True,text=True,timeout=30)
    (folder/"test.log").write_text(checked.stdout+checked.stderr)
    return {"name":name,"exit_code":checked.returncode,
            "compiled":True,"header_sha256":sha(include/"GravitySource.h"),
            "test_sha256":sha(test),"elf_sha256":sha(executable)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    source=args.source.resolve()
    output=args.output.resolve()
    owned=(ROOT/"studio/.local/integration").resolve()
    if not output.is_relative_to(owned) or output.exists():
        raise RuntimeError("new owned local output required")
    if not source.is_relative_to(owned):
        raise RuntimeError("private source required; never mutate public headers")
    output.mkdir(parents=True)
    original=(source/"src/physics/gravity/GravitySource.h").read_text()
    mutations={}
    for component in ("mom_u","mom_v","mom_w","eng"):
        for bad,expression in (("nan","quiet_NaN()"),("inf","infinity()")):
            target={"mom_u":"source.radial_momentum_rate / source.volume",
                    "mom_v":"source.axial_momentum_rate / source.volume",
                    "mom_w":"source.angular_momentum_rate / source.angular_measure",
                    "eng":"source.energy_rate / source.volume"}[component]
            old="delta."+component+" += dt * "+target+";"
            new="delta."+component+" = std::numeric_limits<double>::"+expression+";"
            assert original.count(old)==1
            mutations[component+"_"+bad]=original.replace(old,new)
    for component,rate in (("radial","radial_momentum_rate"),
                           ("axial","axial_momentum_rate"),
                           ("energy","energy_rate")):
        old="source."+rate+" / source.volume"
        assert original.count(old)==1
        mutations[component+"_wrong_W_denominator"]=original.replace(
            old,"source."+rate+" / source.angular_measure")
    mutations["angular_wrong_V_denominator"]=original.replace(
        "source.angular_momentum_rate / source.angular_measure",
        "source.angular_momentum_rate / source.volume")
    mutations["radial_axial_swapped"]=original.replace(
        "source.radial_momentum_rate","source.SWAP").replace(
        "source.axial_momentum_rate","source.radial_momentum_rate").replace(
        "source.SWAP","source.axial_momentum_rate")
    test=ROOT/TEST
    records=[run_one(source,output,"correct_candidate",original,test)]
    assert records[0]["exit_code"]==0,"correct candidate failed"
    for name,header in mutations.items():
        result=run_one(source,output,name,header,test)
        assert result["exit_code"]!=0,name+": corrected test falsely accepted mutation"
        records.append(result)
    historical=output/"old-test.cpp"
    historical.write_bytes(subprocess.check_output(["git","show",OLD+":"+TEST],cwd=ROOT))
    reproduced=[]
    for name in ("eng_nan","radial_wrong_W_denominator"):
        result=run_one(source,output,"old_"+name,mutations[name],historical)
        assert result["exit_code"]==0,"historical false PASS was not reproduced"
        reproduced.append(result)
    summary={"status":"PASS_TEST_SENSITIVITY_ONLY","base_fixture":OLD,
             "candidate_header_sha256":hashlib.sha256(original.encode()).hexdigest(),
             "positive":records[0],"rejected_mutations":records[1:],
             "historical_false_pass_reproduced":reproduced,
             "scope":"Actual scalar-owner compilation, not stage producer/evolution/RZ science acceptance"}
    (output/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps({"status":summary["status"],"rejected_mutations":len(mutations),
                      "historical_false_pass":len(reproduced),"summary":str(output/"summary.json")}))
if __name__=="__main__":
    main()
