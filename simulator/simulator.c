/*
 * Author: Daniel Kopta
 * Updated by: Erin Parker
 * CS 4400, University of Utah
 *
 * Simulator handout
 * A simple x86-like processor simulator.
 * Read in a binary file that encodes instructions to execute.
 * Simulate a processor by executing instructions one at a time and appropriately 
 * updating register and memory contents.
 *
 * Some code and pseudo code has been provided as a starting point.
 *
 * Completed by: STUDENT-FILL-IN
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "instruction.h"

// Forward declarations for helper functions
unsigned int get_file_size(int file_descriptor);
unsigned int* load_file(int file_descriptor, unsigned int size);
instruction_t* decode_instructions(unsigned int* bytes, unsigned int num_instructions);
unsigned int execute_instruction(unsigned int program_counter, instruction_t* instructions, 
				 int* registers, unsigned char* memory);
void print_instructions(instruction_t* instructions, unsigned int num_instructions);
void error_exit(const char* message);

// 17 registers
#define NUM_REGS 17
// 1024-byte stack
#define STACK_SIZE 1024

int main(int argc, char** argv)
{
  // Make sure we have enough arguments
  if(argc < 2)
    error_exit("must provide an argument specifying a binary file to execute");

  // Open the binary file
  int file_descriptor = open(argv[1], O_RDONLY);
  if (file_descriptor == -1) 
    error_exit("unable to open input file");

  // Get the size of the file
  unsigned int file_size = get_file_size(file_descriptor);
  // Make sure the file size is a multiple of 4 bytes
  // since machine code instructions are 4 bytes each
  if(file_size % 4 != 0)
    error_exit("invalid input file");

  // Load the file into memory
  // We use an unsigned int array to represent the raw bytes
  // We could use any 4-byte integer type
  unsigned int* instruction_bytes = load_file(file_descriptor, file_size);
  close(file_descriptor);

  unsigned int num_instructions = file_size / 4;

  /****************************************/
  /**** Begin code to modify/implement ****/
  /****************************************/


  // Allocate and decode instructions (left for you to fill in)
  instruction_t* instructions = decode_instructions(instruction_bytes, num_instructions);


  // Optionally print the decoded instructions for debugging
  // Will not work until you implement decode_instructions
  // Do not call this function in your submitted final version
  // print_instructions(instructions, num_instructions);


  // Once you have completed Part 1 (decoding instructions), uncomment the below block
  

  // Allocate and initialize registers to 0. Register %esp, aka stack point, should be 1024.
  int* registers = (int*)malloc(sizeof(int) * NUM_REGS);
  for (int i = 0; i < NUM_REGS; i++){
    registers[i] = 0;
  }
  registers[6] = 1024;

  // Stack memory is byte-addressed, so it must be a 1-byte type, must be a 1024 byte stack
  // TODO allocate the stack memory. Do not assign to NULL.
  unsigned char* memory = malloc(sizeof(char) * 1024);

  // Run the simulation
  unsigned int program_counter = 0;

  // program_counter is a byte address, so we must multiply num_instructions by 4 
  // to get the address past the last instruction
  while(program_counter != num_instructions * 4)
  {
    program_counter = execute_instruction(program_counter, instructions, registers, memory);
  }
  
  return 0;
}

/*
 * Decodes the array of raw instruction bytes into an array of instruction_t
 * Each raw instruction is encoded as a 4-byte unsigned int
*/
instruction_t* decode_instructions(unsigned int* bytes, unsigned int num_instructions)
{
  instruction_t* retval = malloc(sizeof(instruction_t) * num_instructions);

  for (int i = 0; i < num_instructions; i++){
    int instruction = bytes[i];
    
    int opcode = (instruction >> 27) & 0x1F;
    int reg1 = (instruction >> 22) & 0x1F;
    int reg2 = (instruction >> 17) & 0x1F;
    int immediate = instruction & 0xFFFF;
    
    if (immediate & 0x8000){
      immediate |= 0xFFFF0000;
    }
    
  instruction_t instruction_struct;
  instruction_struct.opcode = opcode;
  instruction_struct.first_register = reg1;
  instruction_struct.second_register = reg2;
  instruction_struct.immediate = immediate;
  retval[i] = instruction_struct;
  }

  return retval;
}


