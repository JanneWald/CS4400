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

/*
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
*/

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
  for (int j = 0; j < dim; j++) { // outer loop on COLUMNS for sequential writes
    int dest_row = dim - j - 1; // used in every unroll iteration

    for (int i = 0; i < dim; i += 8) { // inner loop unrolled by 8, too high is slow.
      // When in doubt, unroll it out! HAHAHAHAHAHHAHA im going joker mode copy and pasting these.
      // Unroll 1
      int dest_col_base = dim - i - 1;  // base col for this block

      // Unroll 1
      int idx = RIDX(i + 0, j, dim);
      int dest_idx = RIDX(dest_row, dest_col_base - 0, dim);
      int sum = src[idx].red + src[idx].green + src[idx].blue;
      int gray = sum / 3;
      dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

      // Unroll 2
      idx = RIDX(i + 1, j, dim);
      dest_idx = RIDX(dest_row, dest_col_base - 1, dim);
      sum = src[idx].red + src[idx].green + src[idx].blue;
      gray = sum / 3;
      dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

      // Unroll 3
      idx = RIDX(i + 2, j, dim);
      dest_idx = RIDX(dest_row, dest_col_base - 2, dim);
      sum = src[idx].red + src[idx].green + src[idx].blue;
      gray = sum / 3;
      dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

      // Unroll 4
      idx = RIDX(i + 3, j, dim);
      dest_idx = RIDX(dest_row, dest_col_base - 3, dim);
      sum = src[idx].red + src[idx].green + src[idx].blue;
      gray = sum / 3;
      dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

      // Unroll 5
      idx = RIDX(i + 4, j, dim);
      dest_idx = RIDX(dest_row, dest_col_base - 4, dim);
      sum = src[idx].red + src[idx].green + src[idx].blue;
      gray = sum / 3;
      dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

      // Unroll 6
      idx = RIDX(i + 5, j, dim);
      dest_idx = RIDX(dest_row, dest_col_base - 5, dim);
      sum = src[idx].red + src[idx].green + src[idx].blue;
      gray = sum / 3;
      dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

      // Unroll 7
      idx = RIDX(i + 6, j, dim);
      dest_idx = RIDX(dest_row, dest_col_base - 6, dim);
      sum = src[idx].red + src[idx].green + src[idx].blue;
      gray = sum / 3;
      dest[dest_idx].red = dest[dest_idx].green = dest[dest_idx].blue = gray;

      // Unroll 8
      idx = RIDX(i + 7, j, dim);
      dest_idx = RIDX(dest_row, dest_col_base - 7, dim);
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
  //add_complex_function(&complex_complex, complex_complex_descr);
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
    for(jj=0; jj < 3; jj++)  // unroll into 9, 
      if ((i + ii < dim) && (j + jj < dim)) 
      {
        num_neighbors++;
        red += (int) src[RIDX(i+ii,j+jj,dim)].red; // Precompute RIDX(i+11, j+11, dim), or save pixel locally
        green += (int) src[RIDX(i+ii,j+jj,dim)].green;
        blue += (int) src[RIDX(i+ii,j+jj,dim)].blue;
      }
  
  current_pixel.red = (unsigned short) (red / num_neighbors);
  current_pixel.green = (unsigned short) (green / num_neighbors);
  current_pixel.blue = (unsigned short) (blue / num_neighbors);
  
  return current_pixel;
}


/*
char inline_motion_descr[] = "motion: inlined weighted_combo";
// 1.1 speedup
void inline_motion(int dim, pixel *src, pixel *dst) 
{
  for (int i = 0; i < dim; i++) {
    for (int j = 0; j < dim; j++) {
      int red = 0, green = 0, blue = 0;
      int num_neighbors = 0;


      // Inlining should stop dim^2 func calls
      for (int ii = 0; ii < 3; ii++) {
        for (int jj = 0; jj < 3; jj++) {

          if ((i + ii < dim) && (j + jj < dim)) {
              int idx = RIDX(i + ii, j + jj, dim);
              pixel sp = src[idx];
              red += sp.red;
              green += sp.green;
              blue += sp.blue;
              num_neighbors++;
            }
          }
      }

      int dest_idx = RIDX(i, j, dim);
      dst[dest_idx].red   = red   / num_neighbors;
      dst[dest_idx].green = green / num_neighbors;
      dst[dest_idx].blue  = blue  / num_neighbors;
    }
  }
}
*/

/*
void split_inner_helper(int dim, int i, int j, pixel *src, pixel *dst){
  int red = 0, green = 0, blue = 0;

  int base = RIDX(i, j, dim); // &pixel(i,j)
  pixel p1 = src[base];
  pixel p2 = src[base + 1];
  pixel p3 = src[base + 2];

  int base2 = base + dim; // &pixel(2i,j)
  pixel p4 = src[base2];
  pixel p5 = src[base2 + 1];
  pixel p6 = src[base2 + 2];

  int base3 = base + 2*dim; // &pixel(3i,j)
  pixel p7 = src[base3];
  pixel p8 = src[base3 + 1];
  pixel p9 = src[base3 + 2];

  red = p1.red + p2.red + p3.red + p4.red + p5.red + p6.red + p7.red + p8.red + p9.red;
  green = p1.green + p2.green + p3.green + p4.green + p5.green + p6.green + p7.green + p8.green + p9.green;
  blue = p1.blue + p2.blue + p3.blue + p4.blue + p5.blue + p6.blue + p7.blue + p8.blue + p9.blue;

  int dest_idx = RIDX(i, j, dim);
  dst[dest_idx].red = red / 9;
  dst[dest_idx].green = green / 9;
  dst[dest_idx].blue = blue / 9;
}
*/

/*
Helper function to average "surrounding" pixels near a border where pixels are not guarenteed to be there

Now branches are contained on dim * 4 pixels 
*/
void split_border_helper(int dim, int i, int j, pixel *src, pixel *dst){
  int red = 0, green = 0, blue = 0, neighbors = 0;
  for (int ii = 0; ii < 3; ii++) {
    for (int jj = 0; jj < 3; jj++) {
      if ((i + ii < dim) && (j + jj < dim)) { // Edge check
        pixel sp = src[RIDX(i + ii, j + jj, dim)]; // Cache pixel
        red += sp.red;
        green += sp.green;
        blue += sp.blue;
        neighbors++;
      }
    }
  }

  int dest_idx = RIDX(i, j, dim); // Precompute
  dst[dest_idx].red = red / neighbors;
  dst[dest_idx].green = green / neighbors;
  dst[dest_idx].blue = blue / neighbors;
}

char split_motion_descr[] = "motion: unroll inner vs border";
void split_motion(int dim, pixel *src, pixel *dst) 
{
  int i, j;
  
  // Optimized/unrolled 3x3 pixel area averaging in most of the picture that isnt near a border
  for (i = 0; i < dim - 2; i++) {
    for (j = 0; j < dim - 2; j++) {
      //split_inner_helper(dim, i, j, src, dst); // 1.8 speedup from 3.2 :( 
      int red = 0, green = 0, blue = 0;

      int base = RIDX(i, j, dim); // &pixel(i,j)
      pixel p1 = src[base];
      pixel p2 = src[base + 1];
      pixel p3 = src[base + 2];

      int base2 = base + dim; // &pixel(2i,j)
      pixel p4 = src[base2];
      pixel p5 = src[base2 + 1];
      pixel p6 = src[base2 + 2];

      int base3 = base + 2*dim; // &pixel(3i,j)
      pixel p7 = src[base3];
      pixel p8 = src[base3 + 1];
      pixel p9 = src[base3 + 2];

      red = p1.red + p2.red + p3.red + p4.red + p5.red + p6.red + p7.red + p8.red + p9.red;
      green = p1.green + p2.green + p3.green + p4.green + p5.green + p6.green + p7.green + p8.green + p9.green;
      blue = p1.blue + p2.blue + p3.blue + p4.blue + p5.blue + p6.blue + p7.blue + p8.blue + p9.blue;

      int dest_idx = RIDX(i, j, dim);
      dst[dest_idx].red = red / 9;
      dst[dest_idx].green = green / 9;
      dst[dest_idx].blue = blue / 9;
    }
  }

  //// Border averaging:
  // Right edge
  for (i = 0; i < dim; i++) {
    for (j = dim - 2; j < dim; j++) { 
      if (j < 0 || j >= dim) continue;
      split_border_helper(dim, i, j, src, dst); // not as bad that its not inline, only a small amount of func calls
    }
  }
  // Bottom edge
  for (i = dim - 2; i < dim; i++) {
    for (j = 0; j < dim - 2; j++) {
      if (i < 0 || i >= dim) continue;
      split_border_helper(dim, i, j, src, dst);
    }
  }
}

/*
 * naive_motion - The naive baseline version of motion 
 */
char naive_motion_descr[] = "naive_motion: Naive baseline implementation";
void naive_motion(int dim, pixel *src, pixel *dst) 
{
  int i, j;
    
  for (i = 0; i < dim; i++)
    for (j = 0; j < dim; j++)
      dst[RIDX(i, j, dim)] = weighted_combo(dim, i, j, src); // Func calls are expensive, inline!
}


/*
 * motion - Your current working version of motion. 
 * IMPORTANT: This is the version you will be graded on
 */
char motion_descr[] = "motion: Current working version";
void motion(int dim, pixel *src, pixel *dst) 
{
  split_motion(dim, src, dst);
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
//  add_motion_function(&inline_motion, inline_motion_descr);
  add_motion_function(&naive_motion, naive_motion_descr);
}
