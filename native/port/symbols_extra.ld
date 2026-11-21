/* Symbols the native build aliases by hand (see each comment). */

/* daDonketu_c declares CleanupResources and Render without defining them: its vtable slots hold daOts_c's, as in
   the cartridge's vtable. */
PROVIDE("_ZN11daDonketu_c16CleanupResourcesEv" = "_ZN7daOts_c16CleanupResourcesEv");
PROVIDE("_ZN11daDonketu_c6RenderEv" = "_ZN7daOts_c6RenderEv");

/* The stack sizes of the original link (they only size the DTCM arena here: threads run on host stacks). */
PROVIDE(SDK_IRQ_STACKSIZE = 0x400);
PROVIDE(SDK_SYS_STACKSIZE = 0x800);