/*
 * Executes a single instruction and returns the next program counter
*/
unsigned int execute_instruction(unsigned int program_counter, instruction_t* instructions, int* registers, unsigned char* memory)
{
  // program_counter is a byte address, but instructions are 4 bytes each
  // divide by 4 to get the index into the instructions array
  instruction_t instr = instructions[program_counter / 4];
  
  // Registers constanly used
  int* eflags = &registers[16]; // Register ID of 16
  int* esp = &registers[6]; // Stack pointer register
  int* reg1 = &registers[instr.first_register]; // Register address of first register in the instruction
  int* reg2 = &registers[instr.second_register];// Register address of second register in the instruction

  // Indeces for opflags
  int CF_bit_index = 0;
  int ZF_bit_index = 6;
  int SF_bit_index = 7;
  int OF_bit_index = 11;

  // Bools for opcodes
  int CF = (*eflags >> (CF_bit_index)) & 1;
  int ZF = (*eflags >> (ZF_bit_index)) & 1;
  int SF = (*eflags >> (SF_bit_index)) & 1;
  int OF = (*eflags >> (OF_bit_index)) & 1;

  switch(instr.opcode)
  {
  case subl:
    *reg1 = *reg1 - instr.immediate;
    break;
  
  case addl_reg_reg:
    *reg2 = *reg1 + *reg2;
    break;
  
  case addl_imm_reg:
    // printf("Adding immediate: %d r1 + %d imm", registers[instr.first_register], instr.immediate);
    *reg1 = *reg1 + instr.immediate;
    break;

  case imull:
    *reg2 = *reg1 * *reg2;
    break;
    
  case shrl:
    unsigned int val = (unsigned int)*reg1;
    val = val >> 1;
    *reg1 = (int) val;
    break;

  case movl_reg_reg:
    *reg2 = *reg1;
    break;
  
  case movl_deref_reg:
    *reg2 = *(int *)&memory[*reg1 + instr.immediate]; // WHAT IN THE C IS THIS CAST
    break;

  case movl_reg_deref:
    *(int *)&memory[*reg2 + instr.immediate] = *reg1;
    break;

  case movl_imm_reg: 
    *reg1 = instr.immediate;    
    break;


  case cmpl:
    unsigned int u_reg2 = *reg2; 
    unsigned int u_reg1 = *reg1; 
    unsigned int u_result = u_reg2 - u_reg1;

    // CF: if unsigned borrow happened
    if (u_reg2 < u_reg1) {
        *eflags |= (1 << CF_bit_index);
    } else {
        *eflags &= ~(1 << CF_bit_index);
    }

    // ZF: reg2 - reg1 == 0
    if (u_result == 0) {
        *eflags |= (1 << ZF_bit_index);
    } else {
        *eflags &= ~(1 << ZF_bit_index);
    }

    // SF: sign bit of reg2 - reg1
    if (u_result & 0x80000000) {
        *eflags |= (1 << SF_bit_index);
    } else {
        *eflags &= ~(1 << SF_bit_index);
    }

    // OF: if signed overflow of reg2 - reg1
    int s_result = *reg2 - *reg1;

    if (((*reg2 ^ *reg1) & (*reg2 ^ s_result)) & 0x80000000) {
        *eflags |= (1 << OF_bit_index);
    } else {
        *eflags &= ~(1 << OF_bit_index);
    }
    break;

  case printr:
    printf("%d (0x%x)\n", *reg1, *reg1);
    break;
  
  case readr:
    scanf("%d", &(registers[instr.first_register]));
    break;

  case jmp:
    // printf("PC: %d\n", program_counter);
    return program_counter + instr.immediate + 4;
  
  case je:
    if (ZF){ 
      return program_counter + instr.immediate + 4;
    } 
    else {
      return program_counter + 4;
    }

  case jl:
    if (SF ^ OF){
      return program_counter + instr.immediate + 4;
    }
    else{
      return program_counter + 4;
    }

  case jle:
    if ((SF ^ OF) | ZF){
      return program_counter + instr.immediate + 4;
    }
    else{
      return program_counter + 4;
    }

  case jge:
    if (!(SF ^ OF)){ 
      return program_counter + instr.immediate + 4;
    }
    else{
      return program_counter + 4;
    }

  case jbe:
    if (CF | ZF){
      return program_counter + instr.immediate + 4;
    }
    else{
      return program_counter + 4;
    }

  case call:
    //printf("Call, program counter at %d, stack pointer at, %d\n", program_counter, *esp);
    *esp -= 4;
    //printf("Reduced SP by 4\n");
    *(int*)&memory[*esp] = program_counter + 4;
    return program_counter + instr.immediate + 4;
    
  case ret:
    if (*esp == 1024){
      exit(0);
    }
    else{
      //printf("Returned, program counter at %d, stack pointer at, %d\n", program_counter, *esp);
      program_counter = *(int*)&memory[*esp];
      *esp += 4;
      //printf("Increased SP by 4\n");
      return program_counter;

    }
    break;
  
  case pushl:
    *esp -= 4;
    *(int*)&memory[*esp] = *reg1;
    break;
  
  case popl:
    *reg1 = *(int*)&memory[*esp];
    *esp += 4;
    break;
  }

  // TODO: Do not always return program_counter + 4
  //       Some instructions jump elsewhere

  // program_counter + 4 represents the subsequent instruction
  return program_counter + 4;
}


/*********************************************/
/****  DO NOT MODIFY THE FUNCTIONS BELOW  ****/
/*********************************************/

/*
 * Returns the file size in bytes of the file referred to by the given descriptor
*/
unsigned int get_file_size(int file_descriptor)
{
  struct stat file_stat;
  fstat(file_descriptor, &file_stat);
  return file_stat.st_size;
}

/*
 * Loads the raw bytes of a file into an array of 4-byte units
*/
unsigned int* load_file(int file_descriptor, unsigned int size)
{
  unsigned int* raw_instruction_bytes = (unsigned int*)malloc(size);
  if(raw_instruction_bytes == NULL)
    error_exit("unable to allocate memory for instruction bytes (something went really wrong)");

  int num_read = read(file_descriptor, raw_instruction_bytes, size);

  if(num_read != size)
    error_exit("unable to read file (something went really wrong)");

  return raw_instruction_bytes;
}

/*
 * Prints the opcode, register IDs, and immediate of every instruction, 
 * assuming they have been decoded into the instructions array
*/
void print_instructions(instruction_t* instructions, unsigned int num_instructions)
{
  printf("instructions: \n");
  unsigned int i;
  for(i = 0; i < num_instructions; i++)
  {
    printf("op: %d, reg1: %d, reg2: %d, imm: %d\n", 
	   instructions[i].opcode,
	   instructions[i].first_register,
	   instructions[i].second_register,
	   instructions[i].immediate);
  }
  printf("--------------\n");
}

/*
 * Prints an error and then exits the program with status 1
*/
void error_exit(const char* message)
{
  printf("Error: %s\n", message);
  exit(1);
}
