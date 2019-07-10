/* Masked Broadcast from root to other nodes in the mask using binary tree algorithm */

void MP_Bcast_masked(void *vec, int nbytes, int root, int *mask)
  {
     int myvproc, nvprocs, *vproc;
     int n, b, igot = 0, source, dest;
     int msg_id;

     if( mask[myproc] != 1 ) return;

     if( mask[root] == 0 )
        errorx(root, "MP_Bcast_masked() - root node must be in the mask");

     if( nprocs == 1 ) return;

     dbprintf("\n%d in MP_Bcast_masked(%d, %d, %d, mask)\n",myproc,vec,nbytes,root);

     vproc = malloc( nprocs*sizeof(int) );
     if( ! vproc ) errorx(0,"MP_Bcast_masked() - malloc(vproc) failed");

     nvprocs = 0;
     vproc[nvprocs++] = root;
     if (myproc == root) myvproc = 0;

     for( n=0; n<nprocs; n++)
     {
        if (n != root)
        {
           if( n == myproc ) myvproc = nvprocs;
           if( mask[n] == 1 ) vproc[nvprocs++] = n;
        }
     }

     if( nvprocs <= 1 ) return;

     b = 1;
     while( b < nvprocs ) b *= 2;
     if( myproc == root ) igot = 1;

     do {
        b /= 2;
        if ( igot == 0 && myvproc%b == 0 )
        {
           source = myvproc-b;
           MP_ARecv(vec, nbytes, vproc[source], 0, &msg_id);
           igot = 1;
        }
     } while ( b != 1 );

     if (nbytes > 10000)
          ;

     igot = 0;
     b = 1;

     while( b < nvprocs ) b *= 2;
     if( myproc == root ) igot = 1;

     do {
        b /= 2;

        if( igot )
        {
           dest = myvproc+b;
           if( dest < nvprocs ) MP_Send(vec, nbytes, vproc[dest], 0);
        }
        else if ( myvproc%b == 0 )
        {
           source = myvproc-b;
           MP_Wait(&msg_id);
           igot = 1;
        }
     } while ( b != 1 );

     free( vproc );
     dbprintf("%d exiting MP_Broadcast_Global()\n",myproc);
  }

/* Broadcast from 0 to all other nodes */

void MP_Broadcast(void *ptr, int nbytes)
   { MP_Bcast_masked(ptr, nbytes, 0, MP_all_mask); }

void MP_dBroadcast(void *ptr, int count)
   { MP_Bcast_masked(ptr, count * sizeof(double), 0, MP_all_mask); }

void MP_iBroadcast(void *ptr, int count)
   { MP_Bcast_masked(ptr, count * sizeof(int), 0, MP_all_mask); }

void MP_fBroadcast(void *ptr, int count)
   { MP_Bcast_masked(ptr, count * sizeof(float), 0, MP_all_mask); }

/* Broadcast from different root */

void MP_Bcast(void *ptr, int nbytes, int root)
   { MP_Bcast_masked(ptr, nbytes, root, MP_all_mask); }

void MP_dBcast(void *ptr, int count,int root)
   { MP_Bcast_masked(ptr, count * sizeof(double), root, MP_all_mask); }

void MP_iBcast(void *ptr, int count,int root)
   { MP_Bcast_masked(ptr, count * sizeof(int), root, MP_all_mask); }

void MP_fBcast(void *ptr, int count,int root)
   { MP_Bcast_masked(ptr, count * sizeof(float), root, MP_all_mask); }

/* Masked Broadcast from different root */

void MP_dBcast_masked(void *ptr, int count,int root, int *mask)
   { MP_Bcast_masked(ptr, count * sizeof(double), root, mask); }

void MP_iBcast_masked(void *ptr, int count,int root, int *mask)
   { MP_Bcast_masked(ptr, count * sizeof(int), root, mask); }

void MP_fBcast_masked(void *ptr, int count,int root, int *mask)
   { MP_Bcast_masked(ptr, count * sizeof(float), root, mask); }

