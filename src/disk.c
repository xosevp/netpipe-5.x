////////////////////////////////////////////////////////////////////////////
// disk.c module for NetPIPE - measure file access rates
// Dave Turner - Kansas State University - October 2015 -
// contact DrDaveTurner@gmail.com
// NetPIPE is freely distributed under a GPL license.
//
// Usage: make disk
//        NPdisk --write --string 100 --datafile data.string100   (string write speed)
//        NPdisk --write --double --datafile data.double   (double write speed)
//        NPdisk --cpto datadir                  (cp files from cwd to datadir)
//        NPdisk --cp datadir                    (cp files from datadir to datadir)
//        NPdisk --scp user@ssh_host:datadir ... (test file scp performance)
//        NPdisk --usage                      for complete usage information
//        NPdisk --createfile #        create a # GB file to clear the memory buffer
//
//        mpirun -np 24 NPdisk ...         run 24 synced copies of NPdisk
////////////////////////////////////////////////////////////////////////////

   // Pull in global variables as externs

#define _GLOBAL_VAR_ extern
#include <mpi.h>
#include "netpipe.h"

char *cparg, *datadir, *datafile, *ssh_host, iofunc, iotype, *cbuf;
int nchar, flush, cpflag = 0, scpflag = 0, overwrite = 0, num_GB = 0, err;
static FILE *fd;
static int ifd;

void Module_Init(int* pargc, char*** pargv) 
{

      // Choose to write characters to npdisk.out by default

   iofunc = 'w';
   iotype = 'c';
   datafile = strdup( "npdisk.out" );
   flush = 0;

   MPI_Init( pargc, pargv);
   MPI_Comm_rank(MPI_COMM_WORLD, &myproc);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
   //myproc = 0;
   //nprocs = 1;

   transmitter = 1;
   stream = 1;
   rwstream = 1;           // read/write streaming data in 1 direction only
   perturbation = 0;
   disk = 1;               // Skip first and last byte checks
   ntrials = 1;

   module = strdup( "disk" );  // Used in netpipe.c to set npairs = nprocs

   est_latency = 10.0e-6;  // Used to calc initial nrepeat
}

   // Process the module specific input arguments

void Module_ArgOpt( char *arg, char *opt )
{
   char *ptr;
   char cmd[120];

   if(        !strcmp( arg, "datafile") ) {
      ERRCHECK( strlen( opt ) == 0 , "No data file name was given\n");
      datafile = strdup( opt );

   } else if( !strcmp( arg, "read") ) {
      iofunc = 'r';

   } else if( !strcmp( arg, "write") ) {
      iofunc = 'w';

   } else if( !strcmp( arg, "char") ) {
      iotype = 'c';

   } else if( !strcmp( arg, "string") ) {

      iotype = 's';
      ERRCHECK( strlen( opt ) == 0, "No string length given\n");
      start = nchar = atoi( opt );
      cbuf = malloc( nchar );
      memset( cbuf, 111, nchar );    // Set string to characters to 'o'

   } else if( !strcmp( arg, "double") ) {
      iotype = 'd';
      start = 8;

   } else if( !strcmp( arg, "unformatted") ) {
      iotype = 'u';
      start = 8;

   } else if( !strcmp( arg, "binary") ) {
      iotype = 'b';

   } else if( !strcmp( arg, "flush") ) {
      flush = 1;
      mprintf("flushing output data after each write\n");

   } else if( !strcmp( arg, "cp") || !strcmp( arg, "cpto" ) ) {
      cpflag = 1;  if( !strcmp( arg, "cpto" ) ) cpflag = 2;
      disk = 10;          // Do 10 file copies per trial
      end = 1000000000;   // Test files up to 1 GB in size
      iotype = '0';
      cparg = strdup( "cp" );
      ERRCHECK( strlen( opt ) == 0, "No data directory name was given\n");
      datadir = strdup( opt );
      sprintf(cmd, "mkdir -p %s\n", datadir);
      err = system( cmd );
      stoptime = 10.0;  est_latency = 3.0e-3;
      mprintf("measuring the file copy rate to %s\n", datadir);

   } else if( !strcmp( arg, "scp") ) {
      scpflag = 1;
      disk = 10;   // Do 10 file copies per trial
      iotype = '0';
      cparg = strdup( "scp" );
      ERRCHECK( strlen( opt ) == 0, "No data directory name was given\n");
      datadir = strdup( opt );
      ptr = strstr( opt, ":" );
      ssh_host = strndup( opt, ptr-opt);
      // DDT - add ssh permission and write check to that directory here
      stoptime = 10.0;  est_latency = 3.0e-3;
      mprintf("measuring the file copy rate to %s\n", datadir);

   } else if( !strcmp( arg, "overwrite") ) {
      overwrite = 1;
      mprintf("This will test overwriting the file each time\n");

   } else if( !strcmp( arg, "createfile") ) {
      ERRCHECK( strlen( opt ) == 0, "No file size was given\n");
      num_GB = atoi( opt );
      cbuf = malloc( 1 << 30 );
      memset( cbuf, 111, nchar );    // Set string to characters to 'o'
      
   } else {
      PrintUsage( arg );
   }
}

