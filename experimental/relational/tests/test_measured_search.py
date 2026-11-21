#!/usr/bin/env python3
"""Cost selection never bypasses validation or accepts unmeasured estimates."""
import importlib.util
import json
from pathlib import Path
import tempfile

path = Path(__file__).resolve().parents[3] / "backends/msx-neo8/tools/optimize.py"
spec = importlib.util.spec_from_file_location("optimize", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as tmp:
    artifact = Path(tmp) / "rom"
    artifact.write_bytes(b"checked artifact")
    def evaluate(candidate, remaining):
        assert remaining > 0
        return {"validated": candidate.get("valid", True),
                "metrics": {"stack": candidate["stack"], "time": candidate["time"]},
                "artifacts": [{"path": str(artifact), "sha256": module.fingerprint(artifact)}]}
    candidates = [{"name": "baseline", "stack": 20, "time": 100},
                  {"name": "wrong", "stack": 0, "time": 0, "valid": False},
                  {"name": "slow", "stack": 10, "time": 110},
                  {"name": "better", "stack": 18, "time": 90}]
    result = module.search(candidates, evaluate, Path(tmp) / "result.json", objective=["stack", "time"],
                           limits={"stack": [1, 100], "time": [1.02, None]})
    assert result["selected"] == "better"
    assert result["frontier"] == ["better"]
    assert [r["status"] for r in result["candidates"]] == ["accepted", "failed", "rejected", "accepted"]
    tradeoffs = [{"name": "baseline", "stack": 20, "time": 100},
                {"name": "fast", "stack": 22, "time": 80},
                {"name": "small-stack", "stack": 18, "time": 110},
                {"name": "duplicate", "stack": 22, "time": 80}]
    result = module.search(tradeoffs, evaluate, Path(tmp) / "tradeoffs.json", objective=["stack", "time"], limits={})
    assert result["selected"] == "small-stack"
    assert result["frontier"] == ["baseline", "fast", "small-stack"]
    assert result["artifact_validation"] == "passed"
    for objective in ([], ["stack", "stack"]):
        try:
            module.search(tradeoffs, evaluate, Path(tmp) / "invalid-policy.json", objective=objective, limits={})
            raise AssertionError("ambiguous objective accepted")
        except ValueError:
            pass
    candidates[0]["valid"] = False
    try:
        module.search(candidates, evaluate, Path(tmp) / "invalid.json", objective=["stack"], limits={})
        raise AssertionError("invalid baseline accepted")
    except RuntimeError:
        pass
    measured = evaluate({"stack": 20, "time": 100}, 1)
    artifact.write_bytes(b"different build")
    try:
        module.checked(measured)
        raise AssertionError("stale measurement accepted")
    except ValueError:
        pass
    # Even an unselected frontier member must still identify its measured
    # artifact. A subsequent policy is allowed to prefer speed over stack.
    def overwritten(candidate, remaining):
        image = Path(tmp) / candidate["name"]
        image.write_bytes(b"measured")
        result = evaluate(candidate, remaining)
        result["artifacts"] = [{"path": str(image), "sha256": module.fingerprint(image)}]
        if candidate["name"] == "later":
            (Path(tmp) / "fast").write_bytes(b"overwritten")
        return result
    try:
        module.search([tradeoffs[0], tradeoffs[1], {"name": "later", "stack": 30, "time": 200}],
                      overwritten, Path(tmp) / "overwritten.json", objective=["stack", "time"], limits={})
        raise AssertionError("stale frontier accepted")
    except ValueError:
        assert json.loads((Path(tmp) / "overwritten.json").read_text())["artifact_validation"] == "failed"
    # Local choices can interact through a shared maximum: neither single
    # change helps, but the joint realization does. Keep it within the budget.
    variants=module.preference_variants({'left':1,'right':1},{'right':(1,0),'left':(1,0)},max_candidates=4)
    assert [v['preferences'] for v in variants]==[
        {'left':1,'right':1},{'left':0,'right':1},{'left':1,'right':0},{'left':0,'right':0}]
    assert module.preference_variants({'left':1,'right':1},{'left':(1,0),'right':(1,0)},max_candidates=3)==variants[:3]
    prioritized=module.preference_variants({'left':1,'right':1},{'left':(1,0),'right':(1,0)},
                                          max_candidates=4,priority=['right'])
    assert prioritized[0]==variants[0]
    assert [v['preferences'] for v in prioritized[1:]]==[variants[i]['preferences'] for i in (2,1,3)]
    for priority in (['missing'],['right','right']):
        try:
            module.preference_variants({'left':1,'right':1},{'left':(1,0),'right':(1,0)},priority=priority)
            raise AssertionError('invalid priority accepted')
        except ValueError:
            pass
    def coupled(candidate, remaining):
        both=not any(candidate['preferences'].values())
        return evaluate({'stack':12 if both else 20,'time':90 if both else 100},remaining)
    selected=module.search(variants,coupled,Path(tmp)/'coupled.json',objective=['stack','time'],limits={})
    assert selected['selected']==variants[-1]['name']
    for domains in ({'missing':(1,0)},{'left':(0,)},{'left':(1,1)},{'left':(1,True)}):
        try:
            module.preference_variants({'left':1},domains)
            raise AssertionError('invalid preference domain accepted')
        except ValueError:pass

    cache=module.MeasurementCache(Path(tmp)/'cache',{'source':'original','compiler':'one','workload':'oracle'})
    preferences={'left':0,'right':1}
    measured=evaluate({'stack':12,'time':90},1)
    assert cache.load(preferences) is None
    cache.save(preferences,measured)
    assert cache.load(dict(reversed(list(preferences.items()))))==measured
    assert cache.load({'left':1,'right':1}) is None
    for identity in ({'source':'changed','compiler':'one','workload':'oracle'},
                     {'source':'original','compiler':'two','workload':'oracle'},
                     {'source':'original','compiler':'one','workload':'other'}):
        assert module.MeasurementCache(Path(tmp)/'cache',identity).load(preferences) is None
    artifact.write_bytes(b'edited measured binary')
    assert cache.load(preferences) is None
    measured=evaluate({'stack':12,'time':90},1)
    cache.save(preferences,measured)
    for corrupt in ('{', '[]', '{}',
                    json.dumps({'format':'vexel-measurement-1','identity':cache.identity,
                                'preferences':preferences,'measurement':None})):
        cache.path(preferences).write_text(corrupt)
        assert cache.load(preferences) is None
    generated=Path(tmp)/'generated';generated.mkdir()
    header=generated/'binding.h';header.write_text('original generated C')
    measured['artifacts'].append({'path':str(generated),'kind':'tree',
                                  'sha256':module.tree_fingerprint(generated)})
    cache.save(preferences,measured)
    assert cache.load(preferences)==measured
    # An editor need not update manifest.json. Contents, added files and links
    # must still invalidate the project associated with a measured binary.
    header.write_text('hand edited C')
    assert cache.load(preferences) is None
    header.write_text('original generated C')
    assert cache.load(preferences)==measured
    added=generated/'new.h';added.write_text('new dependency')
    assert cache.load(preferences) is None
    added.unlink();added.symlink_to(artifact)
    assert cache.load(preferences) is None
    added.unlink()
    for changed in ({'metrics':{'stack':10**1000}}, {'artifacts':['invalid']}):
        invalid=dict(measured,**changed)
        cache.path(preferences).write_text(json.dumps({'format':'vexel-measurement-1','identity':cache.identity,
            'preferences':preferences,'measurement':invalid}))
        assert cache.load(preferences) is None
print("Measured search: bounded joint choices, checked cache identity/artifacts, regression limits and tradeoff frontier passed")
