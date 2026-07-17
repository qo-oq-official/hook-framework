"""PE file parser: export table + section headers (stdlib only, no imports)."""
import struct
from pathlib import Path
from typing import Set, Optional, Dict


class PEFile:
    """Minimal PE parser for export table enumeration and RVA validation."""

    def __init__(self, path: str):
        self._path = path
        self._data = Path(path).read_bytes()
        self._exports: Optional[Set[str]] = None
        self._sections = self._parse_sections()
        self._export_dir = self._parse_export_dir()

    # ── section helpers ──────────────────────────────────────────────────

    def _parse_sections(self):
        pe_offset = struct.unpack_from("<I", self._data, 0x3C)[0]
        # IMAGE_FILE_HEADER: Machine(2) + NumberOfSections(2) + ... = 20 bytes
        num_sections = struct.unpack_from("<H", self._data, pe_offset + 4 + 2)[0]
        # IMAGE_OPTIONAL_HEADER size
        opt_hdr_size = struct.unpack_from("<H", self._data, pe_offset + 4 + 16)[0]
        # first section header
        sec_base = pe_offset + 4 + 20 + opt_hdr_size

        sections = []
        for i in range(num_sections):
            off = sec_base + i * 40  # IMAGE_SECTION_HEADER = 40 bytes
            name = self._data[off:off + 8].rstrip(b'\x00').decode('ascii', errors='replace')
            virtual_size = struct.unpack_from("<I", self._data, off + 8)[0]
            virtual_addr = struct.unpack_from("<I", self._data, off + 12)[0]
            raw_size = struct.unpack_from("<I", self._data, off + 16)[0]
            raw_ptr = struct.unpack_from("<I", self._data, off + 20)[0]
            characteristics = struct.unpack_from("<I", self._data, off + 36)[0]
            sections.append({
                'name': name,
                'va': virtual_addr,
                'size': virtual_size,
                'raw_ptr': raw_ptr,
                'raw_size': raw_size,
                'exec': bool(characteristics & 0x20000000),  # IMAGE_SCN_MEM_EXECUTE
            })
        return sections

    def rva_to_offset(self, rva: int) -> int:
        for s in self._sections:
            if s['va'] <= rva < s['va'] + max(s['size'], s['raw_size']):
                return rva - s['va'] + s['raw_ptr']
        raise ValueError(f"RVA 0x{rva:x} not in any section")

    def is_code_rva(self, rva: int) -> bool:
        for s in self._sections:
            if s['va'] <= rva < s['va'] + max(s['size'], s['raw_size']):
                return s['exec']
        return False

    # ── export table ─────────────────────────────────────────────────────

    def _parse_export_dir(self):
        pe_offset = struct.unpack_from("<I", self._data, 0x3C)[0]
        # OptionalHeader.DataDirectory[0] = export directory (RVA + size)
        opt_hdr_size = struct.unpack_from("<H", self._data, pe_offset + 4 + 16)[0]
        data_dir_off = pe_offset + 4 + 20 + opt_hdr_size - 16 * 8  # DataDirectory starts at end of optional header - 128
        # Actually: opt_hdr_size = size of optional header.
        # DataDirectory = pe_offset + 4 + 20 + opt_hdr_size - 16 * 8? 
        # No. The data directory is at pe_offset + 4 + 20 + 96 = pe_offset + 120 for PE32+, or 112 for PE32.
        # Simpler: parse the magic first bytes to know PE32 vs PE32+
        magic = struct.unpack_from("<H", self._data, pe_offset + 4 + 20)[0]
        if magic == 0x20B:  # PE32+
            data_dir_off = pe_offset + 4 + 20 + 112
        else:
            data_dir_off = pe_offset + 4 + 20 + 96

        export_rva = struct.unpack_from("<I", self._data, data_dir_off)[0]
        export_size = struct.unpack_from("<I", self._data, data_dir_off + 4)[0]
        if export_rva == 0 or export_size == 0:
            return None
        return {'rva': export_rva, 'size': export_size}

    def exports(self) -> Set[str]:
        if self._exports is not None:
            return self._exports
        self._exports = set()
        if self._export_dir is None:
            return self._exports

        try:
            off = self.rva_to_offset(self._export_dir['rva'])
        except ValueError:
            return self._exports

        # IMAGE_EXPORT_DIRECTORY
        num_names = struct.unpack_from("<I", self._data, off + 24)[0]
        if num_names == 0:
            return self._exports

        addr_names_rva = struct.unpack_from("<I", self._data, off + 32)[0]
        try:
            name_off = self.rva_to_offset(addr_names_rva)
        except ValueError:
            return self._exports

        for i in range(min(num_names, 100000)):
            name_rva = struct.unpack_from("<I", self._data, name_off + i * 4)[0]
            try:
                str_off = self.rva_to_offset(name_rva)
            except ValueError:
                continue
            end = self._data.find(b'\x00', str_off)
            if end < 0:
                end = str_off + 256
            name = self._data[str_off:end].decode('ascii', errors='replace')
            if name:
                self._exports.add(name)
        return self._exports

    def has_export(self, symbol: str) -> bool:
        return symbol in self.exports()

    def export_rvas(self) -> Dict[str, int]:
        """Return {symbol_name: RVA} for all exports."""
        result = {}
        if self._export_dir is None:
            return result
        try:
            exp_off = self.rva_to_offset(self._export_dir['rva'])
        except ValueError:
            return result

        # IMAGE_EXPORT_DIRECTORY fields
        num_functions = struct.unpack_from("<I", self._data, exp_off + 20)[0]
        num_names     = struct.unpack_from("<I", self._data, exp_off + 24)[0]
        addr_func_rva = struct.unpack_from("<I", self._data, exp_off + 28)[0]
        addr_name_rva = struct.unpack_from("<I", self._data, exp_off + 32)[0]
        addr_ord_rva  = struct.unpack_from("<I", self._data, exp_off + 36)[0]

        if num_names == 0:
            return result

        try:
            func_off = self.rva_to_offset(addr_func_rva)
            name_off = self.rva_to_offset(addr_name_rva)
            ord_off  = self.rva_to_offset(addr_ord_rva)
        except ValueError:
            return result

        for i in range(min(num_names, 200000)):
            name_rva = struct.unpack_from("<I", self._data, name_off + i * 4)[0]
            try:
                str_off = self.rva_to_offset(name_rva)
            except ValueError:
                continue
            end = self._data.find(b'\x00', str_off)
            if end < 0:
                end = str_off + 256
            name = self._data[str_off:end].decode('ascii', errors='replace')
            if not name:
                continue

            ordinal_index = struct.unpack_from("<H", self._data, ord_off + i * 2)[0]
            if ordinal_index < num_functions:
                func_rva = struct.unpack_from("<I", self._data, func_off + ordinal_index * 4)[0]
                result[name] = func_rva

        return result
