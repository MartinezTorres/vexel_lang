#!/usr/bin/env python3
"""Bounded compile/validate/measure search with a checked baseline.

The embedding supplies finite candidate descriptions and an evaluator. This
module chooses only among successful measured artifacts; it never edits source,
relaxes validation, or treats a code-generation estimate as a measurement.
"""
import hashlib
import itertools
import json
import math
from pathlib import Path
import time
from tempfile import NamedTemporaryFile


def fingerprint(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def tree_fingerprint(directory):
    """Include editable project contents and membership, not just its manifest.

    Generated trees contain ordinary files/directories. A later symlink or
    special file invalidates that artifact rather than hiding an untracked
    dependency outside the recorded tree.
    """
    root = Path(directory)
    if root.is_symlink() or not root.is_dir():
        raise ValueError("measured artifact is not an ordinary directory: " + str(root))
    entries = []
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise ValueError("untracked link in measured artifact: " + str(path))
        if path.is_file():
            entries.append([name, "file", fingerprint(path)])
        elif path.is_dir():
            entries.append([name, "directory"])
        else:
            raise ValueError("special file in measured artifact: " + str(path))
    return hashlib.sha256(json.dumps(entries, separators=(",", ":")).encode()).hexdigest()


def preference_variants(baseline, domains, *, max_candidates=16, priority=()):
    """Bounded, deterministic neighborhoods of explicit integer preferences.

    Try single changes before combinations. Joint candidates allow interacting
    frame/overlay/bank choices to win even when neither local change pays alone.
    An optional ordered list gives promising scopes earlier attempts within
    each distance. It changes search order only, never validation or costs.
    This enumerates physical options supplied by an embedding, never source
    transformations, and does not claim a globally optimal search.
    """
    if type(max_candidates) is not int or max_candidates < 1:
        raise ValueError("candidate budget must be positive")
    if any(not isinstance(key, str) or not key for key in (*baseline, *domains)):
        raise ValueError("preference identities must be nonempty strings")
    if any(type(v) is not int or v < 0 for v in baseline.values()):
        raise ValueError("preferences must be nonnegative integers")
    priority = tuple(priority)
    if any(not isinstance(key, str) or key not in domains for key in priority) or len(set(priority)) != len(priority):
        raise ValueError("priority must contain distinct known preference identities")
    ranks = {key: rank for rank, key in enumerate(priority)}
    alternatives = {}
    for key in sorted(domains, key=lambda key: (ranks.get(key, len(ranks)), key)):
        values = tuple(domains[key])
        if key not in baseline or not values or any(type(v) is not int or v < 0 for v in values):
            raise ValueError("invalid preference domain: " + key)
        if len(set(values)) != len(values) or baseline[key] not in values:
            raise ValueError("domain must contain its baseline once: " + key)
        other = tuple(value for value in values if value != baseline[key])
        if other:
            alternatives[key] = other
    result = [{"name": "baseline", "preferences": dict(baseline)}]
    for distance in range(1, len(alternatives)+1):
        for keys in itertools.combinations(alternatives, distance):
            for values in itertools.product(*(alternatives[key] for key in keys)):
                if len(result) == max_candidates:
                    return result
                preferences = dict(baseline)
                preferences.update(zip(keys, values))
                result.append({"name": "variant-"+str(len(result)), "preferences": preferences})
    return result


class MeasurementCache:
    """A measurement is reusable only for the exact revision and preferences.

    The embedding supplies the complete input identity (compiler, source,
    toolchain, environment and validation workload). Artifact hashes are checked
    on every hit. Corrupt or stale records are misses, never positive evidence.
    """
    @staticmethod
    def encode(value):
        return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)

    def __init__(self, directory, identity):
        encoded = self.encode(identity)
        self.identity = json.loads(encoded)
        self.directory = Path(directory)/hashlib.sha256(encoded.encode()).hexdigest()

    def path(self, preferences):
        return self.directory/(hashlib.sha256(self.encode(preferences).encode()).hexdigest()+".json")

    def load(self, preferences):
        try:
            record = json.loads(self.path(preferences).read_text())
            if (not isinstance(record, dict) or record.get("format") != "vexel-measurement-1" or record.get("identity") != self.identity or
                    self.encode(record.get("preferences")) != self.encode(preferences)):
                return None
            return checked(record["measurement"])
        except (OSError, ValueError, TypeError, KeyError, OverflowError):
            return None

    def save(self, preferences, measurement):
        checked(measurement)
        path = self.path(preferences)
        path.parent.mkdir(parents=True, exist_ok=True)
        record = {"format": "vexel-measurement-1", "identity": self.identity,
                  "preferences": preferences, "measurement": measurement}
        with NamedTemporaryFile(mode="w", dir=path.parent, prefix=path.stem+"-", suffix=".tmp", delete=False) as output:
            output.write(self.encode(record)+"\n")
            temporary = Path(output.name)
        temporary.replace(path)


