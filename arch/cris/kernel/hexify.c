#include <stdio.h>


void main()
{
	int c;
	int comma=0;
	int count=0;
	while((c=getchar())!=EOF)
	{
		unsigned char x=c;
		if(comma)
			printf(",");
		else
			comma=1;
		if(count==8)
		{
			count=0;
			printf("\n");
		}
		if(count==0)
			printf("\t");
		printf("0x%02X",c);
		count++;
	}
	if(count)
		printf("\n");
	exit(0);
}

		