void Module_PrintUsage()
{
   mprintf("\n  NPdisk specific input arguments (default is to write chars to datafile)\n");
   mprintf("  --datafile filename    Use 'filename' to read or write test data\n");
   mprintf("  --read                 Test reading from datafile\n");
   mprintf("  --write                Test writing to datafile\n");
   mprintf("  --char                 Read/write ascii char to/from the datafile\n");
   mprintf("  --string #             Read/write string of # chars to/from the datafile\n");
   mprintf("  --double               Read/write formatted doubles to datafile\n");
   mprintf("  --unformatted          Stream unformatted doubles to/from the datafile\n");
   mprintf("  --binary               Stream the binary data in/out in 1 chunk\n");
   mprintf("  --flush                fflush() data after each write\n");
   mprintf("\n  NPdisk arguments for copying files\n");
   mprintf("  --cp dirname           Files will be copied to this directory\n");
   mprintf("  --scp dirname          Files will be copied to this user@ssh_host:directory\n");
   mprintf("                         Must be able to scp to that host without a password\n");
   mprintf("  --overwrite            File will be overwritten each time (not default)\n");
   mprintf("\n  --createfile #         Create a # GB file to clear the memory buffer\n");
   exit(0);
}

void Module_Setup() 
{
   int i;
   char filename[100];

   if( cpflag || scpflag ) return;   // Files created by Module_malloc() instead

      // Open the datafile using both int and FILE descriptors

   sprintf( filename, "%s.n%d", datafile, myproc);

   if( iofunc == 'w' ) err = remove( filename );   // Try removing old file
   //fd = fopen( filename, &iofunc );
   //ifd = open( filename, O_CREAT | O_RDWR | O_DSYNC, 0644 );
   ifd = open( filename, O_CREAT | O_RDWR | O_SYNC, 0644 );
   //ifd = open( filename, O_CREAT | O_RDWR | __O_DIRECT, 0644 );
   ERRCHECK( ! ifd, "Could not open() the data file %s\n", filename);
   fd = fdopen( ifd, "w+" );   // Yes, w+ really is needed to match O_RDWR :)
   ERRCHECK( ! fd, "Could not fdopen() the data file %s\n", filename);

   if( num_GB > 0 ) {              // Create a large file to clear the memory buffer

      for( i = 0; i < num_GB; i++ ) {
         fwrite( cbuf, 1 << 30, 1, fd );
      }
      fclose( fd );
      exit(0);

   } else if( iofunc == 'r' ) {
      mprintf("You must create an appropriate file using NPdisk --write first.\n");
      mprintf("To avoid reading from data in the write buffer, it's best to\n");
      mprintf("perform the NPdisk --read on a different host from the NPdisk --write\n");
      mprintf("whenever possible.  Otherwise you'll need to clear the memory buffer\n");
      mprintf("usually by creating a large file half the size of RAM in your system.\n");
      mprintf("NPdisk --createfile 64     will create a 64 GB file.\n\n");
   } else {
      mprintf("All tests to a RAM disk should be accurate.\n");
      mprintf("Writes to disk are usually buffered, so results can be much higher\n");
      mprintf("than the actual disk speed. The --flush flag does fflush the write\n");
      mprintf("buffer but this still leaves plenty of buffering in.\n");
      mprintf("Having said this, most write speeds are limited by the conversion\n");
      mprintf("to formatted state or the writing of small amounts of data at a time.\n");
      mprintf("You'll only approach the system limits by streaming binary data out.\n\n");
   }
      
   if( iotype == 'c' ) {
      mprintf("Using single character IO\n");
   } else if( iotype == 's' ) {
      mprintf("Using %d character strings with returns\n", nchar);
   } else if( iotype == 'd' ) {
      mprintf("Data rates count 8-bytes per double for bandwidth,\n");
      mprintf("not the bandwidth for the amount of ascii characters.\n");
   } else if( iotype == 'u' || iotype == 'b' ) {
   }
}   

   // SendData will either read or write data to the disk file
   // If the results are too good, the system is buffering

