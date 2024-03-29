#!/usr/bin/python3

import sys, csv

# csv file found here https://raw.githubusercontent.com/kpmiller/emulator101/master/6502Disassembler/6502ops.csv

def main():
	if len(sys.argv) < 3:
		print('Usage: python3 header-gen.py CSV_FILE PATH_TO_GENERATED_HEADER_FILE')
		exit(0)
	
	mnemonics = ['unknown'] * 256

	with open(sys.argv[1]) as csvfile:
		instr_reader = csv.reader(csvfile)
		next(instr_reader)
		for row in instr_reader:
			if len(row) == 0:
				continue
			mnemonic = row[0]
			opcode = int(row[1], 16)

			mnemonics[opcode] = mnemonic

	unknown_opcode_function = 'int handle_instr_unknown(bc_interpreter_t *interpreter, bool wide);'
	function_headers = [f'int handle_instr_{m}(bc_interpreter_t *interpreter, bool wide);' for m in mnemonics if m != "unknown"]

	gen_file = open(sys.argv[2], 'w')
	gen_file.write('// DO NOT EDIT THIS FILE. ALL CHANGES WILL BE ERASED WHEN THIS FILE IS REGENERATED\n\n')
	gen_file.write('#include <stdbool.h>\n#include "jthread.h"\n\n')
	gen_file.write('static const char *instr_names[256] = {\n')
	for m in mnemonics:
		gen_file.write('\t"' + m + '",\n')
	gen_file.write('};\n\n')
	gen_file.write(unknown_opcode_function)
	gen_file.write('\n')
	for f in function_headers:
		gen_file.write(f)
		gen_file.write('\n')

	opcode_handle_map = []
	for m in mnemonics:
		opcode_handle_map.append(f'handle_instr_{m}')

	gen_file.write('\nstatic int (* const instr_table[256])(bc_interpreter_t *interpreter, bool wide) = {\n')
	for handle in opcode_handle_map:
		gen_file.write(f'\t{handle},\n')
	gen_file.write('};')
	gen_file.close()

if __name__ == '__main__':
	main()
