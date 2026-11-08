// Register-preserving tail-call veneers (ldr ip,[pc]; bx ip): the original
// forwards r0-r3 and the result untouched. VENEER(veneer, target).
VENEER(0x02038420u, 0x02037024u) // dBgCh_Actr_UpdateDiscreteNoLava_veneer
VENEER(0x02038414u, 0x02036ee4u) // func_02038414
VENEER(0x02038408u, 0x02036accu) // func_02038408
VENEER(0x020383fcu, 0x020366b4u) // dBgCh_Actr_UpdateContinuous_Veneer
VENEER(0x020383f0u, 0x02036318u) // func_020383f0
VENEER(0x020383e4u, 0x020358acu) // func_020383e4
VENEER(0x0201aac8u, 0x02059ba0u) // func_0201aac8
VENEER(0x02014a20u, 0x0201493cu) // func_02014a20
VENEER(0x0203c178u, 0x020527e8u) // func_0203c178
