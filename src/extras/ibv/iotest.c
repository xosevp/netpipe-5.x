#include <infiniband/verbs.h>
#include <stdio.h>

main()
{
   int allaccess = ( IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                     IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC );

   printf("IBV_ACCESS_LOCAL_WRITE = %d\n", IBV_ACCESS_LOCAL_WRITE);
   printf("IBV_ACCESS_LOCAL_WRITE = %d\n", IBV_ACCESS_REMOTE_WRITE);
   printf("IBV_ACCESS_LOCAL_WRITE = %d\n", IBV_ACCESS_REMOTE_READ);
   printf("IBV_ACCESS_LOCAL_WRITE = %d\n", IBV_ACCESS_REMOTE_ATOMIC);
   printf("allaccess = %d\n", allaccess);
}
