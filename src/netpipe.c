//////////////////////////////////////////////////////////////////////////////
// "NetPIPE" -- Network Protocol Independent Performance Evaluator.
// Copyright 1997, 1998 Iowa State University Research Foundation, Inc.
// NetPIPE is free software distrubuted under the GPL license.
//
// This code was originally developed by Quinn Snell, Armin Mikler,
// John Gustafson, and Guy Helmer at Ames Laboratory. (original core, MPI, TCP)
//
// Further developed by Dave Turner at Ames Lab 2000-2005 with many contributions
// from ISU students (Bogdan Vasiliu, Adam Oline, Xuehua Chen, Brian Smith).
// (many of the low level modules, complete rewrite of the core)
// Maintained for a while by Troy Benjegerdes 2005 - ~2008 (version 3.7.2).
//
// Currently being developed Fall 2014- by Dave Turner at Kansas State University
// (total rewrite of core and modules plus IO, aggregate, ibverbs modules)
// Email: DrDaveTurner@gmail.com
//////////////////////////////////////////////////////////////////////////////
// TODO: workload measurements are still being calibrated
// TODO: Add a hardware clock to test each ping-pong and plot variance???
// TODO: Test with other systems like IBM

// The default cache mode uses a double buffer for each process.  This
// shows better performance when data gets bounced between cache on
// SMP nodes. Running without cache effects (--nocache) uses separate send and 
// recv buffers each time as a moving window through a very large memory buffer 
// ( > cache size ).  This guarantees that data starts in main memory.
// Both these cases are needed to understand the SMP performance
// since both cases occur in applications while other modules may not be affected.
//
// The bidirectional mode also uses double buffers.  Both procs send data
// simultaneously then receive it before sending it out again from
// the recv buffer.  This double buffering is needed to guarantee 
// that data does not overwrite the previous message before it is read.
//
// Aggregate measurements using multiple pairs of processes uses a sync before 
// proc 0 starts the clock and another before proc 0 stops the clock.  This adds 
// a little latency which doesn't matter but allows all timing to be done on proc 0.
// mpi.c also has a custom Sync() function to replace MPI_Barrier().


   // Pull in global variables from netpipe.h

#define _GLOBAL_VAR_
#include "netpipe.h"

double *failures;            // Globals for integrity checks
int    integrityCheck = 0, nfails;