static int fs = 1;   // file read return, will go to zero if EOF
static int filenum = 0;

void SendData()
{
   int n, nbytes = buflen;
   char letter;
   double dvar;
   char cmd[120];
   //char line[100];
   static uint64_t n_static = 0;

   n_static++;

   if( fs == 0 ) {

      return;    // We have read to the end of the file already

   } else if( cpflag || scpflag ) {                // cp or scp datafile to datadir

         // nbytes will be a factor of 10 with source file of that size pre-made

      if( ! overwrite ) ++filenum;   // Use a different dest file name each time
      if( cpflag == 1 ) {
         sprintf( cmd, "cp %s/npdata-in.%d %s/npdata-out.%d\n", 
                        datadir, nbytes, datadir, filenum);
      } else if( cpflag == 2 ) {
         sprintf( cmd, "cp npdata-in.%d %s/npdata-out.%d\n", 
                        nbytes, datadir, filenum);
      } else {
//DDT
         exit(0);   // Need to finish the scp stuff here
      }

      err = system( cmd );

   } else if( iofunc == 'r' && iotype == 'c' ) {   // Read by char

         for( n = 0; n < nbytes; n++ ) {
            //fs = fscanf(fd,"%c", &letter); 
            //fs = fgetc(fd,"%c", &letter); 
            letter = getc(fd);
            if( 0 == 1 ) printf("%c", letter );
         }

   } else if( iofunc == 'r' && iotype == 's' ) {   // Read by strings

         for( n = 0; n < nbytes; n += nchar) {
            fs = fscanf( fd, "%s\n", cbuf); 
         }

   } else if( iofunc == 'r' && iotype == 'd' ) {   // Read by formatted doubles

         for( n = 0; n < nbytes; n += sizeof(double) ) {
            fs = fscanf(fd, "%lf\n", &dvar);
         }

   } else if( iofunc == 'r' && iotype == 'u' ) {   // Read by unformatted doubles

         for( n = 0; n < nbytes; n += sizeof(double) ) {
            fs = fread( &dvar, sizeof(double), 1, fd);
         }

   } else if( iofunc == 'r' && iotype == 'b' ) {   // Stream char from binary file

         fs = fread( s_ptr, sizeof(char), nbytes, fd);

         if( ferror( fd ) || fs != nbytes ) {
            printf("Proc %d failed a read of %d bytes fs=%d\n", myproc, nbytes, fs );
            clearerr( fd );
         }

   } else if( iofunc == 'w' && iotype == 'c' ) {   // Write by char

         for(n=0; n<nbytes; n++) {
            putc('o',fd);
         }

   } else if( iofunc == 'w' && iotype == 's' ) {   // Write by strings

         for( n = 0; n < nbytes; n += nchar) {
            //fputs( cbuf, fd);
            fprintf( fd, "%s\n", cbuf);
         }

   } else if( iofunc == 'w' && iotype == 'd' ) {   // Write by formatted doubles

         //for( n = 0; n < nbytes; n += 10 * sizeof(double) ) {
            //fprintf( fd, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf\n", 
                          //1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0);
         //}

         dvar = (double) n_static / 3.0;
         for( n = 0; n < nbytes; n += sizeof(double) ) {
            fprintf( fd, "%lf\n", dvar);
            //sprintf( line, "%lf\n", dvar);   // All the time is in the conversion
         }

   } else if( iofunc == 'w' && iotype == 'u' ) {   // Write by unformatted doubles

         dvar = (double) n_static / 3.0;
         for( n = 0; n < nbytes; n += sizeof(double) ) {
            fs = fwrite( &dvar, sizeof(double), 1, fd);
         }

   } else if( iofunc == 'w' && iotype == 'b' ) {   // Stream char to binary file

         fs = fwrite( s_ptr, nbytes, 1, fd);

         if( ferror( fd ) || fs != nbytes ) {
            printf("Proc %d failed a write of %d bytes fs=%d\n", myproc, nbytes, fs );
            clearerr( fd );
         }

   } else {
      ERRCHECK(1, "SendData unknown iofunc %c or iotype %c\n", iofunc, iotype);
   }

   if( fs == 0 ) finished = 1;  // Reached end of file on reads

   if( flush ) fflush( fd );
}

