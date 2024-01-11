dasm dss/test.dss -o test.bin --save-disassembly disassembly/test.dds --verbose
ddb config/testMachine.dvm --force-load test.bin 0x00 --verbose-load --hide-vdisplay