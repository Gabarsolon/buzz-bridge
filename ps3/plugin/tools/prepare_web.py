"""Verify the frozen, already-derived inputs without game data or external trees."""
import hashlib,json,pathlib
ROOT=pathlib.Path(__file__).resolve().parents[2]
manifest=json.loads((ROOT/'plugin/autoload/frozen_inputs.json').read_text())
for name,digest in manifest.items():
    if name.startswith('plugin/web/'):
        assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,'Changed frozen input: '+name
print('Frozen inputs verified; no game ELF or scratchpad needed.')