int main(int argc, char *argv[])
{
   FILE       *out = NULL;    // Output data file
   char       *arg, *opt, *opt2, *worktype = NULL, workstring[100], *outfile, myhost[100];
   int        *memcache=NULL;  // Very large buffer used to flush cache
   int         npairs;         // nprocs/2 or 1 for disk, memcpy, theo

   int         i, j, n, nq,    // Loop indices and increments
               nrepeat,        // Number of ping-pongs per trial
               len,            // Number of bytes to be transmitted
               inc=0,          // Main loop increment value
               pert,           // Main loop perturbation value (3)
               finished=0,     // Set to 1 when main loop is finished
               factors_of_2=0, // Flag to set for testing factors of 2 only
               worksize=0,     // Data size for workload measurement
               workcount=0;    // Number of tests calibrated to ~0.1 usec
  
   double      t, t0, t1,      // Time variables
               tlast,          // Used to calc next nrepeat
              *times,          // Array for gathering times from other pairs
               avg_latency=0,  // Latency averaged over the first 10 tests
               max_bandwidth=0,// Average of 10 highest bandwidth tests
               runtime=0.25,   // Goal time for each trial
               work;           // Amount of work done (bytes moved or Flops)

   double      min_time, max_time, avg_time;
   double      min_gbps, avg_gbps, max_gbps;  // Gigabits per second for each trial
   int         bytes;          // number of bytes in the message
   double      tcal;


      // Initialize vars that may change from default due to input arguments

   nprocs = myproc = transmitter = mypair = source = finished = disk = 0;
   bidir = stream = rwstream = workload = burst = async = soffset = roffset = 0;
   cache = 1;              // Default is to test with cache effects
   perturbation = 3;
   nrepeat_const = 0;
   ntrials = 7;
   start= 1;               // Starting value for the message size tests
   end = 10000000;         // Max meessage size
   stoptime = 10.0;         // Stop if a trial exceeds 1.0 seconds
   est_latency = 10.0e-6;  // Estimated latency may be set in each module
                           // This is just used to calc initial nrepeat

   outfile = strdup( "np.out" );
   module = strdup( "" );  // Initialize to nothing, set in Module_Init() if needed

   Module_Init( &argc, &argv);      // Let modules initialize variables as needed

      // Set the first half to be transmitters

   if( myproc < nprocs/2 || nprocs == 1 || 
       !strcmp( module, "memcpy") || !strcmp( module, "disk") ) {
      transmitter = 1;
   } else {
      transmitter = 0;
   }
   mypair = source = nprocs - 1 - myproc;

      // Parse the arguments. Run NetPIPE with --help for a full description.

   for( i = 1; i < argc; i++ ) {  // Loop through all input arguments to NetPIPE

      arg = argv[i];
      while( arg[0] == '-' ) arg++;   // Get rid of leading -'s

      //opt = "";
      //opt2 = "";
      opt = NULL;
      opt2 = NULL;
      if( i+1 != argc && argv[i+1][0] != '-' ) {
         opt = argv[i+1];
         i++;
      }
      if( i+1 != argc && argv[i+1][0] != '-' ) {
         opt2 = argv[i+1];
         i++;
      }

      if( !strcmp( arg, "help") || !strcmp( arg, "h") ) {
         PrintUsage( NULL );

      } else if( !strcmp( arg, "repeats") || !strcmp( arg, "repeat") ) {
         nrepeat_const = atoi(opt);
         mprintf("Using a constant number of %d transmissions\n", nrepeat_const);
         mprintf("NOTE: Be leary of timings that are close to the clock accuracy.\n");

      } else if( !strcmp( arg, "workload") ) {
         workload = 1;
         if( !opt )  worktype = strdup( "matrix" );  // Assume matrix unless specified
         else        worktype = strdup( opt );
         worksize = 100;                             // Default is for matrices 100x100
         if(      !opt2 && !strcmp( worktype, "daxpy")  ) worksize = 1000;
         else if( !opt2 && !strcmp( worktype, "memcpy") ) worksize = 1000;
         else if( opt2 )                                  worksize = atoi( opt2 );
         async = 1;
         mprintf("Measuring the CPU workload using type %s size %d\n", 
               worktype, worksize);
         if( !async ) mprintf("Setting --async which is required to measure workload\n");
         mprintf("The workload measurement may affect the communication performance.\n");
         start = 1048576;
         mprintf("Starting measurements at %d Bytes\n", start );

      } else if( !strcmp( arg, "nocache") || !strcmp( arg, "inval") ) {
         cache = 0;
         mprintf("Pass data from a different memory buffer each time.\n");
         mprintf("The data will always come from main memory\n");

      } else if( !strcmp( arg, "cache") ) {     // This is the default anyway
         cache = 1;
         mprintf("Test using cache effects.  Use the same data buffers each time\n");

      } else if( !strcmp( arg, "burst") ) {  // Prepost all ARecv's
         burst = 1;
         async = 1;
         mprintf("Preposting all receives before a timed run\n");

      } else if( !strcmp( arg, "soffset") || !strcmp( arg, "send_offset") ) {
         ERRCHECK( !opt, "No send offset value given");
         soffset = atoi(opt);
         mprintf("Offsetting send buffer by %d from page aligned\n", soffset);

      } else if( !strcmp( arg, "roffset") || !strcmp( arg, "recv_offset") ) {
         ERRCHECK( !opt, "No recv offset value given");
         roffset = atoi(opt);
         mprintf("Offsetting recv buffer by %d from page aligned\n", roffset);

      } else if( !strcmp( arg, "perturbation") || !strcmp( arg, "pert") ) {
         ERRCHECK( !opt, "No perturbation value given");
         perturbation = atoi(opt);

      } else if( !strcmp( arg, "factorsof2") || !strcmp( arg, "fac2") ) {
         factors_of_2 = 1;
         perturbation = 0;
         mprintf("Only testing factors of 2 for comparison with other benchmarks\n");

      } else if( !strcmp( arg, "quick") ) {
         ntrials = 3;
         runtime = 0.10;
         perturbation = 0;

      } else if( !strcmp( arg, "quicker") ) {
         ntrials = 1;
         runtime = 0.10;
         perturbation = 0;

      } else if( !strcmp( arg, "quickest") ) { // 1 repeat 1 trial
         ntrials = 1;
         runtime = 0.10;
         perturbation = 0;
         if( nrepeat_const == 0 ) nrepeat_const = 1;

      } else if( !strcmp( arg, "outfile") || !strcmp( arg, "o") ) {
         ERRCHECK( !opt, "No output file name given");
         outfile = strdup(opt);
         mprintf("Saving output to %s\n\n", outfile);

      } else if( !strcmp( arg, "stream") ) {
         stream = 1;
         ERRCHECK( bidir, "You cannot use stream and bidirection together");
         mprintf("Streaming data in one direction\n");
         mprintf("    Streaming does not necessarily provide an accurate measurement\n");
         mprintf("    of the latency since small messages may get bundled together.\n");

      } else if( !strcmp( arg, "start") ) {    // lower bound for the message size
         ERRCHECK( !opt, "No lower bound given");
         start = atoi(opt);
         ERRCHECK( start < 1 || start > end, 
            "Lower bound %d must be positive and less than upper %d", start, end);

      } else if( !strcmp( arg, "end") ) {    // upper bound for the message size
         ERRCHECK( !opt, "No upper bound given");
         end = atoi(opt);
         ERRCHECK( end < start, "end %d must greater than start %d", end, start);

      } else if( !strcmp( arg, "bidirectional") || !strcmp( arg, "bidir")  ) {
         bidir = 1;
         transmitter = 1;    // Both procs are transmitters
         ERRCHECK( stream, "You cannot use stream and bidirection together");
         mprintf("Bidirectional mode using %d pairs of processes.\n", nprocs/2);
         mprintf("The output shows the combined throughput.\n");

      } else if( !strcmp( arg, "integrity") ) {
         integrityCheck = 1;
         ntrials = 1;
         mprintf("Doing a message integrity check instead of measuring performance\n");

      } else if( !strcmp( arg, "printhostnames") ) {

         Sync();
         gethostname( myhost, 100 );
         printf("Proc %d is on host %s\n", myproc, myhost);
         Sync();
         mprintf("\n");

      } else {
         Module_ArgOpt( arg, opt );  // Send others to the modules
      }
   }

   Module_Setup();     // Handshake and set global variables

   times    = malloc( nprocs * sizeof(double) );
   failures = malloc( nprocs * sizeof(double) );

   if( !strcmp( module, "memcpy" ) || !strcmp( module, "disk") ) { 
      npairs = nprocs;
   } else if( nprocs == 1 ) {
      npairs = 1;
   } else {
      npairs = nprocs/2;
   }

   ERRCHECK(integrityCheck && burst, "Integrity check is not supported with burst mode");

   if( nprocs > 2 && bidir == 0 && strcmp( module, "memcpy") && strcmp(module,"disk") ) {
      mprintf("\nMultiple overlapping unidirectional communications may become out \n");
      mprintf("of sync.  I recommend bidirectional communications here.\n\n");
   }

   if( myproc == 0 ) {   // Open the output file np.out
      out = fopen(outfile, "w");
      ERRCHECK( out==NULL, "Can't open %s for output", outfile);
   }

      // Very rough estimate for the clock accuracy

   t0 = myclock(); t0 = myclock(); t0 = myclock();
   t1 = myclock();
   mprintf("\n      Clock resolution ~ %s", timestring( clock_res ) );
   mprintf("      Clock accuracy ~ %s\n\n", timestring(t1-t0) );

   tlast = est_latency;   // Estimated latency used to calc initial nrepeat

   if( workload ) { // Initialize then calibrate the CPU workload rate to 0.1 usec

      workcount = 10;  // Initial guess
      DoWork( worktype, worksize, workcount, 1 );  // Initialize and pull into cache

            // Do the idle while loop in DoWork() 1000 times

      t0 = myclock();

      work = DoWork( worktype, worksize, workcount, 1000 );

      tcal = (myclock()-t0)  / 1000;  // Average time in secs
      mprintf("First cal at workcount %d got %lf GFlops in %lf usecs\n", 
            workcount, work * 1e-9 / (1000 * tcal), tcal*1e6);

         // Scale down to ~100.0 usec test times

      workcount = MAX( 0.5 + workcount * 100e-6 / tcal, 1 );

            // Do the idle while loop in DoWork() a final 1000 times

      t0 = myclock();

      work = DoWork( worktype, worksize, workcount, 1000 );

      tcal = (myclock()-t0) / 1000;  // Average time in secs
      mprintf("Final check at workcount %d is %lf usecs\n", workcount, tcal*1e6);

      work = work * 1e-9 / (1000 * tcal);      // GB/sec moved or GFlops calculated
      if( !strcmp( worktype, "memcpy" ) ) {
         mprintf("memcpy moved " RED "%6.2lf GB/sec" RESET " in " CYAN "%6.2lf usecs" 
                 RESET " when the comm system was idle\n\n", work, tcal*1e6);
      } else {
         mprintf("%s calc at " RED "%6.2lf GFlops" RESET " took " CYAN "%8.3lf usecs" 
                 RESET " for an idle comm system\n\n", worktype, work, tcal*1e6);
      }
   }

      // Do setup for nocache mode, using two distinct very large buffers

   if( ! cache ) {

          // Allocate dummy pool of memory to flush cache with

      memcache = (int *) malloc( MEMSIZE );
      ERRCHECK( memcache == NULL, "Error mallocing memcache");
      bzero( memcache, MEMSIZE);

          // Allocate large memory pools

       MallocBufs( MEMSIZE ); 
   }

      // Set a starting value for the message size increment

   inc = (start > 1) ? start / 2 : 1;
   nq  = (start > 1) ? 1 : 0;

       ////////////////////////////
       // Main loop of benchmark //
       ////////////////////////////

   mprintf("Start testing with %d trials for each message size\n", ntrials);

   n = 0;
   len = start;

   while( ! finished ) {
      
      if( factors_of_2 ) inc = len;              // Double the test size each time
      else if (nq > 2)   inc = ((nq % 2))? inc + inc: inc; // Exponential with perts
       
          // This is a perturbation loop to test nearby values

      for (pert = ((perturbation > 0) && (inc > perturbation+1)) ? -perturbation : 0;
           pert <= perturbation; 
           pert += ((perturbation > 0) && (inc > perturbation+1)) 
                        ? perturbation : perturbation+1)
      {
         n++;

         buflen = len + pert;

            // Calculate how many times to repeat the experiment

         if( disk > 1 ) {
            nrepeat = disk;                // Copy the file 'disk' times per trial
         } else if( nrepeat_const ) {
            nrepeat = nrepeat_const;
         } else if( disk ) {
            nrepeat = 100000000 / buflen;   // Sliding but consistent scale for disk IO
            if( nrepeat < 3 ) nrepeat = 3;
         } else {
            nrepeat = MAX( runtime / tlast, 3);
         }

         Broadcast(&nrepeat);

         ERRCHECK( nrepeat <= 0 || nrepeat > 1000000000, 
                   "ERROR: nrepeat = %u is out of range (tlast = %lf)", nrepeat, tlast);

         mprintf("%3d: %s %9u times -->  ", n, bytestring(buflen), nrepeat);
         fflush( stdout );

         if( disk > 1 ) {    // cp or scp files

            MallocBufs( buflen );

         } else if( cache ) {  // Allocate the send and recv double buffers
                        // with page alignment and room for offsets

            MallocBufs( 2*buflen );

         } else {    // Eliminate cache effects using 2 very large distinct buffers.
                     // Pointers will walk through the large buffers in main memory
                     // Set at 200 MB >> sum of all cache memory in the computer
 
            InitBufferData( MEMSIZE );
         }

         min_time = LONGTIME;
         max_time = avg_time = 0;
         work = 0.0;  // Sum of work returned from DoWork()
         failures[myproc] = 0.0;

            // Finally, we get to transmit or receive and time

         if( transmitter || bidir ) {   // We send first then recv

            for (i = 0; i < ntrials; i++) {   // Typically 1-7 trials per data point

               if(burst && !stream) {   // burst requires async too

                  SavePtrs( s_ptr, r_ptr );

                  for( j=0; j<nrepeat; j++ ) {

                     PrepostRecv();
                     AdvancePtrs( &s_ptr, &r_ptr, NULL, &r_ptr, buflen );
                  }

                  RestorePtrs( &s_ptr, &r_ptr );
               }

                  // Flush the cache using the dummy buffer

               if( !cache ) flushcache(memcache, MEMSIZE/sizeof(int));

               Sync();

               t0 = myclock();

                  // Transmitter: send data then recv, nrepeat's times
                  // If streaming we only send

               for (j = 0; j < nrepeat; j++) {

                  if( !burst && async && !stream) PrepostRecv();

                  SetupBufferData();
                  SendData();
                  AdvancePtrs( &ds_ptr, &dr_ptr, &s_ptr, &dr_ptr, buflen );

                  if( !stream ) {

                     if( workload ) work += DoWork( worktype, worksize, workcount, 0 );

                     RecvData();
                     CheckBufferData();   // Check the first and last bytes
                     AdvancePtrs( &s_ptr, &r_ptr, &ds_ptr, &r_ptr, buflen );
                  }
               }

#if defined(THEO)
               t = Theo_Time( );         // Get a theoretically calc time from theo.c
#else
               if( npairs > 1 ) Sync();  // Don't worry about latency for multi-pair
               t = MAX( myclock() - t0, clock_res );  // Protect against 0 time

               t /= nrepeat;
//printf("myproc %d for nrepeat %d took %f seconds\n", myproc, nrepeat, t);

               if( !stream && !bidir) t /= 2;
#endif

                  // t is now the 1-directional transmission time
                  // Calc the avg latency using the first 10 tests

               if( n != 0 && n <= 10 ) avg_latency += t; // Average out later

               Reset();
 
               min_time = MIN(min_time, t);
               max_time = MAX(max_time, t);
               avg_time += t / ntrials;
            }

         } else {   // Receiver:  Recv data first then send it back
                    // If we are streaming then just recv continuously

            for( i = 0; i < ntrials; i++ ) {

               if( burst ) {  // Prepost all recv's at once

                  SavePtrs( s_ptr, r_ptr );

                  for (j=0; j < nrepeat; j++) {

                     PrepostRecv();
                     AdvancePtrs( &s_ptr, &r_ptr, NULL, &r_ptr, buflen );
                  }

                  RestorePtrs( &s_ptr, &r_ptr );

               } else if( async ) {   // Prepost one recv

                  PrepostRecv();

               }

                  // Flush the cache using the dummy buffer

               if( !cache ) flushcache(memcache, MEMSIZE/sizeof(int));

               Sync();

               t0 = myclock();

               for( j = 0; j < nrepeat; j++ ) {

                  if( workload ) work += DoWork( worktype, worksize, workcount, 0 );

                  RecvData();
                  CheckBufferData();   // At least check the first and last bytes
                  AdvancePtrs( &s_ptr, &r_ptr, &ds_ptr, &r_ptr, buflen );
 
                  if( !burst && async && (j < nrepeat-1)) // async and not last
                     PrepostRecv();

                  if( !stream ) {  // Return the data unless we are streaming

                     SetupBufferData();
                     SendData();
                     AdvancePtrs( &ds_ptr, &dr_ptr, &s_ptr, &dr_ptr, buflen );
                  }
               }

               t = MAX( myclock() - t0, clock_res );  // Protect against 0 time
               t /= nrepeat;
               if( !stream && !bidir) t /= 2;

               if( n != 0 && n <= 10 ) avg_latency += t / (10 * ntrials);

               Reset();
 
               min_time = MIN(min_time, t);
               max_time = MAX(max_time, t);
               avg_time += t / ntrials;
            }
         }

              // Now gather the times to proc 0 if needed
                  // stream is each proc streaming to its pair
                  // rwstream is each proc memcpy or disk read/write individually

         if( stream ) {   // Recv proc sends time to trans

            times[myproc] = min_time;
            Gather(times);
            if( rwstream ) {
               for( i=0;        i < nprocs; i++) min_time = MIN(min_time, times[i]);
            } else {  // stream from 1 proc to its pair
               for( i=nprocs/2; i < nprocs; i++) min_time = MIN(min_time, times[i]);
            }

            times[myproc] = max_time;
            Gather(times);
            if( rwstream ) {
               for( i=0;        i < nprocs; i++) max_time = MAX(max_time, times[i]);
            } else {  // stream from 1 proc to its pair
               for( i=nprocs/2; i < nprocs; i++) max_time = MAX(max_time, times[i]);
            }

            times[myproc] = avg_time;
            Gather(times);
            avg_time = 0.0;
            if( rwstream ) {
               for( i=0;        i < nprocs; i++) avg_time += times[i] / nprocs;
            } else {  // stream from 1 proc to its pair
               for( i=nprocs/2; i < nprocs; i++) avg_time += times[i] / npairs;;
            }
         }

         Gather(failures);

             // Streaming mode doesn't really calculate correct latencies.
             // Protect against a zero time.

         if( min_time < 0.000001 ) min_time = 0.000001;
         tlast = avg_time;
  
             // Print to the data file np.out and stdout

         if( myproc ==  0 ) {

               // Calculate the aggregate throughput

            bytes = buflen * (1+bidir);
            min_gbps = npairs * (double) bytes * 8 * 1.0e-9 / max_time;
            avg_gbps = npairs * (double) bytes * 8 * 1.0e-9 / avg_time;
            max_gbps = npairs * (double) bytes * 8 * 1.0e-9 / min_time;

            max_bandwidth = AverageBandwidth( avg_gbps );

            for( i=0, nfails=0; i<nprocs; i++ ) nfails += (int) failures[i];

            if( integrityCheck ) {

               fprintf(out,"%9d bytes %6u times %8d failures\n", 
                            bytes, nrepeat, nfails);
               mprintf(" %8d failures\n", nfails);

            } else {

               work = work * 1e-9 / (ntrials * nrepeat * avg_time); // GFlops or GB/sec
               if( workload && !strcmp( worktype, "memcpy") ) {
                  sprintf( workstring, "   %6.2lf GB/sec\n", work);
               } else if( workload ) {
                  sprintf( workstring, "   %6.2lf GFlops\n", work);
               } else {
                  sprintf( workstring, "\n");
               }

               fprintf(out,"%9d %9.3lf %9.3lf %9.3lf %7.2lf%s", 
                       bytes, avg_gbps, min_gbps, max_gbps, avg_time*1e6, workstring);

// NOTE: Graphing max_time would emphasize any dropouts, 
//       min_time would give best results, but avg_time is probably fairest
//       The user can always choose which column to plot.

               mprintf("%s  in  %s", bitstring(avg_gbps), timestring(avg_time));
               if( nfails != 0 ) mprintf("   %d failures", nfails);
               mprintf(RED "%s" RESET, workstring);
            }
            fflush(out);
         }

         if (cache) FreeBufs( );  // Free buffers

         nq++;

      } // End of the perturbation loop

      if( tlast >= stoptime ) finished = 1;  // Stop testing due to time
      len += inc;
      if( len > end ) finished = 1;          // Stop testing due to the size
      Broadcast(&finished);                  // All procs need to know we're done

   } // End of main loop

   if( !cache ) FreeBufs( );  // Free send and receive buffers

   Module_CleanUp();  // Allow each module to do cleanup

   if( myproc ==  0 ) fclose(out);
   if( n > 1 ) avg_latency /= ( ntrials * MIN( n-1, 10 ) );
   mprintf("\nCompleted with        max bandwidth  %12s      %13s latency\n\n\n", 
           bitstring( max_bandwidth ), timestring(avg_latency) );

   return 0;   // mpirun complains if we don't return 0
}


