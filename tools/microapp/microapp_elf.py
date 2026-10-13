"""Strict ELF32 ARM ET_DYN audit. Fixups come exclusively from ELF relocations."""
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from elftools.elf.sections import SymbolTableSection

def extract_elf(path: Path, float_abi: str) -> dict:
    with path.open("rb") as stream:
        e = ELFFile(stream)
        file_size = path.stat().st_size
        for section in e.iter_sections():
            if section['sh_type'] != 'SHT_NOBITS' and not (
                    section['sh_offset'] <= file_size and
                    section['sh_size'] <= file_size-section['sh_offset']):
                raise ValueError("ELF section exceeds file bounds")
        if e.elfclass != 32 or not e.little_endian or e['e_machine'] != 'EM_ARM' or e['e_type'] != 'ET_DYN':
            raise ValueError("requires little endian ELF32 ARM ET_DYN")
        if e['e_flags'] & 0xff000000 != 0x05000000:
            raise ValueError("requires ARM EABI5")
        attrs = e.get_section_by_name('.ARM.attributes')
        if attrs is None:
            raise ValueError("missing ARM attributes")
        tags = {a.tag: a.value for sub in attrs.iter_subsections()
                for group in sub.iter_subsubsections() for a in group.iter_attributes()}
        if tags.get('TAG_CPU_ARCH') != 13 or tags.get('TAG_CPU_ARCH_PROFILE') != ord('M'):
            raise ValueError("requires Cortex-M4 / ARMv7E-M")
        hard = tags.get('TAG_ABI_VFP_ARGS', 0) == 1
        fp = tags.get('TAG_FP_ARCH', 0) != 0
        if hard != (float_abi == 'hard') or (float_abi == 'soft' and fp):
            raise ValueError("ELF floating ABI differs from profile")
        symtab = e.get_section_by_name('.symtab')
        if symtab is None:
            raise ValueError("ELF must retain symbols and relocations")
        symbols = {s.name: s for s in symtab.iter_symbols() if s.name}
        for table in e.iter_sections():
            if isinstance(table, SymbolTableSection):
                for s in table.iter_symbols():
                    if s.name and s['st_shndx'] == 'SHN_UNDEF':
                        raise ValueError(f"unresolved symbol: {s.name}")
        required = ('__microapp_text_start','__microapp_text_end','__microapp_ram_start',
                    '__microapp_got_end','__microapp_data_start','__microapp_init_end',
                    '__microapp_bss_start','__microapp_ram_end','_GLOBAL_OFFSET_TABLE_','microapp_main')
        if any(name not in symbols for name in required):
            raise ValueError("use the SDK linker template and microapp_main entry")
        v = {name: symbols[name]['st_value'] for name in required}
        flash, flash_end = v[required[0]], v[required[1]]
        ram, init_end, ram_end = v[required[2]], v['__microapp_init_end'], v['__microapp_ram_end']
        if (flash != 0x10000000 or not flash < flash_end <= flash+0x20000 or
                ram != 0x20000000 or not ram <= init_end <= ram_end <= ram+0x10000):
            raise ValueError("invalid SDK logical layout")
        for segment in e.iter_segments():
            kind, addr, size, flags = segment['p_type'], segment['p_vaddr'], segment['p_memsz'], segment['p_flags']
            if kind == 'PT_LOAD':
                if segment['p_filesz'] > size or segment['p_paddr'] != addr:
                    raise ValueError("invalid load segment")
                if not ((flags == 5 and flash <= addr <= addr+size <= flash_end) or
                        (flags == 4 and flash_end <= addr <= addr+size <= 0x10020000) or
                        (flags == 6 and ram <= addr <= addr+size <= ram_end)):
                    raise ValueError("load segment violates code/metadata/RAM boundaries")
            elif kind == 'PT_DYNAMIC':
                if flags != 4 or not flash_end <= addr <= addr+size <= 0x10020000:
                    raise ValueError("invalid dynamic segment")
            elif kind != 'PT_GNU_STACK' or flags & 1:
                raise ValueError(f"unsupported ELF segment: {kind}")
        got = v['_GLOBAL_OFFSET_TABLE_']
        if got != ram:
            raise ValueError("GOT base must match the linker-defined RAM start")
        image = bytearray(flash_end-flash)
        initial = bytearray(init_end-ram)
        image_alignment, ram_alignment = 4, 8
        allowed = {'.text','.got','.data','.bss','.dynsym','.dynstr','.hash','.gnu.hash','.rel.dyn','.dynamic'}
        for s in e.iter_sections():
            if not s['sh_flags'] & 2 or not s['sh_size']:
                continue
            if s.name not in allowed or s['sh_flags'] & 0x400:
                raise ValueError(f"unsupported allocated section: {s.name} (TLS/constructors/PLT forbidden)")
            addr, size = s['sh_addr'], s['sh_size']
            if s.name == '.text':
                image_alignment = max(image_alignment,s['sh_addralign'])
                if s['sh_flags'] & 1 or addr != flash or addr+size != flash_end:
                    raise ValueError("invalid Flash text section")
                image[:] = s.data()
            elif s.name in ('.got','.data'):
                ram_alignment = max(ram_alignment,s['sh_addralign'])
                if not ram <= addr <= addr+size <= init_end or s['sh_flags'] & 4:
                    raise ValueError("invalid initialized RAM section")
                initial[addr-ram:addr-ram+size] = s.data()
            elif s.name == '.bss':
                ram_alignment = max(ram_alignment,s['sh_addralign'])
                if s['sh_type'] != 'SHT_NOBITS' or addr != v['__microapp_bss_start'] or addr+size != ram_end:
                    raise ValueError("invalid BSS section")
        # Three ABI-reserved GOT words (dynamic metadata and lazy loader slots)
        # are unused by this freestanding static binding model. They aren't code pointers.
        if len(initial) < 12:
            raise ValueError("missing GOT header")
        initial[:12] = bytes(12)
        fixups = {}
        audited = 0
        for rels in e.iter_sections():
            if not isinstance(rels, RelocationSection):
                continue
            if rels['sh_type'] != 'SHT_REL':
                raise ValueError("RELA is unsupported")
            dynamic = rels.name == '.rel.dyn'
            table = e.get_section(rels['sh_link'])
            target_section = None if dynamic else e.get_section(rels['sh_info'])
            for r in rels.iter_relocations():
                kind, off = r['r_info_type'], r['r_offset']
                audited += 1
                if dynamic:
                    if kind != 23 or r['r_info_sym'] != 0: # R_ARM_RELATIVE
                        raise ValueError(f"unsupported dynamic relocation {kind}")
                    if not ram+12 <= off <= init_end-4 or off % 4:
                        raise ValueError("dynamic relocation would modify Flash or invalid RAM")
                    pos = off-ram
                    if pos in fixups:
                        raise ValueError("duplicate RAM relocation")
                    value, = struct.unpack_from('<I',initial,pos)
                    if flash <= value <= flash_end:
                        fixups[pos] = (pos,value-flash,1)
                    elif ram <= value <= ram_end:
                        fixups[pos] = (pos,value-ram,2)
                    else:
                        raise ValueError(f"external absolute pointer 0x{value:08x}")
                    struct.pack_into('<I',initial,pos,0)
                else:
                    if not target_section or not target_section['sh_flags'] & 2:
                        continue # debug relocations have no runtime effect
                    sym = table.get_symbol(r['r_info_sym'])
                    if sym['st_shndx'] in ('SHN_UNDEF','SHN_ABS'):
                        raise ValueError(f"external/absolute symbol in relocation: {sym.name}")
                    if not target_section['sh_addr'] <= off < target_section['sh_addr']+target_section['sh_size']:
                        raise ValueError("relocation outside target section")
                    if target_section.name == '.text':
                        # PC-relative branches/rodata and r9-relative GOT accesses only.
                        if kind not in (3,10,25,26,30,51):
                            raise ValueError(f"Flash relocation {kind} requires unsupported patching")
                        if kind != 26 and not flash <= (sym['st_value'] & ~1) < flash_end:
                            raise ValueError("PC-relative reference escapes immutable image")
                    elif target_section.name in ('.data','.got'):
                        if kind != 2 or off-ram not in fixups:
                            # .rel.dyn can appear later in ELF: check after collection below.
                            if kind != 2:
                                raise ValueError(f"unsupported RAM relocation {kind}")
                    else:
                        raise ValueError("unsupported allocated relocation target")
        # Every static absolute RAM relocation must be backed by a dynamic record.
        for rels in e.iter_sections():
            if isinstance(rels,RelocationSection) and rels.name != '.rel.dyn' and rels['sh_info']:
                s = e.get_section(rels['sh_info'])
                if s.name in ('.got','.data'):
                    for r in rels.iter_relocations():
                        if r['r_offset']-ram not in fixups:
                            raise ValueError("RAM pointer lacks dynamic relocation")
        entry = v['microapp_main']
        if entry != e['e_entry'] or not entry & 1 or not flash <= (entry & ~1) < flash_end:
            raise ValueError("invalid Thumb entry")
        return dict(image=bytes(image), initial=bytes(initial), fixups=[fixups[k] for k in sorted(fixups)],
                    entry_offset=entry-flash, ram_size=ram_end-ram, ram_alignment=ram_alignment,
                    image_alignment=image_alignment,
                    got_offset=got-ram, got_size=v['__microapp_got_end']-got,
                    bss_offset=v['__microapp_bss_start']-ram,
                    bss_size=ram_end-v['__microapp_bss_start'],
                    data_size=init_end-v['__microapp_data_start'], audited_relocations=audited)
