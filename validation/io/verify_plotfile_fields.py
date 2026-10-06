#!/usr/bin/env python3
"""Read-only multi-field t=0 validation; no scientific tolerance or value repair."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from functools import lru_cache
import h5py
import numpy as np

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def text(value):
    return value.decode() if isinstance(value, bytes) else str(value)

def load_plot_json(serialized):
    """Restore a JSON integer token -0 as FP64 signed zero; retain other JSON types."""
    return json.loads(serialized, parse_int=lambda token: -0.0 if token == "-0" else int(token))

@lru_cache(maxsize=1)
def identity_node():
    """Resolve an explicit campaign/PATH Node 24+; never replace a bad selection."""
    explicit = os.environ.get("ARCH_STUDIO_NODE")
    if explicit and not Path(explicit).is_absolute():
        raise RuntimeError("ARCH_STUDIO_NODE must be an absolute supported Node executable")
    selected = explicit or shutil.which("node")
    if not selected:
        raise RuntimeError("Put Node 24+ on PATH or set ARCH_STUDIO_NODE")
    result = subprocess.run([selected, "--version"], capture_output=True, timeout=5)
    version = re.fullmatch(rb"v([0-9]+)\.[0-9]+\.[0-9]+\s*", result.stdout)
    if result.returncode or len(result.stdout) > 64 or not version or int(version[1]) < 24:
        raise RuntimeError("Selected Node must be version 24+; no older runtime fallback")
    return selected

def production_identity_metadata(path, observed=None):
    """Reuse the isolated reader/client once per file, without requesting field payloads."""
    root = Path(__file__).resolve().parents[2]
    # Paths are argv, metadata is stdin JSON, and the code/module owners are
    # fixed. No input record is interpreted as code or passed through a shell.
    code = r'''
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [root,path]=process.argv.slice(1);
try {
 const reader=await import(pathToFileURL(join(root,'studio/host/isolatedPlotfileMetadata.ts')).href);
 const client=await import(pathToFileURL(join(root,'studio/src/host/plotfileAudit.ts')).href);
 const {PROTOCOL_VERSION}=await import(pathToFileURL(join(root,'studio/src/host/contracts.ts')).href);
 const {stringifyPlotfile}=await import(pathToFileURL(join(root,'studio/host/plotfileJson.ts')).href);
 const envelope={protocolVersion:PROTOCOL_VERSION,projectId:'h5py-identity',relativePath:path};
 const expected=JSON.parse(readFileSync(0,'utf8'));
 if(expected!==null)client.validatePlotfileAudit({...envelope,metadata:expected},envelope.projectId,path);
 const metadata=await reader.inspectPlotfileMetadataIsolated(path);
 const {audit}=client.validatePlotfileAudit({...envelope,metadata},envelope.projectId,path);
 const serialized=stringifyPlotfile(audit);
 if(Buffer.byteLength(serialized)>64*1024)throw Error('Identity metadata exceeds 64 KiB');
 process.stdout.write(serialized);
}catch(error){process.stderr.write(String(error?.message??error).slice(0,1024));process.exitCode=1;}
'''
    request = json.dumps(observed, allow_nan=False).encode("utf-8")
    if len(request) > 128*1024:
        raise ValueError("Observed metadata exceeds the existing transport budget")
    result = subprocess.run([identity_node(), "--max-old-space-size=256", "--input-type=module",
                             "-e", code, str(root), str(Path(path).resolve())],
                            input=request, capture_output=True, timeout=30)
    if result.returncode or len(result.stdout) > 64*1024 or len(result.stderr) > 2048:
        raise RuntimeError("Production identity validation failed: " + result.stderr.decode("utf-8", "replace")[:1024])
    value = load_plot_json(result.stdout.decode("utf-8"))
    assert isinstance(value, dict) and value["schemaVersion"] == "audit-1"
    return value

def identity_record_sha(dataset):
    """Hash a single actual UTF-8 string value, never an object array's pointer bytes."""
    assert isinstance(dataset, h5py.Dataset) and h5py.check_string_dtype(dataset.dtype) is not None
    assert dataset.size == 1
    value = dataset[()]
    if isinstance(value, np.ndarray):
        value = value.item()
    assert isinstance(value, (str, bytes, np.bytes_))
    raw = value.encode("utf-8") if isinstance(value, str) else bytes(value)
    assert raw.decode("utf-8").encode("utf-8") == raw
    assert 0 < len(raw) <= 1024*1024 and b"\0" not in raw
    return hashlib.sha256(raw).hexdigest()