// Return the current time in seconds using a double precision number

static time_t t_start = 0.0;  // Save and subtract off each time

double myclock()
{
   struct timespec ts;

   if( clock_res == 0 ) {   // Get the clock resolution on first call
      clock_getres(CLOCK_REALTIME, &ts);
      clock_res = (double) ts.tv_sec + ts.tv_nsec * 1.0e-9;
   }

   clock_gettime(CLOCK_REALTIME, &ts);
   if( t_start == 0.0 ) t_start = ts.tv_sec;

   return (double) (ts.tv_sec - t_start) + ts.tv_nsec * 1.0e-9;
}

   // Read the first n integers of the memmory area pointed to by ptr,
   // to flush out the cache   

void flushcache(int *ptr,  uint64_t n)
{
   static int flag = 0;
   uint64_t i; 

   flag = (flag + 1) % 2; 
   if ( flag == 0) 
       for (i = 0; i < n; i++)
           *(ptr + i) = *(ptr + i) + 1;
   else
       for (i = 0; i < n; i++) 
           *(ptr + i) = *(ptr + i) - 1; 
}

void PrintUsage( char *arg)
{
   if( arg ) mprintf( ONRED "\n**** Unknown parameter %s ****\n\n" RESET, arg);
   mprintf("\n NETPIPE USAGE - use mpirun for all tests requiring more than 1 process\n");
   mprintf("  mpirun -np # [--hostfile mpihosts] [NPmpi|NPtcp|NPibverbs|NPmemcpy] [options]\n");
   mprintf("                or\n");
   mprintf("  [NPtheo|NPdisk] [options]\n\n");
   mprintf("      -o filename   Set the output file name (default np.out)\n");
   mprintf("      --bidir       Send data in both directions at the same time\n");
   mprintf("      --nocache     Invalidate cache (data always comes from main memory)\n");
   mprintf("      --integrity   Full message integrity check instead of performance measurement\n");
   mprintf("      --workload type size   Measure the workload done during transmission\n");
   mprintf("           type: matrix size:100   - C = A * B  comp rate from cache\n");
   mprintf("           type: daxpy  size:10000 - Z = a*X + Y \n");
   mprintf("      --start #     Set the lower bound for the message sizes to test\n");
   mprintf("      --end #       Set the upper bound for the message sizes to test\n");
   mprintf("      --soffset #   Offset the send buffer from page alignment\n");
   mprintf("      --roffset #   Offset the recv buffer from page alignment\n");
   mprintf("                    Combined with --nocache, offset from 8-byte alignment\n");
   mprintf("      --pert #      Set the perturbation size (default +/- 3 bytes)\n");
   mprintf("      --fac2        Test only factors of two to compare to other benchmarks\n");
   mprintf("      --stream      Stream data in one direction only\n");
   mprintf("      --burst       Burst pre-post all receives async before timing\n");
   mprintf("      --repeats #   Set a constant number of repeats per trial\n");
   mprintf("      --quick       quick run with no perturbations, 3 trials, lower time per trial\n");
   mprintf("      --quicker     quicker run with no perturbations, 1 trial, lower time per trial\n");
   mprintf("      --quickest    Quick and dirty test of 1 repeat per trial, 1 trial\n");
   mprintf("      --printhostnames   Each proc will print its hostname\n");

   Module_PrintUsage();  // Explain the module specific input arguments

   mprintf("\n");
   Module_CleanUp();     // Clean up and exit
   exit(0);
}