void RecvData() { }     // RecvData should actually do nothing

void Sync()
{
   //if( nprocs == 1 ) return;

   MPI_Barrier(MPI_COMM_WORLD);
}

void Broadcast(int *nrepeat)
{
   MPI_Bcast( nrepeat, 1, MPI_INT, 0, MPI_COMM_WORLD);
//printf( "myproc %d has nrepeat of %d\n", myproc, *nrepeat);
}

void Gather(double *buf)
{
   double time = buf[myproc];

   MPI_Gather( &time, 1, MPI_DOUBLE, buf, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

void PrepostRecv() { }

void Module_CleanUp()
{
   MPI_Finalize();
   if( ! cpflag && ! scpflag ) fclose( fd );
}

static char df[120];

char *Module_malloc( uint64_t nbytes )  // For file copies, create the file to be copied
{
   int i;
   FILE *lfd;
   void *buf = NULL;

   if( cpflag || scpflag ) {  // cp or scp datafile to datadir

         // Make source datafile with nbytes size

      if( cpflag == 1 ) {
         sprintf(df, "%s/npdata-in.%d", datadir, (int)nbytes);
      } else {
         sprintf(df, "npdata-in.%d", (int)nbytes);
      }
      lfd = fopen( df, "w" );
      //fd = open( datafile, O_CREAT | O_DIRECT | O_DSYNC, S_IWUSR );
      for( i=0; i < nbytes; i++ ) putc( 'o', lfd);
      fclose( lfd );

   } else {

      err = posix_memalign( &buf, PAGESIZE, nbytes );
      ERRCHECK( err, "Could not malloc %ld bytes\n", nbytes);
   }

   return buf;
}

void Module_free( void *buf)      // Remove the datafile
{
   if( cpflag || scpflag ) {

      err = remove( df );         // Remove the source datafile

   } else {
      free( buf );
   }
}

   // For file copy, remove all dest files before starting the next round of tests

void Module_Reset()
{
   char cmd[120];

   if( cpflag ) {
      sprintf(cmd, "rm -fr %s/npdata-out.*\n", datadir);
      err = system( cmd );
   } else if( scpflag ) {
      sprintf(cmd, "ssh %s 'rm -fr %s/%s.*'\n", ssh_host, datadir, datafile);
      err = system( cmd );
   }
}

