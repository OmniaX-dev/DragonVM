#ifndef __SOME_DEFINE__
#define __SOME_DEFINE__ 0

#include <file.dsh>


struct SomeStruct {
	//var data1;
	int data2;
	int data3 = 0;
	string data4 = "Hello";		//string type is basically a byte array with a 0 at the end
	int[10] data6;				//Array
	int[] data7 = { 2, 5, 7 };  //Array
	int* data8;                 //Pointer
	SomeStruct* data9 = null;
};

fn main(void) -> int {
	let myVar: SomeStruct;
	(*myVar.data8) = 12;
	let myVar2: int* = myVar.data8;
	myFunction(myVar2, 10);
	dstd::print("The result is %d!", (*myVar2));
}

fn myFunction(int* num1_ptr, int num2) -> void {
	(*num1_ptr) = (*num1_ptr) + num2;
}

namespace dstd {
	fn print(string fmt, var args@) -> int {
		let _args_len: int = args_count(args);
		let _args_counter: int = 0;
		let _ctrl_char: bool = false;
		let _ret_val: int = 0;
		for (byte c in fmt) {
			if (!_ctrl_char) {
				_ret_val = __internal_dstd::__print_char_in_display_mode(__internal_dstd::__display_modes::__text_single_color, c);
				continue;
			}
			if (c == '%') { _ctrl_char = true; continue; }
			if (_ctrl_char) {
				if (c == 'd') {
					_ctrl_char = false;
					if (_args_len < 1) {
						continue;
					}
					let _integer: int = args_get_int(_args_counter);
					_args_counter++;
					_ret_val = __internal_dstd::__print_integer_in_display_mode(__internal_dstd::__display_modes::__text_single_color, _integer);
				}
			}
		}
	}
}

#endif