static char *s_saved, *r_saved;

void SavePtrs( char *s, char *r )
{
   s_saved = s;
   r_saved = r;
}

void RestorePtrs( char **s, char **r )
{
   *s = s_saved;
   *r = r_saved;
}


   // For cache just swap the first two pointers.
   // For nocache advance the pointers through the large memory buffer if 
   // there is room, otherwise reset to the start of the buffer.
   // This routine advances the global pointers

void AdvancePtrs( char **s_cache, char **r_cache, char **s_no, char **r_no, int size )
{
   void *t_ptr;
   int n = sizeof(void *), blocksize = size;    // Round up to a factor of 8 bytes
   if( size % n > 0 ) blocksize += n - (size % n);

   if( cache ) {  // For cache just swap the first two pointers

      if( *s_cache != NULL ) {  // Skip when there are no RDMA buffers

         dbprintf("%d Swapping %p with %p\n", myproc, *s_cache, *r_cache);

         t_ptr    = *s_cache;
         *s_cache = *r_cache;
         *r_cache = t_ptr;

         dbprintf("%d Swapped  %p with %p\n", myproc, *s_cache, *r_cache);
      }

   } else {          // For nocache advance the 3rd and 4th ptrs by size

      dbprintf("%d Advancing %p %p by %d %d bytes\n", 
         myproc, *s_no, *r_no, size, blocksize);

      if( *s_no == s_ptr ) {

         if( *s_no + 2*blocksize < r_buf ) {
            *s_no += blocksize;
         } else {
            //fprintf(stderr,"\n%d flipping s_ptr %p to ", myproc, *s_no);
            *s_no = s_buf + soffset;
            //fprintf(stderr,"%p\n", *s_no);
         }

      } else if( *s_no == ds_ptr && *s_no != NULL ) {

         if( *s_no + 2*blocksize < dr_buf ) {
            *s_no += blocksize;
         } else {
            *s_no = s_buf + soffset;
         }
      }

      if( *r_no == r_ptr ) {

         if( *r_no + 2*blocksize < sr_buf + MEMSIZE ) {
            *r_no += blocksize;
         } else {
            //fprintf(stderr,"\n%d flipping r_ptr %p to ", myproc, *r_no);
            *r_no = r_buf + roffset;
            //fprintf(stderr,"%p\n", *r_no);
         }

      } else if( *r_no == dr_ptr && *r_no != NULL ) {

         if( *r_no + 2*blocksize < dsr_buf + MEMSIZE ) {
            *r_no += blocksize;
         } else {
            *r_no = dr_buf + roffset;
         }
      }
      dbprintf("%d Advanced  %p %p by %d %d bytes\n", 
         myproc, *s_no, *r_no, size, blocksize);
   }
}


