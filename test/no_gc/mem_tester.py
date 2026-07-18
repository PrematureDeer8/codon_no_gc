import subprocess
import pathlib
import sys


class Leaks:
    leaks = "leaks";
    arg_atExit = "-atExit";
    arg_quiet = "-quiet";

class MemoryTester:
    tmp_file = "tmp.py";
    def __init__(self, program : pathlib.Path | str, libomp_dylib_path : pathlib.Path | None = None, codon_exe_path : pathlib.Path | None = None, 
                 memdebugger_path : pathlib.Path | None = None):
        
        if(isinstance(codon_exe_path, pathlib.Path) and not codon_exe_path.exists()):
            raise ValueError(f"Codon executable does not exist:\n{codon_exe_path}");
        self.codon_exe_path : str = str(codon_exe_path.absolute()) if codon_exe_path else "codon";
        
        # codon file (*.py)
        if(isinstance(program, pathlib.Path)):
            if(not program.exists()):
                raise ValueError(f"Program path does not exist:\n{program}");
            self.program_code : str = str(program.read_text());
        #raw code
        else:
            self.program_code : str = program;
        
        self.tmp_file_path : pathlib.Path = pathlib.Path.cwd() / self.tmp_file;
        self.tmp_file_path.touch(exist_ok=True);
        self.tmp_file_path.write_text(self.program_code);
        self.tmp_dir : pathlib.Path = self.tmp_file_path.parent;

        if(isinstance(memdebugger_path, pathlib.Path) and not memdebugger_path.exists()):
            raise ValueError(f"Memory Debugger Path does not exist:\n{memdebugger_path}");
        # only functionality for MacOS through memory debugger leaks
        if(sys.platform == "darwin"):
            self.memdebugger_path : str = str(memdebugger_path.absolute()) if memdebugger_path else "leaks";
            if(memdebugger_path):
                if(Leaks.leaks not in memdebugger_path.name):
                    raise Warning(f"Recommended Debugger is {Leaks.leaks} for MacOS!");
        else:
            raise NotImplementedError(f"Unknown OS: {sys.platform}\nSupported OS: MacOS");

        self.libomp_dylib_path = libomp_dylib_path;
        # find libomp.dylib
        if(libomp_dylib_path == None):
            if(sys.platform == "darwin"):
                self.libomp_dylib_path = pathlib.Path("/opt/homebrew/opt/libomp/lib/libomp.dylib");


    def build(self):
        # TODO: add optional arguments
        exe_name = "tmp"; 
        build_process = subprocess.run([self.codon_exe_path, "build", self.tmp_file_path]);
        self.tmp_exe_path = self.tmp_file_path.parent / exe_name;

        libomp_tmp_dir = self.tmp_dir / "libomp.dylib";
        if(not libomp_tmp_dir.exists()):
            self.libomp_dylib_path.copy(libomp_tmp_dir);
        

    def analyze_mem_leaks(self):
        # TODO: add functionality for other debuggers beside leaks
        mem_debugger_process = subprocess.run([Leaks.leaks, Leaks.arg_quiet, Leaks.arg_atExit, 
                                        "--", str(self.tmp_exe_path.absolute())], 
                                        capture_output=True, text=True);
        return mem_debugger_process;

    # delete tmp files
    def clean(self):
        # Should libomp.dylib be deleted as well?
        self.tmp_exe_path.unlink(missing_ok=True);
        self.tmp_file_path.unlink(missing_ok=True);

# program test code
fstring_test1 = \
"""
from internal.gc import free

x : int = 5;
my_str : str = f'x = {x}';

free(my_str._ptr);
"""