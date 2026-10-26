"""Compatibility entry point for the 0.8 derived-profile header generator."""
import pathlib,runpy,sys
folder=pathlib.Path(__file__).resolve().parents[2]/'tools'
sys.path.insert(0,str(folder))
runpy.run_path(str(folder/'make_profiles.py'),run_name='__main__')
