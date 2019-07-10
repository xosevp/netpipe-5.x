/*                  pong.c Generic Benchmark code
 *               Dave Turner - Ames Lab - July of 1994+++
 *
 *  Most Unix timers can't be trusted for very short times, so take this
 *  into account when looking at the results.  This code also only times
 *  a single message passing event for each size, so the results may vary
 *  between runs.  For more accurate measurements, grab NetPIPE from
 *  http://www.scl.ameslab.gov/ .
 */

void 
main (int *argc, char ***argv)
{
  int myproc, end_node, size, other_node, nprocs, i, last;
  double t1, t2, time, MP_Time ();
  double *a, *b, *c;
/*   double a[132000],b[132000],t1,t2,time; */
  double max_rate = 0.0, min_latency = 10e6;
  int isid, irid, irid_a, irid_b;

#if defined (_CRAYT3E)
  a = (double *) shmalloc (132000 * sizeof (double));
  b = (double *) shmalloc (132000 * sizeof (double));
#else
  a = (double *) malloc (132000 * sizeof (double));
  b = (double *) malloc (132000 * sizeof (double));
#endif

  for (i = 0; i < 132000; i++)
    {
      a[i] = (double) i;
      b[i] = 0.0;
    }

  MP_Init (argc, argv);
  MP_Myproc (&myproc);
  MP_Nprocs (&nprocs);
  if (nprocs != 2)
    exit (1);
  end_node = nprocs - 1;
  other_node = (myproc + 1) % 2;

  printf ("Hello from %d of %d\n", myproc, nprocs);
  MP_Sync ();

/* Timer accuracy test */

  t1 = MP_Time ();
  t2 = MP_Time ();
  while (t2 == t1)
    t2 = MP_Time ();
  if (myproc == 0)
    printf ("Timer accuracy of ~%f usecs\n\n", (t2 - t1) * 1000000);

/* Communications between nodes 
 *   - Blocking sends and recvs
 *   - No guarantee of prepost, so might pass through comm buffer
 */

  MP_Enter("Blocking sends and recvs");
  for (size = 8; size <= 1048576; size *= 2)
    {
      for (i = 0; i < size / 8; i++)
	{
	  a[i] = (double) i;
	  b[i] = 0.0;
	}
      last = size / 8 - 1;
      MP_Sync ();
      t1 = MP_Time ();
      if (myproc == 0)
	{
	  MP_Send (a, size, other_node);
	  MP_Recv (b, size, other_node);
	}
      else
	{
	  MP_Recv (b, size, other_node);
	  b[0] += 1.0;
	  if (last != 0)
	    b[last] += 1.0;
	  MP_Send (b, size, other_node);
	}
      t2 = MP_Time ();
      time = 1.e6 * (t2 - t1);
      MP_Sync ();
      if ((b[0] != 1.0 || b[last] != last + 1))
	{
	  printf ("ERROR - b[0] = %f b[%d] = %f\n", b[0], last, b[last]);
	  exit (1);
	}
      for (i = 1; i < last - 1; i++)
	if (b[i] != (double) i)
	  printf ("ERROR - b[%d] = %f\n", i, b[i]);
      if (myproc == 0 && time > 0.000001)
	{
	  printf (" %7d bytes took %9.0f usec (%8.3f MB/sec)\n",
		  size, time, 2.0 * size / time);
	  if (2 * size / time > max_rate)
	    max_rate = 2 * size / time;
	  if (time / 2 < min_latency)
	    min_latency = time / 2;
	}
      else if (myproc == 0)
	{
	  printf (" %7d bytes took less than the timer accuracy\n", size);
	}
    }
        MP_Leave(0.0);

/* Async communications
 *   - Prepost receives to guarantee bypassing the comm buffer
 */

  MP_Enter("Async communications");
  MP_Sync ();
  if (myproc == 0)
    printf ("\n  Asynchronous ping-pong\n\n");
  for (size = 8; size <= 1048576; size *= 2)
    {
      for (i = 0; i < size / 8; i++)
	{
	  a[i] = (double) i;
	  b[i] = 0.0;
	}
      last = size / 8 - 1;
      MP_ARecv (b, size, other_node, &irid);
      MP_Sync ();
      t1 = MP_Time ();
      if (myproc == 0)
	{
	  MP_Send (a, size, other_node);
	  MP_Wait (&irid);
	}
      else
	{
	  MP_Wait (&irid);
	  b[0] += 1.0;
	  if (last != 0)
	    b[last] += 1.0;
	  MP_Send (b, size, other_node);
	}
      t2 = MP_Time ();
      time = 1.e6 * (t2 - t1);
      MP_Sync ();
      if ((b[0] != 1.0 || b[last] != last + 1))
	{
	  printf ("ERROR - b[0] = %f b[%d] = %f\n", b[0], last, b[last]);
	}
      for (i = 1; i < last - 1; i++)
	if (b[i] != (double) i)
	  printf ("ERROR - b[%d] = %f\n", i, b[i]);
      if (myproc == 0 && time > 0.000001)
	{
	  printf (" %7d bytes took %9.0f usec (%8.3f MB/sec)\n",
		  size, time, 2.0 * size / time);
	  if (2 * size / time > max_rate)
	    max_rate = 2 * size / time;
	  if (time / 2 < min_latency)
	    min_latency = time / 2;
	}
      else if (myproc == 0)
	{
	  printf (" %7d bytes took less than the timer accuracy\n", size);
	}
    }

   MP_Leave(0.0);

  MP_Enter("Bi-directional asynchronous ping-pong");
  if (myproc == 0)
    printf ("\n  Bi-directional asynchronous ping-pong\n\n");
  for (size = 8; size <= 1048576; size *= 2)
    {
      for (i = 0; i < size / 8; i++)
	{
	  a[i] = (double) i;
	  b[i] = 0.0;
	}
      last = size / 8 - 1;
      MP_ARecv (b, size, other_node, &irid_b);
      MP_ARecv (a, size, other_node, &irid_a);
      MP_Sync ();
      t1 = MP_Time ();

      MP_Send (a, size, other_node);
      MP_Wait (&irid_b);

      b[0] += 1.0;
      if (last != 0)
	b[last] += 1.0;

      MP_Send (b, size, other_node);
      MP_Wait (&irid_a);

      t2 = MP_Time ();
      time = 1.e6 * (t2 - t1);
      MP_Sync ();
      if ((a[0] != 1.0 || a[last] != last + 1))
	{
	  printf ("ERROR - a[0] = %f a[%d] = %f\n", a[0], last, a[last]);
	}
      for (i = 1; i < last - 1; i++)
	if (a[i] != (double) i)
	  printf ("ERROR - a[%d] = %f\n", i, a[i]);
      if (myproc == 0 && time > 0.000001)
	{
	  printf (" %7d bytes took %9.0f usec (%8.3f MB/sec)\n",
		  size, time, 2.0 * size / time);
	  if (2 * size / time > max_rate)
	    max_rate = 2 * size / time;
	  if (time / 2 < min_latency)
	    min_latency = time / 2;
	}
      else if (myproc == 0)
	{
	  printf (" %7d bytes took less than the timer accuracy\n", size);
	}
    }
    MP_Leave(0.0);

  if (myproc == 0)
    printf ("\n Max rate = %f MB/sec  Min latency = %f usec\n",
	    max_rate, min_latency);

  MP_Time_Report("pong.out");
  MP_Finalize ();
}
