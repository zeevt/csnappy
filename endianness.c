#include <stdio.h>

int main()
{
	int n = 0x11223344;
	unsigned char c = *(unsigned char *)&n;
	if (c == 0x11)
		puts("CFLAGS += -D__BIG_ENDIAN");
	else if (c == 0x44)
		puts("CFLAGS += -D__LITTLE_ENDIAN");
	else {
		fputs("weird endian!", stderr);
		return 1;
	}
	return 0;
}