void InitBufferData( uint64_t nbytes)
{
   memset(s_buf, -1, nbytes + soffset);
   //memset(r_buf, -1, nbytes + roffset);
 
   r_ptr  = r_buf  + roffset;
   s_ptr  = s_buf  + soffset;

   dr_ptr  = dr_buf  + roffset;
   ds_ptr  = ds_buf  + soffset;
}

   // Setup for an integrity test if needed otherwise set first and last bytes only

void SetupBufferData()
{
   int i;
   static int stag = 0;

   if( disk ) return;

   if( integrityCheck ) {       // Set each byte to 0..99

      for( i = 1; i < buflen-1; i++ ) {
         s_ptr[i] = i % 100;
      }
   }

      // Set the first and last bytes to stag for the quick checks

   stag = (stag + 1) % 100;
   s_ptr[0] = s_ptr[buflen-1] = stag;
}

   // Do a full integrity test if requested otherwise check first and last bytes

void CheckBufferData()
{
   int i;
   static int rtag = 0;

   if( disk ) return;

   rtag = (rtag + 1) % 100;
   if( integrityCheck ) {     // Do a full integrity check of the message

//char failure_file[100];
//sprintf( failure_file,  "failures.p%d.%dB", myproc, buflen);
//FILE *fd = fopen( failure_file, "w");

      for( i = 1; i < buflen-1; i++ ) {
         if( r_ptr[i] != i % 100) {
//fprintf(fd, "%d %d %d\n", i, i % 100, r_ptr[i]);
            failures[myproc] += 1.0;
         }
         r_ptr[i] = -1;  // Clear for the next message to overwrite
      }
      if( r_ptr[0]        != rtag ) failures[myproc] += 1.0;
      if( r_ptr[buflen-1] != rtag ) failures[myproc] += 1.0;
//fclose(fd);

   } else {      // Always at least check the first and last bytes

      if( r_ptr[0] != rtag || r_ptr[buflen-1] != rtag ) failures[myproc] += 1.0;
   }
}


