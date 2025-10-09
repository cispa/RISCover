from pyutils.cpuinfo import CPUInfo
import pyutils.config as config
import hashlib

class RiscvFakeCPUInfo(CPUInfo):
    def __init__(self, proc_cpuinfo):
        super().__init__()

        # Hash the proc_cpuinfo string using SHA256 and convert to an integer
        digest = hashlib.sha256(proc_cpuinfo.encode('utf-8')).hexdigest()
        hash_int = int(digest, 16)

        # Use different parts of the hash integer to populate fields
        self.implementer = (hash_int >> 0)  & 0xFF  # 8 bits
        self.variant     = (hash_int >> 8)  & 0xF   # 4 bits
        self.part        = (hash_int >> 12) & 0xFFF # 12 bits
        self.revision    = (hash_int >> 24) & 0xF   # 4 bits

        # Hardcoded architecture for RISC-V
        self.architecture = 0xf
