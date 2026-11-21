"""Read the compiler's closed program publication without parsing generated C."""
import json
from pathlib import Path


def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def load(directory, report=None):
    directory = Path(directory)
    image = json.loads((directory / 'program-image.json').read_text())
    if image.get('format') != 'vexel-program-image-1':
        raise ValueError('unsupported program image')
    if report is None:
        report = json.loads((directory / 'source-plan.json').read_text())
    if report.get('status') != 'resolved' or report.get('program_image') != image:
        raise ValueError('program image does not belong to this closed source plan')
    contents = {}
    for artifact in image['artifacts']:
        name = artifact['path']
        if Path(name).name != name or name in contents:
            raise ValueError('invalid or duplicate program artifact: ' + name)
        data = (directory / name).read_bytes()
        if len(data) != artifact['bytes'] or fingerprint(data) != artifact['fnv1a']:
            raise ValueError('program artifact changed after publication: ' + name)
        contents[name] = data
    if b''.join(contents[n] for n in image['segments']) != contents['vx_bridges.c']:
        raise ValueError('program units do not cover the complete C projection')
    units = {u['symbol']: u for u in image['units']}
    if len(units) != len(image['units']) or len(image['segments']) != len(set(image['segments'])):
        raise ValueError('duplicate program unit or segment')
    for unit in units.values():
        if unit.get('support') and unit['support'] not in contents:
            raise ValueError('program unit support is not a verified artifact: ' + unit['symbol'])
    fragments = {u['fragment'] for u in units.values()}
    if not fragments.issubset(image['segments']):
        raise ValueError('program unit is not in the assembly sequence')
    support = b''.join(contents[n] for n in image['segments'] if n not in fragments)
    if support != contents[image['support']]:
        raise ValueError('program support disagrees with the assembly sequence')
    if {r['name'] for r in image['roots']} != {s['name'] for s in report['sources']}:
        raise ValueError('program entry coverage mismatch')
    if any(units[r['unit']]['kind'] != 'source' for r in image['roots']):
        raise ValueError('program entry lacks its source unit')
    return image
