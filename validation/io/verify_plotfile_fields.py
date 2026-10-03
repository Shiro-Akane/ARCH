#!/usr/bin/env python3
"""Read-only multi-field t=0 validation; no scientific tolerance or value repair."""
import argparse
import hashlib
import json
from pathlib import Path
import h5py
import numpy as np

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def text(value):
    return value.decode() if isinstance(value, bytes) else str(value)

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
    reader = {row["case"]: row for row in json.loads(Path(args.reader_summary).read_text())} if args.reader_summary else {}
    refs = {}
    for directory in json.loads(Path(args.references).read_text()):
        directory = Path(directory)
        refs[json.loads((directory / "summary.json").read_text())["case"]] = directory
    results = []
    for record in json.loads(Path(args.runs).read_text()):
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
            assert text(identities["build_id"]) == "unknown"
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
