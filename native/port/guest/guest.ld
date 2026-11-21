/* Guest image: one static executable at GUEST_BASE (port/guest/guest_abi.h), overlays grouped in their sections. */
ENTRY(g_guest_header)
SECTIONS {
    . = 0x10000000;
    .guest_header : { KEEP(*(.guest_header)) }
    .text : { EXCLUDE_FILE(*/ov00?/*) *(.text .text.*) }
    ov000_text : { PROVIDE(__start_ov000_text = .); */ov000/*(.text .text.*) PROVIDE(__stop_ov000_text = .); }
    ov001_text : { PROVIDE(__start_ov001_text = .); */ov001/*(.text .text.*) PROVIDE(__stop_ov001_text = .); }
    ov002_text : { PROVIDE(__start_ov002_text = .); */ov002/*(.text .text.*) PROVIDE(__stop_ov002_text = .); }
    ov003_text : { PROVIDE(__start_ov003_text = .); */ov003/*(.text .text.*) PROVIDE(__stop_ov003_text = .); }
    . = ALIGN(0x10000);
    .rodata : { EXCLUDE_FILE(*/ov00?/*) *(.rodata .rodata.*) }
    ov000_rodata : { PROVIDE(__start_ov000_rodata = .); */ov000/*(.rodata .rodata.*) PROVIDE(__stop_ov000_rodata = .); }
    ov001_rodata : { PROVIDE(__start_ov001_rodata = .); */ov001/*(.rodata .rodata.*) PROVIDE(__stop_ov001_rodata = .); }
    ov002_rodata : { PROVIDE(__start_ov002_rodata = .); */ov002/*(.rodata .rodata.*) PROVIDE(__stop_ov002_rodata = .); }
    ov003_rodata : { PROVIDE(__start_ov003_rodata = .); */ov003/*(.rodata .rodata.*) PROVIDE(__stop_ov003_rodata = .); }
    . = ALIGN(0x10000);
    .data : { EXCLUDE_FILE(*/ov00?/*) *(.data .data.* .sdata .sdata.*) }
    ov000_data : { PROVIDE(__start_ov000_data = .); */ov000/*(.data .data.*) PROVIDE(__stop_ov000_data = .); }
    ov001_data : { PROVIDE(__start_ov001_data = .); */ov001/*(.data .data.*) PROVIDE(__stop_ov001_data = .); }
    ov002_data : { PROVIDE(__start_ov002_data = .); */ov002/*(.data .data.*) PROVIDE(__stop_ov002_data = .); }
    ov003_data : { PROVIDE(__start_ov003_data = .); */ov003/*(.data .data.*) PROVIDE(__stop_ov003_data = .); }
    .bss : { EXCLUDE_FILE(*/ov00?/*) *(.bss .bss.* .sbss .sbss.* COMMON) }
    ov000_bss : { PROVIDE(__start_ov000_bss = .); */ov000/*(.bss .bss.* COMMON) PROVIDE(__stop_ov000_bss = .); }
    ov001_bss : { PROVIDE(__start_ov001_bss = .); */ov001/*(.bss .bss.* COMMON) PROVIDE(__stop_ov001_bss = .); }
    ov002_bss : { PROVIDE(__start_ov002_bss = .); */ov002/*(.bss .bss.* COMMON) PROVIDE(__stop_ov002_bss = .); }
    ov003_bss : { PROVIDE(__start_ov003_bss = .); */ov003/*(.bss .bss.* COMMON) PROVIDE(__stop_ov003_bss = .); }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
