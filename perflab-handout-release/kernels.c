/*******************************************
 * Solutions for the CS:APP Performance Lab
 ********************************************/

#include <stdio.h>
#include <stdlib.h>
#include "defs.h"

/* 
 * Please fill in the following student struct 
 */
student_t student = {
  "Janne Wald",     /* Full name */
  "janne.wald@utah.edu",  /* Email address */
};

/***************
 * COMPLEX KERNEL
 ***************/

char complex_complex_descr[] = "complex: optimized scalar with reverse ordering";

void complex_complex(int dim, pixel *src, pixel *dest)
{
    for (int i = 0; i < dim; i++) {
        int dest_i = dim - i - 1;  // reused in dest column calculation

        for (int j = 0; j < dim; j += 4) {

            // Unroll 4 iterations of inner loop
            for (int u = 0; u < 4 && (j + u) < dim; u++) {
                int jj = j + u;

                int src_idx  = RIDX(i, jj, dim);
                int dest_idx = RIDX(dim - jj - 1, dest_i, dim);

                pixel sp = src[src_idx];

                // grayscale = (r+g+b)/3 but avoid slow integer division
                // multiply by reciprocal (2^16/3 ≈ 21845), shift right
                int sum = sp.red + sp.green + sp.blue;
                int gray = sum / 3;

                dest[dest_idx].red = gray;
                dest[dest_idx].green = gray;
                dest[dest_idx].blue = gray;
            }
        }
    }
}

/*
char unroll_32_complex_descr[] = "complex: optimized with pragma 32 unroll";
void unroll_32_complex(int dim, pixel *src, pixel *dest)
{
    for (int i = 0; i < dim; i++) {
        int dest_i = dim - i - 1;  // reused in dest column calculation

        for (int j = 0; j < dim; j += 4) {

          
          for (int u = 0; u < 32; u++) {
              int jj = j + u;
              int src_idx  = RIDX(i, jj, dim);
              int dest_idx = RIDX(dim - jj - 1, dest_i, dim);
              pixel sp = src[src_idx];
              int sum = sp.red + sp.green + sp.blue;
              int gray = sum / 3;
              dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;
          }
        }
    }
}
*/

char man_unroll_8_complex_descr[] = "complex: row-major write + manual 8 unroll";

void man_unroll_8_complex(int dim, pixel *src, pixel *dest)
{
    for (int j = 0; j < dim; j++) {           // outer loop on columns for sequential writes
        int dest_row = dim - j - 1;           // precompute dest row start

        for (int i = 0; i < dim; i += 8) {   // inner loop unrolled by 8
            // Unroll 1
            int idx = RIDX(i + 0, j, dim);
            int dest_idx = dest_row * dim + (dim - (i + 0) - 1);
            int sum = src[idx].red + src[idx].green + src[idx].blue;
            int gray = sum / 3;
            dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

            // Unroll 2
            idx = RIDX(i + 1, j, dim);
            dest_idx = dest_row * dim + (dim - (i + 1) - 1);
            sum = src[idx].red + src[idx].green + src[idx].blue;
            gray = sum / 3;
            dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

            // Unroll 3
            idx = RIDX(i + 2, j, dim);
            dest_idx = dest_row * dim + (dim - (i + 2) - 1);
            sum = src[idx].red + src[idx].green + src[idx].blue;
            gray = sum / 3;
            dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

            // Unroll 4
            idx = RIDX(i + 3, j, dim);
            dest_idx = dest_row * dim + (dim - (i + 3) - 1);
            sum = src[idx].red + src[idx].green + src[idx].blue;
            gray = sum / 3;
            dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

            // Unroll 5
            idx = RIDX(i + 4, j, dim);
            dest_idx = dest_row * dim + (dim - (i + 4) - 1);
            sum = src[idx].red + src[idx].green + src[idx].blue;
            gray = sum / 3;
            dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

            // Unroll 6
            idx = RIDX(i + 5, j, dim);
            dest_idx = dest_row * dim + (dim - (i + 5) - 1);
            sum = src[idx].red + src[idx].green + src[idx].blue;
            gray = sum / 3;
            dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

            // Unroll 7
            idx = RIDX(i + 6, j, dim);
            dest_idx = dest_row * dim + (dim - (i + 6) - 1);
            sum = src[idx].red + src[idx].green + src[idx].blue;
            gray = sum / 3;
            dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

            // Unroll 8
            idx = RIDX(i + 7, j, dim);
            dest_idx = dest_row * dim + (dim - (i + 7) - 1);
            sum = src[idx].red + src[idx].green + src[idx].blue;
            gray = sum / 3;
            dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;
        }
    }
}