def checked(record):
    if not isinstance(record, dict) or record.get("validated") is not True:
        raise ValueError("candidate has not passed behavioral validation")
    metrics = record.get("metrics")
    if not isinstance(metrics, dict) or not metrics or any(not isinstance(v, (int, float)) or isinstance(v, bool) or
                          not math.isfinite(v) or v < 0 for v in metrics.values()):
        raise ValueError("invalid measured cost")
    if not record.get("artifacts"):
        raise ValueError("missing measured artifact provenance")
    for artifact in record["artifacts"]:
        if not isinstance(artifact, dict):
            raise ValueError("invalid measured artifact description")
        kind = artifact.get("kind", "file")
        if kind not in ("file", "tree"):
            raise ValueError("unknown measured artifact kind")
        actual = tree_fingerprint(artifact["path"]) if kind == "tree" else fingerprint(artifact["path"])
        if actual != artifact["sha256"]:
            raise ValueError("measured artifact changed: " + artifact["path"])
    return record


def frontier(entries, objective):
    """Keep accepted, nondominated measurements in original candidate order.

    Equal-cost alternatives keep the earlier candidate, just like selection.
    This is evidence for a later policy decision, not a new validation rule.
    """
    result = []
    for entry in entries:
        if entry.get("status") != "accepted":
            continue
        score = tuple(entry["measurement"]["metrics"][key] for key in objective)
        if any(all(a <= b for a, b in zip(previous, score)) for _, previous in result):
            continue
        result = [(name, previous) for name, previous in result
                  if not all(a <= b for a, b in zip(score, previous))]
        result.append((entry["candidate"]["name"], score))
    return [name for name, _ in result]


def search(candidates, evaluate, report, *, objective, limits, seconds=1200, max_candidates=4):
    """Evaluate baseline first, then bounded alternatives; lower costs win.

    limits maps metric names to (relative-to-baseline, absolute-cap) pairs.
    Objective is lexicographic, with the baseline winning equal-cost ties.
    The report also retains the nondominated accepted alternatives, comparing
    the objective metrics without imposing their lexicographic priority.
    The evaluator receives the remaining wall budget and must bound its tools.
    A failed baseline aborts; failed alternatives retain the checked baseline.
    """
    if not candidates or len(candidates) > max_candidates or seconds <= 0:
        raise ValueError("empty search or search budget exceeded")
    if not objective or len(set(objective)) != len(objective):
        raise ValueError("objective must contain distinct measured costs")
    identities = [c["name"] for c in candidates]
    if len(set(identities)) != len(identities):
        raise ValueError("duplicate candidate identity")
    started = time.monotonic()
    output = {"policy": {"objective": objective, "limits": limits, "max_candidates": max_candidates,
                         "wall_seconds": seconds}, "candidates": [], "selected": None,
              "artifact_validation": "pending"}
    baseline = winner = None

    def save():
        output["frontier"] = frontier(output["candidates"], objective)
        output["elapsed_seconds"] = time.monotonic() - started
        path = Path(report)
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(json.dumps(output, indent=2) + "\n")
        temporary.replace(path)

    for candidate in candidates:
        entry = {"candidate": candidate}
        output["candidates"].append(entry)
        remaining = seconds - (time.monotonic() - started)
        if remaining <= 0:
            entry.update(status="skipped", reason="wall budget exhausted")
            save()
            continue
        try:
            measured = checked(evaluate(candidate, remaining))
            metrics = measured["metrics"]
            if any(key not in metrics for key in (*objective, *limits)):
                raise ValueError("missing objective or limit measurement")
            entry["measurement"] = measured
            if baseline is None:
                baseline = measured
            rejected = []
            for key, (relative, absolute) in limits.items():
                if relative is not None and metrics[key] > baseline["metrics"][key] * relative:
                    rejected.append(key + " exceeds baseline allowance")
                if absolute is not None and metrics[key] > absolute:
                    rejected.append(key + " exceeds target budget")
            if rejected:
                entry.update(status="rejected", reason="; ".join(rejected))
                if winner is None:
                    raise ValueError("baseline violates target budgets")
            else:
                entry["status"] = "accepted"
                score = tuple(metrics[key] for key in objective)
                if winner is None or score < winner:
                    winner = score
                    output["selected"] = candidate["name"]
        except Exception as error:
            entry.update(status="failed", reason=str(error))
            save()
            if candidate is candidates[0]:
                raise RuntimeError("baseline could not be validated") from error
        save()
    # Do not return a winner whose binaries were overwritten during a later
    # build. An embedding must give each candidate its own artifact directory.
    selected = next(e for e in output["candidates"] if e["candidate"]["name"] == output["selected"])
    try:
        checked(selected["measurement"])
        # A later chooser may select any frontier member. Verify those artifacts
        # too, rather than publishing a tradeoff whose build has been overwritten.
        for entry in output["candidates"]:
            if entry is not selected and entry["candidate"]["name"] in output["frontier"]:
                checked(entry["measurement"])
    except Exception as error:
        output.update(artifact_validation="failed", artifact_error=str(error))
        save()
        raise
    output["artifact_validation"] = "passed"
    save()
    return output
