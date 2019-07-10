#include "globals.h"
#include "mplite.h"

int main()
{
   int i, myproc = 0;
   int nx3 = 8, ny3 = 4, nz3 = 2;
   int nx2 = 8, ny2 = 8;

   nprocs = nx3 * ny3 * nz3;

   MP_Remap_To_Torus( nx3, ny3, nz3 );

   MP_Remap_Setup(nx2, ny2, &myproc);

   for( i=0; i<nprocs; i++) {
      myproc = MP_Remap( i );
      fprintf(stderr, "%d  --> %d\n", i, myproc);
   }

}