/*
char man_unroll_4_complex_descr[] = "complex: w/o pragma, manual 4 unroll ";
void man_unroll_4_complex(int dim, pixel *src, pixel *dest)
{
    for (int i = 0; i < dim; i++) {
        int dest_i = dim - i - 1;  // reused in dest column calculation

        for (int j = 0; j < dim; j += 4) {
          int jj, src_idx, dest_idx, sum, gray;
          pixel sp;

          // Unroll 1
          jj = j + 0;
          src_idx  = RIDX(i, jj, dim);
          dest_idx = RIDX(dim - jj - 1, dest_i, dim);
          sp = src[src_idx];
          sum = sp.red + sp.green + sp.blue;
          gray = sum / 3;
          dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;
        
          // Unroll 2
          jj = j + 1;
          src_idx  = RIDX(i, jj, dim);
          dest_idx = RIDX(dim - jj - 1, dest_i, dim);
          sp = src[src_idx];
          sum = sp.red + sp.green + sp.blue;
          gray = sum / 3;
          dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;
        
          // Unroll 3
          jj = j + 2;
          src_idx  = RIDX(i, jj, dim);
          dest_idx = RIDX(dim - jj - 1, dest_i, dim);
          sp = src[src_idx];
          sum = sp.red + sp.green + sp.blue;
          gray = sum / 3;
          dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;
        
          // Unroll 4
          jj = j + 3;
          src_idx  = RIDX(i, jj, dim);
          dest_idx = RIDX(dim - jj - 1, dest_i, dim);
          sp = src[src_idx];
          sum = sp.red + sp.green + sp.blue;
          gray = sum / 3;
          dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;
        }
    }
}
*/

/* 
 * naive_complex - The naive baseline version of complex 
 */
char naive_complex_descr[] = "naive_complex: Naive baseline implementation";
void naive_complex(int dim, pixel *src, pixel *dest)
{
  int i, j;

  for(i = 0; i < dim; i++)
    for(j = 0; j < dim; j++)
    {

      dest[RIDX(dim - j - 1, dim - i - 1, dim)].red = 
        ((int)src[RIDX(i, j, dim)].red +
         (int)src[RIDX(i, j, dim)].green +
         (int)src[RIDX(i, j, dim)].blue) / 3;
      
      dest[RIDX(dim - j - 1, dim - i - 1, dim)].green = 
        ((int)src[RIDX(i, j, dim)].red +
         (int)src[RIDX(i, j, dim)].green +
         (int)src[RIDX(i, j, dim)].blue) / 3;
      
      dest[RIDX(dim - j - 1, dim - i - 1, dim)].blue = 
        ((int)src[RIDX(i, j, dim)].red +
         (int)src[RIDX(i, j, dim)].green +
         (int)src[RIDX(i, j, dim)].blue) / 3;
    }
}

/* 
 * complex - Your current working version of complex
 * IMPORTANT: This is the version you will be graded on
 */
char complex_descr[] = "complex: Current working version";
void complex(int dim, pixel *src, pixel *dest)
{
  man_unroll_8_complex(dim, src, dest);
}

/*********************************************************************
 * register_complex_functions - Register all of your different versions
 *     of the complex kernel with the driver by calling the
 *     add_complex_function() for each test function. When you run the
 *     driver program, it will test and report the performance of each
 *     registered test function.  
 *********************************************************************/

void register_complex_functions() {
  add_complex_function(&complex, complex_descr);
  //add_complex_function(&unroll_32_complex, unroll_32_complex_descr);
  //add_complex_function(&man_unroll_4_complex, man_unroll_4_complex);
  add_complex_function(&complex_complex, complex_complex_descr);
  add_complex_function(&naive_complex, naive_complex_descr);
}

/***************
 * MOTION KERNEL
 **************/

/***************************************************************
 * Various helper functions for the motion kernel
 * You may modify these or add new ones any way you like.
 **************************************************************/


/* 
 * weighted_combo - Returns new pixel value at (i,j) 
 */
static pixel weighted_combo(int dim, int i, int j, pixel *src) 
{
  int ii, jj;
  pixel current_pixel;

  int red, green, blue;
  red = green = blue = 0;

  int num_neighbors = 0;
  for(ii=0; ii < 3; ii++)
    for(jj=0; jj < 3; jj++) 
      if ((i + ii < dim) && (j + jj < dim)) 
      {
	num_neighbors++;
	red += (int) src[RIDX(i+ii,j+jj,dim)].red;
	green += (int) src[RIDX(i+ii,j+jj,dim)].green;
	blue += (int) src[RIDX(i+ii,j+jj,dim)].blue;
      }
  
  current_pixel.red = (unsigned short) (red / num_neighbors);
  current_pixel.green = (unsigned short) (green / num_neighbors);
  current_pixel.blue = (unsigned short) (blue / num_neighbors);
  
  return current_pixel;
}



/******************************************************
 * Your different versions of the motion kernel go here
 ******************************************************/


/*
 * naive_motion - The naive baseline version of motion 
 */
char naive_motion_descr[] = "naive_motion: Naive baseline implementation";
void naive_motion(int dim, pixel *src, pixel *dst) 
{
  int i, j;
    
  for (i = 0; i < dim; i++)
    for (j = 0; j < dim; j++)
      dst[RIDX(i, j, dim)] = weighted_combo(dim, i, j, src);
}


/*
 * motion - Your current working version of motion. 
 * IMPORTANT: This is the version you will be graded on
 */
char motion_descr[] = "motion: Current working version";
void motion(int dim, pixel *src, pixel *dst) 
{
  naive_motion(dim, src, dst);
}

/********************************************************************* 
 * register_motion_functions - Register all of your different versions
 *     of the motion kernel with the driver by calling the
 *     add_motion_function() for each test function.  When you run the
 *     driver program, it will test and report the performance of each
 *     registered test function.  
 *********************************************************************/

void register_motion_functions() {
  add_motion_function(&motion, motion_descr);
  add_motion_function(&naive_motion, naive_motion_descr);
}