def verify_plot_identity(file, record=None, observed=None, expected_sha=None):
    """Share production identity authority; keep independent HDF record/attribute checks."""
    metadata = production_identity_metadata(file.filename, observed)
    source = metadata["candidateSourceIdentity"]
    attrs = file["SourceIdentity"].attrs
    version = text(attrs["version"])
    assert version == source["version"] and metadata["geometry"] == "cartesian"
    assert metadata["file"]["sha256"] == (expected_sha if expected_sha is not None else sha(Path(file.filename)))
    formal = version == "arch-plot-identity-1"
    if formal:
        assert source["scope"] == "resolved-runtime" and source["recordsVerified"] is True
        assert metadata["completion"]["state"] == "complete"
        assert text(file.attrs["plot_identity_state"]) == "recorded"
        assert text(file.attrs["plot_publication_version"]) == "arch-plot-publication-1"
        assert metadata["candidateNativeGrid"]["version"] == "arch-native-cartesian-1"
        assert metadata["nativeCellGeometry"] == {"bounds": "recorded", "volume": "recorded"}
        for name, attr, key in (("effective_config_record", "effective_config_sha256", "effectiveConfigSha256"),
                                ("source_manifest_record", "source_manifest_sha256", "sourceManifestSha256"),
                                ("build_profile_record", "build_profile_sha256", "buildProfileSha256"),
                                ("eos_identity_record", "eos_identity_sha256", "eosIdentitySha256")):
            assert identity_record_sha(file["SourceIdentity/" + name]) == text(attrs[attr]) == source[key]
    else:
        # This tool already supported historical candidates. A different or
        # absent version cannot acquire that narrow partial-evidence allowance.
        assert version == "candidate-identity-1" and source["scope"] == "partial"
        assert metadata["candidateNativeGrid"]["version"] == "candidate-cartesian-1"
        assert text(file.attrs["plot_identity_state"]) == "unknown"
        assert text(file.attrs["plot_publication_version"]) == "candidate-1"
        assert metadata["completion"]["state"] == "unknown" and metadata["renderEligible"] is False
        for key, attr in (("effectiveConfigSha256", "effective_config_sha256"),
                          ("buildId", "build_id"), ("sourceGitHead", "source_git_head")):
            assert source[key] is None and text(attrs[attr]) == "unknown"
    for key, attr in (("caseId", "case_id"), ("rawConfigSha256", "raw_config_sha256"),
                      ("binarySha256", "binary_sha256")):
        assert source[key] is not None and source[key] == text(attrs[attr])
    if observed is not None:
        for key in ("candidateSourceIdentity", "candidateNativeGrid", "scientificIdentity", "completion", "renderEligible"):
            assert observed[key] == metadata[key]
    if record is not None:
        assert source["caseId"] == record["case"]
        assert source["rawConfigSha256"] == record["inputSha256"]
        assert source["binarySha256"] == record["binarySha256"]
        for key in ("runId", "caseSourceSha256", "effectiveConfigSha256", "buildId", "sourceManifestSha256",
                    "buildProfileSha256", "eosType", "eosIdentitySha256", "eosTableSha256", "idealGamma",
                    "speciesNames", "sourceGitHead", "sourceGitDirty", "resolvedBackend", "eosUnitSystem"):
            if key in record:
                assert source[key] == record[key]
    return {"version": version, "completion": metadata["completion"]["state"],
            "scientificIdentity": metadata["scientificIdentity"], "sourceGitHead": source["sourceGitHead"],
            "sourceGitSource": source.get("sourceGitSource"),
            "scope": "stored identity consistency; numerical qualification separate"}