#if defined(TCP) || defined(MPI) || defined(SHMEM)
   int TestForCompletion( ) { return Module_TestForCompletion(); }
#else
   int TestForCompletion( ) { return 1; }
#endif

#if defined (MPI2) || defined(TCP) || defined(DISK) || defined(IBVERBS)
   void Reset( ) { Module_Reset(); }
#else
   void Reset( ) { }
#endif

   // ibverbs needs to register memory
   // shmem needs to shmalloc shared memory
   // disk needs to create and rm data files for cp and scp tests

#if defined(IBVERBS) || defined(SHMEM) || defined(DISK)
   char * mymalloc( uint64_t nbytes ) { return Module_malloc( nbytes ); }
   void   myfree( char *buf )    { Module_free( buf ); }
//#elif defined(MPI)
   //#include <mpi.h>
   //char * mymalloc( uint64_t nbytes ) {
      //void *ptr;
      //MPI_Alloc_mem( nbytes, MPI_INFO_NULL, &ptr );
      //return ptr;
   //}
   //void   myfree( char *buf )    { MPI_Free_mem( buf ); }
#else
   char * mymalloc( uint64_t nbytes ) {
      void *buf = NULL;
      int err = posix_memalign( &buf, PAGESIZE, nbytes );
      ERRCHECK( err, "Could not malloc %d bytes", (int)nbytes);
      return buf;
   }
   void   myfree( char *buf )    { free( buf ); }
