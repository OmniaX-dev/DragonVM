#ifndef __SOME_DEFINE__
#define __SOME_DEFINE__

#include <someFile.dsc>

#data_alloc(128)   // Total data memory allocated

//Types: byte, word, word, string, bool

struct SomeStruct {
	var data1: byte = 0;
	var data2: word;
	var data3: string = "Hello";   	//Basically equivalent to var data3: byte[] = { 'H', 'e', 'l', 'l', 'o', 0 }
	var data4: bool = false;       	//<true> and <false> are respectively replaced with <1> and <0>, and
									//<bool> is actually equivalent to <byte>
	var data5: byte[] = { 4, 1, 4, 2 }; 
	var data6: word[10];
}

/** DASM Translation

	@struct SomeStruct
		data1:1 > 0x00
		data2:4;
		data3:6 > 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x00
		data4:1 > 0x00
		data5:4 > 0x04, 0x01, 0x04, 0x02
		data6:20
	@end
**/

fn testFunction(var param1:word, var param2:string) -> word {
	if (length<param2> == 0) {
		return 0;
	} else {
		return param1;
	}
}

/** DASM Translation

	__fn_testFunction_2_word_byteptr__:
		arg R1		## param1 (word)
		arg R2		## param2 (string)
		mov ACC, 6
		ret
**/

fn strlen(var str:string) -> word {
	if (str == null) { return 0; }
	var c:byte = str[0];
	var counter:word = 0;
	while (c != 0) {
		counter++;
		c = str[counter];
	}
	return counter;
}

fn strlen(var str:string) -> word {
	dasm {
		arg R1
		mov R2, 0
	_strlen_loop:
		movb ACC, *R1
		inc R1
		jeq $_strlen_loop_end, 0
		inc R2
		jmp $_strlen_loop
	_strlen_loop_end:
			mov RV, R2
	ret
	}
}

/** DASM Translation

	__fn_strlen_1_byteptr__:
		arg R1		## param2 (string)
		mov ACC, R1
		jne $__fn_strlen_1_byteptr__branch_1_end__, 0
	__fn_strlen_1_byteptr__branch_1__:
		mov RV, 0
		jmp $__fn_strlen_1_byteptr__end__
	__fn_strlen_1_byteptr__branch_1_end__:
		omovb *FP, *R1, -1		## c
		omov *FP, 0x0000, -3		## counter 
	__fn_strlen_1_byteptr__loop_1__:
		movbo ACC, *FP, -1
		jeq, $__fn_strlen_1_byteptr__loop_1_end__, 0
		movo ACC, *FP, -3
		inc ACC
		omov *FP, ACC, -3
		omovb *FP, *ACC, -1
		jmp $__fn_strlen_1_byteptr__loop_1__
	__fn_strlen_1_byteptr__loop_1_end__:
		movo RV, *FP, -3
	__fn_strlen_1_byteptr__end__
		ret
**/

#endif