def keys(file, group):
    return list(zip(file[group + "/level"][:].tolist(),
                    file[group + "/logical_x1"][:].tolist(),
                    file[group + "/logical_x2"][:].tolist(),
                    file[group + "/logical_x3"][:].tolist()))

def bit_mismatches(left, right):
    assert left.shape == right.shape and left.dtype == right.dtype == np.dtype("float64")
    return int(np.count_nonzero(left.view(np.uint64) != right.view(np.uint64)))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", required=True)
    parser.add_argument("--references", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--reader-summary", help="Optional processed all-fields Host/client query result")
    args = parser.parse_args()
    reader = {row["case"]: row for row in load_plot_json(Path(args.reader_summary).read_text())} if args.reader_summary else {}
    refs = {}
    for directory in json.loads(Path(args.references).read_text()):
        directory = Path(directory)
        refs[json.loads((directory / "summary.json").read_text())["case"]] = directory
    results = []
    records = json.loads(Path(args.runs).read_text())
    assert isinstance(records, list) and records, "Require a nonempty approved field-run list"
    for record in records:
        directory = Path(record["localEvidenceDirectory"])
        plots, checkpoints = list((directory / "output").glob("*plt*.h5")), list((directory / "output").glob("*chk*.h5"))
        assert record["exitCode"] == 0 and len(plots) == len(checkpoints) == 1
        plot, checkpoint = plots[0], checkpoints[0]
        before = [sha(plot), sha(checkpoint)]
        reference = refs[record["case"]]
        prior_checkpoint = next((reference / "output").glob("*chk*.h5"))
        with h5py.File(plot, "r") as p, h5py.File(checkpoint, "r") as c, h5py.File(prior_checkpoint, "r") as old:
            assert p.attrs["time"] == c.attrs["time"] == old.attrs["time"] == 0
            assert c.attrs["step"] == old.attrs["step"] == 0
            shape = p["Data/DENS"].shape
            blocks, per = shape[0], int(np.prod(shape[1:]))
            pk = list(zip(p["Grid/level"][:].tolist(), *(p["NativeGrid/logical_x" + str(a)][:].tolist() for a in (1, 2, 3))))
            ck, ok = keys(c, "Blocks"), keys(old, "Blocks")
            assert len(set(pk)) == blocks and set(pk) == set(ck) == set(ok)
            index = {key: i for i, key in enumerate(ck)}
            old_index = {key: i for i, key in enumerate(ok)}
            order = [index[key] for key in pk]
            prior_order = [old_index[key] for key in ck]
            unchanged = {}
            for name in ("rho", "mom_u", "mom_v", "mom_w", "eng", "enuc_rate"):
                current = c["Data/" + name][:]
                previous = old["Data/" + name][:][prior_order]
                unchanged[name] = bit_mismatches(current, previous)
            for name in ("X", "rhoX"):
                unchanged[name] = bit_mismatches(c["Data/" + name][:], old["Data/" + name][:][:, prior_order, :])
            assert all(value == 0 for value in unchanged.values())
            identities = p["SourceIdentity"].attrs
            assert text(identities["case_id"]) == record["case"]
            assert text(identities["raw_config_sha256"]) == record["inputSha256"]
            assert text(identities["binary_sha256"]) == record["binarySha256"]
            identity = verify_plot_identity(p, record, expected_sha=before[0])
            fields = {}
            raw_matches = {}
            for field, dataset in p["Data"].items():
                assert dataset.shape == shape and dataset.dtype == np.dtype("float64")
                values = dataset[:].reshape(blocks, per)
                assert np.all(np.isfinite(values))
                attrs = {key: text(value) for key, value in dataset.attrs.items()}
                assert attrs["metadata_version"] == "candidate-field-1" and attrs["centering"] == "cell"
                if attrs["unit"] == "unknown":
                    assert attrs.get("unit_reason", "").strip()
                fields[field] = {"shape": list(shape), "dtype": str(dataset.dtype),
                                 "declaration": attrs, "samples": int(values.size),
                                 "min": float(values.min()), "max": float(values.max())}
                source = {"DENS": "rho", "ENER": "eng", "ENUC": "enuc_rate"}.get(field)
                if source:
                    raw_matches[field] = bit_mismatches(values, c["Data/" + source][:][order])
                if field in ("VELX", "VELY", "VELZ"):
                    momentum = {"VELX": "mom_u", "VELY": "mom_v", "VELZ": "mom_w"}[field]
                    # Matches the inspected authoritative recover() quotient only;
                    # no EOS formula, physical floor or tolerance is introduced.
                    quotient = c["Data/" + momentum][:][order] / c["Data/rho"][:][order]
                    raw_matches[field] = bit_mismatches(values, quotient)
            species = [text(v) for v in c["Species/name"][:]]
            for number, name in enumerate(species):
                assert name in fields
                raw_matches[name] = bit_mismatches(p["Data/" + name][:].reshape(blocks, per), c["Data/X"][number][order])
                assert fields[name]["declaration"]["unit"] == "1"
                assert fields[name]["declaration"]["meaning"] == "species_mass_fraction"
            assert all(value == 0 for value in raw_matches.values())
            reader_mismatches = None
            if reader:
                observed = reader[record["case"]]
                assert observed["fileSha256"] == before[0]
                samples = observed["exportedFields"]
                assert {item["field"] for item in samples} == set(fields)
                reader_mismatches = {}
                for item in samples:
                    index = item["pointIndex"]
                    assert isinstance(index, int) and 0 <= index < blocks * per
                    raw = np.array([p["Data/" + item["field"]][:].reshape(-1)[index]], dtype=np.float64)
                    queried = np.array([item["rawPointValue"]], dtype=np.float64)
                    reader_mismatches[item["field"]] = bit_mismatches(raw, queried)
                assert all(value == 0 for value in reader_mismatches.values())
            assert fields["ENTR"]["declaration"]["meaning"] in ("pressure_density_gamma1_proxy", "pressure_density_proxy")
            assert fields["ENTR"]["declaration"]["unit"] == "unknown"
            results.append({"case": record["case"], "time": 0, "step": 0,
                            "plotfileSha256": before[0], "plotfileBytes": plot.stat().st_size,
                            "checkpointSha256": before[1], "inputSha256": record["inputSha256"],
                            "binarySha256": record["binarySha256"], "changedInputKeys": record["changedKeys"],
                            "identity": identity,
                            "fields": fields, "checkpointUnchangedBitMismatches": unchanged,
                            "fieldCheckpointBitMismatches": raw_matches,
                            "hostReaderRawPointBitMismatches": reader_mismatches,
                            "logicalKeysMatch": True,
                            "unverifiedScientificFields": ["PRES", "TEMP", "ENTR", "VORT", "DIVV"],
                            "scope": "t=0 stored fields and metadata; not EOS/diagnostic independent science acceptance"})
        assert [sha(plot), sha(checkpoint)] == before
    Path(args.output).write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps([{"case": row["case"], "fieldCount": len(row["fields"]),
                      "fieldNames": list(row["fields"]), "plotfileSha256": row["plotfileSha256"],
                      "plotfileBytes": row["plotfileBytes"],
                      "checkpointUnchangedBitMismatches": row["checkpointUnchangedBitMismatches"],
                      "fieldCheckpointBitMismatches": row["fieldCheckpointBitMismatches"],
                      "unverifiedScientificFields": row["unverifiedScientificFields"]} for row in results], indent=2))

if __name__ == "__main__":
    main()
