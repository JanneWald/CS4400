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
  
  int CF_bit = 0;
  int ZF_bit = 6;
  int SF_bit = 7;
  int OF_bit = 11;
  int* eflags = &registers[16]; // Register ID of 16
  int CF = (*eflags >> (16 - CF_bit)) & 1;
  int ZF = (*eflags >> (16 - ZF_bit)) & 1;
  int SF = (*eflags >> (16 - SF_bit)) & 1;
  int OF = (*eflags >> (16 - OF_bit)) & 1;
  int* esp = &registers[6]; // Stack pointer register

  switch(instr.opcode)
  {
  case subl:
    registers[instr.first_register] = registers[instr.first_register] - instr.immediate;
    break;
  case addl_reg_reg:
    registers[instr.second_register] = registers[instr.first_register] + registers[instr.second_register];
    break;
  case addl_imm_reg:
    registers[instr.first_register] = registers[instr.first_register] + registers[instr.immediate];
    break;
  case imull:
    registers[instr.second_register] = registers[instr.first_register] * registers[instr.second_register];
    break;
    
  case shrl:
    registers[instr.first_register] = registers[instr.first_register] >> 1;
    break;

  case movl_reg_reg:
    registers[instr.second_register] = registers[instr.first_register];
    break;
  
  case movl_deref_reg:
    registers[instr.second_register] = memory[registers[instr.first_register] + registers[instr.immediate]];
    break;

  case movl_reg_deref:
    memory[registers[instr.second_register] + registers[instr.immediate]] = registers[instr.first_register];
    break;

  case movl_imm_reg: 
    int imm = instr.immediate & 0xFFFF;        // mask last 16 bits
    if (imm & 0x8000)                          // if sign bit is 1
        imm |= 0xFFFF0000;                     // fill first 16 with 1s
    registers[instr.first_register] = imm;    
    break;

  case cmpl:
    int result = registers[instr.first_register] < registers[instr.second_register];

    // CF
    if (registers[instr.first_register] < registers[instr.second_register]){
      *eflags |= (1 << CF_bit);
    }
    else{
      *eflags &= ~(1 << CF_bit);
    }
    
    // ZF
    if (result == 0){
      *eflags |= (1 << ZF_bit);
    }
    else{
      *eflags &= ~(1 << ZF_bit);
    }

    // SF
    if ((registers[instr.first_register] - registers[instr.second_register]) >> 31){
      *eflags |= (1 << SF_bit);
    }
    else{
      *eflags &= ~(1 << SF_bit);
    }
    
    // OF
    int signed_overflow = ((registers[instr.second_register] ^ registers[instr.first_register]) & (registers[instr.second_register] ^ result)) & (1 << 31);
    if(signed_overflow){
      *eflags |= (1 << OF_bit);
    }
    else{
      *eflags &= ~(1 << OF_bit);
    }
    break;

  case printr:
    printf("%d (0x%x)\n", registers[instr.first_register], registers[instr.first_register]);
    break;
  
  case readr:
    scanf("%d", &(registers[instr.first_register]));
    break;

  case jmp:
    return program_counter + registers[instr.immediate];
    break;
  
  case je:
    if (~(1 << ZF_bit) & *eflags){ // If ZF
      return program_counter + registers[instr.immediate];
    } 
    else {
      return program_counter + 4;
    }

  case jl:
    if (SF ^ OF){ // If SF ^ OF
      return program_counter + registers[instr.immediate];
    }
    else{
      return program_counter + 4;
    }

  case jle:
    if ((SF ^ OF) | ZF){ // If ZF ^ OF
      return program_counter + registers[instr.immediate];
    }
    else{
      return program_counter + 4;
    }

  case jge:
    if (!(ZF ^ OF)){ // If ZF ^ OF
      return program_counter + registers[instr.immediate];
    }
    else{
      return program_counter + 4;
    }

  case jbe:
    if (CF | ZF){ // If ZF ^ OF
      return program_counter + registers[instr.immediate];
    }
    else{
      return program_counter + 4;
    }

  case call:
    *esp -= 4;
    memory[*esp] = program_counter + 4;
    return program_counter + registers[instr.immediate];
    
  case ret:
    if (*esp == 1024){
      exit(0);
    }
    else{
      program_counter = memory[*esp];
      esp += 4;
    }
    break;
  
  case pushl:
    *esp -= 4;
    memory[*esp] = registers[instr.first_register];
    break;
  
  case popl:
    registers[instr.first_register] = memory[*esp];
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
