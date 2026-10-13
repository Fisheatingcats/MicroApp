/* Logical ELF addresses only. Neither is an installation address. */
ENTRY(microapp_main)
MEMORY {
    CODE (rx) : ORIGIN = 0x10000000, LENGTH = 128K
    DATA (rw) : ORIGIN = 0x20000000, LENGTH = 64K
}
PHDRS {
    code PT_LOAD FLAGS(5);
    metadata PT_LOAD FLAGS(4);
    ram PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(4);
}
SECTIONS {
    .text : { __microapp_text_start = .; KEEP(*(.text.microapp_main))
        *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4);
        __microapp_text_end = .; } > CODE :code
    /* Loader metadata is audited from ELF; it is never installed. */
    .dynsym : { *(.dynsym) } > CODE :metadata
    .dynstr : { *(.dynstr) } > CODE :metadata
    .hash : { *(.hash) } > CODE :metadata
    .gnu.hash : { *(.gnu.hash) } > CODE :metadata
    .rel.dyn : { *(.rel.dyn) } > CODE :metadata
    .dynamic : { *(.dynamic) } > CODE :metadata :dynamic
    .got : { __microapp_ram_start = .; *(.got.plt) *(.got)
        __microapp_got_end = .; } > DATA :ram
    .data : { __microapp_data_start = .;
        *(.data .data.*) *(.data.rel.ro .data.rel.ro.*)
        . = ALIGN(4); __microapp_init_end = .; } > DATA :ram
    .bss ALIGN(8) (NOLOAD) : { __microapp_bss_start = .;
        *(.bss .bss.* COMMON) . = ALIGN(8); __microapp_ram_end = .; } > DATA :ram
    /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note*) }
}