#endif


   // Malloc the send and receive buffers and register if needed
   //    Use 1 buffer split in 2 so only a single registration is needed
   //    Page align s_buf and r_buf then allow testing of offsets to aligned

void MallocBufs(  uint64_t size )
{
   uint64_t nbytes;

   if( disk > 1 ) {  // Doing a cp or scp so create file of size nbytes
      nbytes = size;
      sr_buf = mymalloc( nbytes );
      return;
   }

   nbytes = size/2 + MAX(soffset,roffset);    // Round up to an even page size
   if( nbytes % PAGESIZE > 0 ) nbytes += (PAGESIZE - nbytes % PAGESIZE);

      // mymalloc will register memory or allocate from shared pool if necessary

   sr_buf = mymalloc( 2*nbytes );  // Uses posix_memalign() to page align
   ERRCHECK( ! sr_buf, "Can't allocate %d bytes for send/recv buffers", (int)nbytes);

      // Set r_buf to second half and page align both buffers

   s_buf = sr_buf;
   r_buf = sr_buf + nbytes;
   r_ptr = r_buf  + roffset;   // Allow testing of offsets to page alignment
   s_ptr = s_buf  + soffset;

   dbprintf("%d malloc(%d)  sr_buf = %p  s_ptr = %p  r_ptr = %p\n", 
      myproc, 2*(int)nbytes, sr_buf, s_ptr, r_ptr);

      // Do the same for a remote buffer if present for RDMA modules
      // Module_malloc() must have properly set dsr_buf as the remote buffer

   if( dsr_buf != NULL ) {

      ds_buf = dsr_buf;
      dr_buf = dsr_buf + nbytes;
      dr_ptr = dr_buf  + roffset;   // Allow testing of offsets to page alignment
      ds_ptr = ds_buf  + soffset;

      dbprintf("%d malloc(%d) dsr_buf = %p ds_ptr = %p dr_ptr = %p\n", 
         myproc, 2*(int)nbytes, sr_buf, s_ptr, r_ptr);
   }

      // Set up all pointers for the memcpy.c module

   if( !strcmp( module, "memcpy" ) ) {

      dsr_buf = sr_buf;
      ds_buf  = s_buf;
      ds_ptr  = s_ptr;
      dr_buf  = r_buf;
      dr_ptr  = r_ptr;

   }

   if( disk ) {
      memset( s_buf, 111, 2*nbytes);
   } else {
      memset( s_buf, -1, 2*nbytes);
   }
}

// Free the send/recv buffer and deregister memory if needed

void FreeBufs( )
{
   myfree( sr_buf );
}


   // Measure the CPU workload during communications

