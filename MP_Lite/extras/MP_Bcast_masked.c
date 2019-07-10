void MP_Bcast_masked(void *vec, int nbytes, int root, int *mask)
{
   int myvproc, nvprocs, *vproc;
   int n, b, igot = 0, source, dest, bmod;

   if( nprocs == 1 || mask[myproc] != 1 ) return;
   dbprintf("\n%d in MP_Bcast_masked(%d, %d, %d, mask)\n",myproc,vec,n,root);

   if( mask[root] == 0 ) 
      errorx(root, "MP_Bcast_masked() - root node must be in the mask");

   vproc = malloc( nprocs*sizeof(int) );
   if( ! vproc ) errorx(0,"MP_Bcast_masked() - malloc(vproc) failed");

   nvprocs = 0;
   for( n=0; n<nprocs; n++) {
      if( n == myproc ) myvproc = nvprocs;
      if( mask[n] == 1 ) vproc[nvprocs++] = n;
   }

   if( nvprocs <= 1 ) return;

   b = 1;
   while( b < nvprocs ) b *= 2;

   if( myproc == root ) igot = 1;
   bmod = root;

   do {
      b /= 2;
      bmod = bmod % b;

      if( igot ) 
      {
         dest = (myvproc+b)%(2*b);
         if( dest < nvprocs ) MP_Send(vec, nbytes, vproc[dest], 0);
      }
      else if ( myvproc%b == bmod )  
      {
         source = (myvproc+b)%(2*b);
         MP_Recv(vec, nbytes, vproc[source], 0);
         igot = 1;
      }
   } while ( b != 1 );

   free( vproc );
   dbprintf("%d exiting MP_Broadcast_Global()\n",myproc);
}
