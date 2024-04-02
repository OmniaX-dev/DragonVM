dasm dss/tests/test1.dss -o test1.bin --save-disassembly disassembly/test1.dds --verbose
ddb config/testMachine.dvm --force-load test1.bin 0x00 --verbose-load --hide-vdisplay