double DoWork( char *type, int nelem, int count, int calibration )
{
   int i, j, k, w, ntests = 0, nbytes;
   static double **A, **B, **C;  // Matrices
   static double *X, *Y, *Z;     // Vectors
   static double a;              // constant
   double flops = 0, bytes_moved = 0;
   char *r_ptr_saved = r_ptr;

   if( !strcmp( type, "matrix") && A == NULL ) {   // Alloc and init matrices

      dbprintf("%d Initializing matrices for matrix multiplication\n", myproc);
      nbytes = nelem * sizeof( double * );
      A = malloc( nbytes );
      ERRCHECK( !A, "Malloc of A for %d bytes failed", nbytes);
      B = malloc( nbytes );
      ERRCHECK( !B, "Malloc of B for %d bytes failed", nbytes);
      C = malloc( nbytes );
      ERRCHECK( !C, "Malloc of C for %d bytes failed", nbytes);
      nbytes = nelem * sizeof( double );
      for( i = 0; i < nelem; i++ ) {
         A[i] = malloc( nbytes);
         ERRCHECK( !A[i], "Malloc of A[%d] for %d bytes failed",i,nbytes);
         B[i] = malloc( nbytes);
         ERRCHECK( !B[i], "Malloc of B[%d] for %d bytes failed",i,nbytes);
         C[i] = malloc( nbytes);
         ERRCHECK( !C[i], "Malloc of C[%d] for %d bytes failed",i,nbytes);
         for( j = 0; j < nelem; j++ ) {
            A[i][j] = i*nelem + j;
            B[i][j] = 2*j - 1;
            C[i][j] = 0.0;
         }
      }

   } else if( X == NULL ) {   // Alloc and init vectors for daxpy and memcpy

      dbprintf("%d Initializing vectors for daxpy or memcpy\n", myproc);
      a = 2*acos(0);
      nbytes = nelem * sizeof( double );
      X = malloc( nbytes);
      Y = malloc( nbytes);
      Z = malloc( nbytes);
      for( i = 0; i < nelem; i++ ) {
         X[i] = i;
         Y[i] = i*i;
         Z[i] = 0.0;
      }
   }

      // Do small amounts of work until comm is done

   dbprintf("%d DoWork before while loop cal = %d\n", myproc, calibration);
   bytesLeft = buflen;  // tcp.c TestForCompletion() needs this running count

   while( calibration || ! TestForCompletion() ) {   

      ntests++;

      for( w = 0; w < count; w++ ) {  // How much work between probing the network

         if(        !strcmp( type, "matrix") ) {

            for( i = 0; i < nelem; i++ ) {
               for( j = 0; j < nelem; j++ ) {
                  for( k = 0; k < nelem; k++ ) {
                     C[i][j] += A[i][k] * B[k][j];
            }  }  }
            A[0][0] = B[nelem-1][nelem-1] = ntests;
            flops += 2 * nelem*nelem*nelem;

         } else if( !strcmp( type, "daxpy") ) {

            for( i = 0; i < nelem; i++ ) {
               Z[i] = a * X[i] + Y[i];
            }
            X[0] = X[nelem-1] = ntests;
            flops += nelem * 2;

         } else if( !strcmp( type, "memcpy") ) {

            memcpy( Z, X, nelem * sizeof(double) );
            X[0] = X[nelem-1] = ntests;
            bytes_moved += nelem * sizeof(double);  // Global variable

         }
      }
      if( ntests == calibration ) break;
   }
   r_ptr = r_ptr_saved;  // Return r_ptr as tcp.c advances it while testing

   dbprintf("%d DoWork after while loop ntests = %d\n", myproc, ntests);

   return MAX( flops, bytes_moved );  // Return whichever is relavent
}


char * bytestring( int nbytes )
{
   char bstring[32];

   if( nbytes < 1e3 ) {
      sprintf( bstring, GREEN  "    %3d  B" RESET, nbytes);
   } else if( nbytes < 1e6 ) {
      sprintf( bstring, CYAN    "%7.3lf KB" RESET, nbytes/1e3);
   } else if( nbytes < 1e9 ) {
      sprintf( bstring, MAGENTA "%7.3lf MB" RESET, nbytes/1e6);
   } else if( nbytes < 1e12 ) {
      sprintf( bstring, RED     "%7.3lf GB" RESET, nbytes/1e9);
   } else {
      sprintf( bstring, CYAN    "%7.3lf TB" RESET, nbytes/1e12);
   }
   return strdup( bstring );
}

char * bitstring( double gbps )
{
   char bstring[32];
   double bps = gbps * 1e9;

   if( bps < 1.0e3) {
      sprintf( bstring, GREEN   "%7.3lf  bps" RESET, bps);
   } else if( bps < 1.0e6) {
      sprintf( bstring, CYAN    "%7.3lf Kbps" RESET, bps/1e3);
   } else if( bps < 1.0e9) {
      sprintf( bstring, MAGENTA "%7.3lf Mbps" RESET, bps/1e6);
   } else if( bps < 1.0e12) {
      sprintf( bstring, RED     "%7.3lf Gbps" RESET, bps/1e9);
   } else {
      sprintf( bstring, YELLOW  "%7.3lf Tbps" RESET, bps/1e12);
   }
   return strdup( bstring );
}

char * timestring( double secs )
{
   char bstring[32];

   if( secs < 1.0e-6) {
      sprintf( bstring, YELLOW  "%7.3lf nsecs" RESET, secs*1e9);
   } else if( secs < 1.0e-3) {
      sprintf( bstring, CYAN    "%7.3lf usecs" RESET, secs*1e6);
   } else if( secs < 1.0) {
      sprintf( bstring, GREEN   "%7.3lf msecs" RESET, secs*1e3);
   } else {
      sprintf( bstring, BLUE    "%7.3lf  secs" RESET, secs);
   }

   return strdup( bstring );
}

   // Calculate the average of the n highest bandwidth rates

double AverageBandwidth( double rate )      // rate is in Gbps
{
   int i, lowest_rate_index = 0, nrates;
   static double *bw;
   double lowest_rate = 1e100, avg_bw = 0.0;

   if( perturbation ) nrates = 10; else nrates = 5;

      // Initialize bw array first time called

   if( bw == NULL ) {
      bw = (double *) malloc( nrates * sizeof(double) );
      for( i=0; i < nrates; i++ ) bw[i] = 0.0;
   }

      // Find the lowest member and replace with incoming rate if higher

   for( i = 0; i < nrates; i++ ) {

      if( bw[i] < lowest_rate ) {
         lowest_rate = bw[i];
         lowest_rate_index = i;
      }
   }

   if( rate > lowest_rate ) bw[lowest_rate_index] = rate;

      // Return the average bw over the nrates highest communication rates

   for( i=0; i < nrates; i++ ) avg_bw += bw[i] / nrates;

   return avg_bw;
}


