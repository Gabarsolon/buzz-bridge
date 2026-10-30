"""Exact finished inputs; both packaging and acceptance pin these two hashes."""
import hashlib
ASSETS={
 'ps2_netemu.self':'74e890bf7f77edfd92d4c0a57f45e73cf7fe39aa3c21cd7587948fb3c81ade40',
 'ps2_netemu_original.self':'bad3e45c9a81f25726e414b146ea5a112f28216ab4bdd2fd9f5c81b33d04c80a',
}
def verify_assets(folder):
    assert {p.name for p in folder.iterdir() if p.is_file()}==set(ASSETS), 'Unexpected bundled file set'
    for name,want in ASSETS.items():
        assert hashlib.sha256((folder/name).read_bytes()).hexdigest()==want, 'Wrong finished netemu: '+name
