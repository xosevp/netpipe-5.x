////////////////////////////////////////////////////////////////////////////
// theo.c - theoretical module for NetPIPE
// Developed by Dave Turner - Kansas State University - October 2015 -
// contact DrDaveTurner@gmail.com
// NetPIPE is freely distributed under a GPL license.
//
// Usage: make theo
//        theo --ethernet --maxrate 40 --latency 15 [--mtu 9000]
//        theo --infiniband --maxrate 32 --latency 1.5         (QDR InfiniBand)
//        theo --help                         (for complete usage information)
///////////////////////////////////////////////////////////////////////////////
// Create a theoretical performance curve from the latency and network info
//
//     T(n) = To + No * Npackets / Rmedium  +  n / Rmedium
//
//     Tlat = To + No / Rmedium
//
//     T(n) = Tlat + ( No * (Npackets - 1) + n ) / Rmedium
//
//            To is the handshake time
//            Tlat is the measured latency input from --latency #
//            No is the number of bytes in the packet header++
//               (Ethernet 42 bytes,  InfiniBand 30 bytes???)
//            mtu is the MTU size
//               (1500 byte MTU for Ethernet, 2048 default for InfiniBand)
//               (this can be changed using --mtu #
//            Npackets = int( (n + mtu - 1) / mtu )
//            Rmedium is the maximum theoretical bandwidth of the medium in Gbps
//               entered as --maxrate #
//               (10 Gbps for 10 GigE, 32 Gbps for QDR InfiniBand)
///////////////////////////////////////////////////////////////////////////////

   // Pull in global variables as externs

#define _GLOBAL_VAR_ extern
#include "netpipe.h"

double maxrate = 0, latency = 0;  // The 2 required input values
int mtu = 0, nheader = 0;         // Set according to the network type
int rendezvous = 1000000000;      // Rendezvous threshold
int max_inline = 1000000000;      // Inline threshold
int alliance = 0, mellanox = 0;

double Theo_Time( )
{
   double t;
   int n = buflen, npackets = 0;


   if( alliance || mellanox ) { // Pad to half packet then by bytes

      if( n <= mtu/2 ) {
         n = mtu/2;            // Pad to half packet
         npackets = 1;
      } else {
         npackets = 1 + ( (n-mtu/2 + mtu - 1) / mtu );  // extra packet
      }

   } else {

      npackets = (n + mtu - 1) / mtu;

   }

   t = latency + (nheader * (npackets-1) + n) / maxrate;
   if( (alliance || mellanox) && n > mtu/2 ) t += latency;  // extra latency hit

   if( n > max_inline ) t += latency;  // Double the measured latency

   if( n > rendezvous ) t += latency;   // Add in the time for the rendezvous msg

   return t / 1.0e9;               // Return the time in seconds
}


void Module_Init(int* pargc, char*** pargv)
{
   myproc = 0;
   nprocs = 1;
   stream = 1;
   //perturbation = 0;

   nrepeat_const = 1;
   ntrials = 1;

   module = strdup( "theo" );  // Not used at this point
}

   // Process the module specific input arguments

void Module_ArgOpt( char *arg, char *opt )
{
   if(        !strcmp( arg, "maxrate") ) {
      ERRCHECK( ! opt, "No max theoretical rate was given!");
      maxrate = atof( opt );

   } else if( !strcmp( arg, "latency") ) {
      ERRCHECK( ! opt, "No latency was given!");
      latency = atof( opt );

   } else if( !strcmp( arg, "ethernet") || !strcmp( arg, "E") ) {
      nheader = 42;    // Pre 7 SFD 1 MAC 6+6 tag 4 Ethertype 2 CRC 4 gap 12
      if( mtu == 0 ) mtu = 1500;

   } else if( !strcmp( arg, "infiniband") || !strcmp( arg, "IB") ) {
      if( mtu == 0 ) mtu = 4096;
      nheader = 34;    // LRH 8 IBA 12 ext&imm 8? CRC 6

   } else if( !strcmp( arg, "alliance") ) {
      alliance = 1;    // Set alliance to true

   } else if( !strcmp( arg, "mellanox") ) {
      mellanox = 1;    // Set mellanox to true

   } else if( !strcmp( arg, "roce") ) {
      if( mtu == 0 ) mtu = 4096;
      nheader = 74;    // LRH 8 GRH 40 IBA 12 ext&imm 8? CRC 6

   } else if( !strcmp( arg, "mtu") || ! strcmp( arg, "MTU") ) {
      ERRCHECK( ! opt, "No MTU size was given!");
      mtu = atoi( opt );

   } else if( !strcmp( arg, "inline") ) {
      ERRCHECK( ! opt, "No max inline value was given!");
      max_inline = atoi( opt );
      mprintf("Data will be inlined up to %d bytes\n", max_inline);

   } else if( !strcmp( arg, "rendezvous") || ! strcmp( arg, "MTU") ) {
      ERRCHECK( ! opt, "No rendezvous threshold was given!");
      rendezvous = atoi( opt );

   } else {
      PrintUsage( arg );
   }
}

void Module_PrintUsage()
{
   mprintf("\n  NPtheo specific input arguments\n");
   mprintf("\n    (must choose ethernet|infiniband|roce with maxrate and latency)\n");
   mprintf("  --ethernet     Use 1500 byte mtu size, 26 byte preamble, 20 byte header\n");
   mprintf("  --infiniband   Use 4096 byte mtu size, 30 byte header\n");
   mprintf("    --alliance     Start with 2 padded half packets\n");
   mprintf("    --mellanox     Start with a padded half packet\n");
   mprintf("  --roce         Use 4096 byte mtu size, 74 byte header\n");
   mprintf("  --maxrate #    The theoretical max bandwidth for the medium in Gbps\n");
   mprintf("  --latency #    The measured small message latency in microseconds\n");
   mprintf("  --mtu #        Change the MTU size (dafault 1500 Ethernet 4096 IB)\n");
   mprintf("  --inline #     The first # bytes are inlined with the message\n");
   mprintf("  --rendezvous # Add an extra message send at the rendezvous threshold\n");
}

void Module_Setup()
{
   ERRCHECK( maxrate <= 0 || latency <= 0, "You must set the maxrate and latency");
   ERRCHECK( nheader <= 0, "You must specify --ethernet or --infiniband");

   mprintf("Using %lf Gbps with a %lf usec latency for the medium\n", maxrate, latency);
   mprintf("Using a %d byte MTU with a %d byte header\n", mtu, nheader);

   maxrate = maxrate / 8;     // Convert Gbps to GB/sec
   latency *= 1e3;            // Convert to nsec

   if( alliance || mellanox ) latency -= (mtu/2) / maxrate;
}   

void PrepostRecv() { }

void SendData() { }

void RecvData() { }

void Sync() { }

void Broadcast(int *buf) { }

void Gather(double *buf) { }

void Module_CleanUp() { }

