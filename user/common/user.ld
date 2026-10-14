OUTPUT_ARCH(arm)
ENTRY(__minemu_user_start)

/*
 * One load segment per permission set, each starting on its own page, so the
 * kernel can map code read+execute, constants read-only, and data/BSS
 * read+write (never executable).
 */
PHDRS
{
  text PT_LOAD FLAGS(5);   /* R-X */
  rodata PT_LOAD FLAGS(4); /* R-- */
  data PT_LOAD FLAGS(6);   /* RW- */
}

SECTIONS
{
  . = 0x00400000;
  .text : { *(.text*) } :text
  . = ALIGN(4096);
  .rodata : { *(.rodata*) } :rodata
  . = ALIGN(4096);
  .data : { *(.data*) } :data
  .bss (NOLOAD) : { *(.bss*) *(COMMON) } :data
}
