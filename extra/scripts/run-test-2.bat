dasm dss/test2.dss -o test2.bin --save-disassembly disassembly/test2.dds --verbose
ddb config/testMachine.dvm --force-load test2.bin 0x00 --verbose-load --hide-vdisplay